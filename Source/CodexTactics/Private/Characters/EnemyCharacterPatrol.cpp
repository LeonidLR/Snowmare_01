// AEnemyCharacter: patrol routes, escorts, guard posts, patrol break and its propagation, trap alerts.
// Split out of EnemyCharacter.cpp verbatim (cleanup 2026-10-19; map: docs/port/REFACTOR_ENEMY_2026-10.md).
// UE-only (Sprint 11 patrols, guard posts 2026-10-11; no Godot reference). Pure rules: AI/PatrolRouteRules.

#include "Characters/EnemyCharacter.h"
#include "AI/PatrolRouteActor.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "Core/PlayerCommands.h"
#include "CodexTactics.h"
#include "UI/FloatingTextSubsystem.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "Combat/HealthComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Combat/EnemyRegistrySubsystem.h"

// --- Sprint 11 patrols -----------------------------------------------------------------------------------------------

void AEnemyCharacter::SetEscortLeader(AEnemyCharacter* NewLeader)
{
	if (HasActorBegunPlay())
	{
		StartPatrol(AssignedPatrolRoute, NewLeader);
		return;
	}
	EscortLeader = NewLeader != this ? NewLeader : nullptr;
}

void AEnemyCharacter::StartPatrol(APatrolRouteActor* Route, AEnemyCharacter* Leader)
{
	if (bIsDying)
	{
		return;
	}
	AssignedPatrolRoute = Route;
	EscortLeader = Leader;
	InitPatrol();
}

void AEnemyCharacter::InitPatrol()
{
	if (EscortLeader.Get() == this)
	{
		EscortLeader = nullptr;
	}
	if (AssignedPatrolRoute || EscortLeader.IsValid())
	{
		bGuardingPost = false; // a route / a leader of its own replaces the guard post
	}
	bGuardReturning = false;
	ResetScent();
	bPatrolActive = AssignedPatrolRoute != nullptr || EscortLeader.IsValid() || bGuardingPost;
	bEscortMoving = false;
	PatrolPhase = EPatrolPhase::Moving;
	PatrolWaypointIndex = 0;
	bPatrolForward = true;
	bPatrolMoveIssued = false;
	// Spread the sight traces of a group over the 0.2 s period.
	PatrolSightTimer = FMath::FRandRange(0.f, 0.2f);
	if (bPatrolActive)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Patrol] %s %s (route %s, leader %s)"), *GetName(), bGuardingPost ? TEXT("guards its post") : TEXT("patrols"),
			AssignedPatrolRoute ? *AssignedPatrolRoute->GetName() : TEXT("-"), EscortLeader.IsValid() ? *EscortLeader->GetName() : TEXT("-"));
	}
}

void AEnemyCharacter::TickPatrolBehavior(float DeltaTime)
{
	// Sight / hearing / smell (user request 2026-10-06): a detection breaks the patrol into Engage.
	if (TickPatrolPerception(DeltaTime))
	{
		return;
	}
	if (EscortLeader.IsValid() || EscortLeader.IsStale())
	{
		const AEnemyCharacter* Leader = EscortLeader.Get();
		if (!Leader || Leader->IsDying() || !Leader->GetHealthComponent() || !Leader->GetHealthComponent()->IsAlive())
		{
			// Lost its leader: hunts around his last spot (a trap may have killed him; a squad shot that did was heard,
			// or its hit started the fight already — user amendment 2026-10-06).
			const FVector Where = Leader ? Leader->GetActorLocation() : GetActorLocation();
			EscortLeader = nullptr;
			StartPatrolSearch(Where);
			return;
		}
		// Mirrors the leader: he broke off (or never patrolled) -> so does the escort.
		FPatrolAlertInput Input;
		Input.bPartnerAlerted = !Leader->IsOnPatrol();
		if (PatrolRouteRules::ShouldBreakPatrol(Input))
		{
			BreakPatrol(EPatrolAlertCause::PartnerAlert, Leader->GetActorLocation());
			return;
		}
		if (TickScentBehavior(DeltaTime))
		{
			return; // sniffing / following a scent: off the tether for now
		}
		if (bSearching)
		{
			TickPatrolSearch(DeltaTime);
			return;
		}
		TickEscort(DeltaTime, *Leader);
		return;
	}
	if (TickScentBehavior(DeltaTime))
	{
		return; // sniffing / following a scent (hounds): the route / post / search waits
	}
	if (bSearching)
	{
		TickPatrolSearch(DeltaTime);
		return;
	}
	if (bGuardingPost && !AssignedPatrolRoute)
	{
		TickGuardPost(DeltaTime);
		return;
	}
	TickPatrolRoute(DeltaTime);
}

bool AEnemyCharacter::TryStartGuardPost()
{
	if (bIsDying)
	{
		return false;
	}
	if (bGuardingPost)
	{
		return IsOnPatrol();
	}
	const UWorld* World = GetWorld();
	const ULevelEncounterSubsystem* Encounter = World ? World->GetSubsystem<ULevelEncounterSubsystem>() : nullptr;
	if (!PatrolRouteRules::ShouldGuardPost(Encounter && Encounter->IsLevelEnemy(this), AssignedPatrolRoute != nullptr || HasLegacyPatrolRoute(),
		EscortLeader.IsValid(), bGuardPostWhenUnrouted))
	{
		return false;
	}
	GuardPostLocation = GetActorLocation();
	GuardPostYaw = GetActorRotation().Yaw;
	// Spread the look-around of a group (the first look comes after a while, not all heads at once).
	GuardClock = FMath::FRandRange(0.f, 0.3f) * GuardLookPeriodSeconds;
	bGuardingPost = true;
	StartPatrol(nullptr, nullptr); // virtual: the marksman enters his patrol state too
	return IsOnPatrol();
}

void AEnemyCharacter::TickGuardPost(float DeltaTime)
{
	// Back to the post after a search / a shove; a guard does not wander.
	const float Away = FVector::Dist2D(GetActorLocation(), GuardPostLocation);
	if (PatrolRouteRules::ShouldWalkBackToPost(Away, bGuardReturning))
	{
		PatrolStuckTime = GetVelocity().Size2D() < 10.f ? PatrolStuckTime + DeltaTime : 0.f;
		if (!bGuardReturning || !bPatrolMoveIssued || PatrolStuckTime > 1.f)
		{
			PatrolStuckTime = 0.f;
			IssuePatrolMove(GuardPostLocation, PatrolWalkSpeed);
		}
		bGuardReturning = true;
		return;
	}
	if (bGuardReturning)
	{
		bGuardReturning = false;
		bPatrolMoveIssued = false;
		if (AAIController* AIC = Cast<AAIController>(GetController()))
		{
			AIC->StopMovement();
		}
	}
	// Faces its placement rotation and looks around now and then (its sight cone sweeps with it).
	GuardClock += DeltaTime;
	FaceYaw(GuardPostYaw + PatrolRouteRules::GetGuardLookOffset(GuardClock, GuardLookAroundDeg, GuardLookPeriodSeconds), DeltaTime, 2.f);
}

void AEnemyCharacter::LogStealthDetection(EPatrolAlertCause Cause, bool bWasSearching) const
{
	// One line per patrol break for the Jev AI coach (Scripts/Tools/jev_ai_coach.py --stealth): sense, world time.
	const TCHAR* Sense = TEXT("other");
	switch (Cause)
	{
	case EPatrolAlertCause::Sight: Sense = TEXT("sight"); break;
	case EPatrolAlertCause::Hearing: Sense = TEXT("hearing"); break;
	case EPatrolAlertCause::Smell: Sense = TEXT("smell"); break;
	case EPatrolAlertCause::Damage: Sense = TEXT("damage"); break;
	case EPatrolAlertCause::PartnerAlert: Sense = TEXT("partner"); break;
	default: break;
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Stealth] detection by %s at %.1f s (%s%s)"), Sense, GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0,
		*GetName(), bWasSearching ? TEXT(", search found the squad") : TEXT(""));
}

void AEnemyCharacter::TickEscort(float DeltaTime, const AEnemyCharacter& Leader)
{
	AAIController* AIC = Cast<AAIController>(GetController());
	const FEscortDecision Decision = PatrolRouteRules::EvaluateEscort(GetActorLocation(), Leader.GetActorLocation(), bEscortMoving);
	if (!Decision.bShouldMove)
	{
		if (bEscortMoving && AIC)
		{
			AIC->StopMovement();
		}
		bEscortMoving = false;
		return; // within the tether: mills about (idle)
	}
	// Far behind (a fresh spawn, a detour): runs to catch up; at the band edge it walks at the patrol pace.
	const float Distance = FVector::Dist2D(GetActorLocation(), Leader.GetActorLocation());
	const float Speed = Distance > PatrolRouteRules::EscortMaxCm * 2.f ? FMath::Max(BaseWalkSpeed, PatrolWalkSpeed) : PatrolWalkSpeed * 1.25f;
	if (!bEscortMoving || !bPatrolMoveIssued || GetVelocity().Size2D() < 10.f || FVector::Dist2D(Decision.Destination, EscortGoal) > 100.f)
	{
		IssuePatrolMove(Decision.Destination, Speed);
		EscortGoal = Decision.Destination;
	}
	bEscortMoving = true;
}

void AEnemyCharacter::TickPatrolRoute(float DeltaTime)
{
	const APatrolRouteActor* Route = AssignedPatrolRoute;
	AAIController* AIC = Cast<AAIController>(GetController());
	if (!Route || Route->GetNumberOfWaypoints() == 0 || PatrolPhase == EPatrolPhase::Finished)
	{
		return;
	}
	PatrolWaypointIndex = FMath::Clamp(PatrolWaypointIndex, 0, Route->GetNumberOfWaypoints() - 1);
	const FVector Waypoint = Route->GetWaypointWorldLocation(PatrolWaypointIndex);
	switch (PatrolPhase)
	{
	case EPatrolPhase::Moving:
		if (FVector::Dist2D(GetActorLocation(), Waypoint) <= PatrolRouteRules::WaypointAcceptCm)
		{
			if (AIC)
			{
				AIC->StopMovement();
			}
			bPatrolMoveIssued = false;
			PatrolPhase = EPatrolPhase::Waiting;
			PatrolWaitLeft = Route->GetWaitTimeAtWaypoint(PatrolWaypointIndex);
			return;
		}
		// (Re)issue the walk: first time, or standing still for 1 s (an avoidance shove, a lost path).
		PatrolStuckTime = GetVelocity().Size2D() < 10.f ? PatrolStuckTime + DeltaTime : 0.f;
		if (!bPatrolMoveIssued || PatrolStuckTime > 1.f)
		{
			PatrolStuckTime = 0.f;
			bPatrolMoveIssued = true;
			IssuePatrolMove(Waypoint, PatrolWalkSpeed);
		}
		return;
	case EPatrolPhase::Waiting:
		PatrolWaitLeft -= DeltaTime;
		if (PatrolWaitLeft <= 0.f)
		{
			const int32 Next = Route->GetNextWaypointIndex(PatrolWaypointIndex, bPatrolForward);
			if (Next == INDEX_NONE)
			{
				PatrolPhase = EPatrolPhase::Finished; // a one-way route ends here: it stands guard
				return;
			}
			PatrolWaypointIndex = Next;
			PatrolPhase = EPatrolPhase::Turning;
			PatrolTurnTime = 0.f;
		}
		return;
	case EPatrolPhase::Turning:
	{
		// Turns on the spot towards the next segment, then walks.
		const FVector ToNext = Waypoint - GetActorLocation();
		const float WantedYaw = ToNext.Rotation().Yaw;
		PatrolTurnTime += DeltaTime;
		if (ToNext.Size2D() < 1.f || FMath::Abs(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw, WantedYaw)) < 8.f || PatrolTurnTime > 2.f)
		{
			PatrolPhase = EPatrolPhase::Moving;
			bPatrolMoveIssued = false;
			return;
		}
		FaceYaw(WantedYaw, DeltaTime, 4.f);
		return;
	}
	default:
		return;
	}
}

bool AEnemyCharacter::IssuePatrolMove(const FVector& Goal, float Speed)
{
	GetCharacterMovement()->MaxWalkSpeed = Speed;
	bPatrolMoveIssued = true;
	if (AAIController* AIC = Cast<AAIController>(GetController()))
	{
		// own gating (first issue, 1 s stuck, escort goal moved): never throttled
		return AIC->MoveToLocation(Goal, 50.f, false, true) != EPathFollowingRequestResult::Failed;
	}
	return false;
}

void AEnemyCharacter::BreakPatrol(EPatrolAlertCause Cause, const FVector& AlertLocation)
{
	if (!bPatrolActive)
	{
		return;
	}
	const bool bWasSearching = bSearching;
	LastPatrolBreakCause = Cause;
	bHasBrokenPatrol = true;
	ResetScent();
	bPatrolActive = false;
	bGuardingPost = false;
	bGuardReturning = false;
	bEscortMoving = false;
	bSearching = false;
	PatrolSuspicion = 0.f;
	GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed;
	if (AAIController* AIC = Cast<AAIController>(GetController()))
	{
		AIC->StopMovement();
	}
	PatrolAlertLocation = AlertLocation;
	bHasPatrolAlertLocation = true;
	UE_LOG(LogCodexTactics, Display, TEXT("[Patrol] %s breaks its patrol (cause %d)"), *GetName(), static_cast<int32>(Cause));
	LogStealthDetection(Cause, bWasSearching);
	if (!bIsDying)
	{
		UFloatingTextSubsystem::SpawnAboveEnemy(this, TEXT("❗ ALARM!"), FLinearColor(1.f, 0.35f, 0.2f));
		// Game designer 2026-10-20: an ABP with an alert clip roars at the squad before it charges (Military Zombie).
		StartAlertPerformance(AlertLocation);
	}
	PropagatePatrolBreak(AlertLocation);
	// 2026-10-21: the tutorial hears it first (the A4 "spotted" card pauses the fight that starts right below).
	{
		const TCHAR* CauseName = TEXT("Other");
		switch (Cause)
		{
		case EPatrolAlertCause::Sight: CauseName = TEXT("Sight"); break;
		case EPatrolAlertCause::Hearing: CauseName = TEXT("Hearing"); break;
		case EPatrolAlertCause::Smell: CauseName = TEXT("Smell"); break;
		case EPatrolAlertCause::Damage: CauseName = TEXT("Damage"); break;
		case EPatrolAlertCause::PartnerAlert: CauseName = TEXT("PartnerAlert"); break;
		case EPatrolAlertCause::LeaderLost: CauseName = TEXT("LeaderLost"); break;
		default: break;
		}
		GameplayEvents::Raise(this, FName(*FString::Printf(TEXT("%s.%s"), *GameplayEvents::EnemyAlerted.ToString(), CauseName)), this);
		GameplayEvents::Raise(this, GameplayEvents::EnemyAlerted, this);
	}
	// User request 2026-10-06: an enemy that engages the squad starts the fight on an ambush level.
	ULevelEncounterSubsystem::NotifyHostileContactIn(GetWorld(), EAmbushTrigger::PatrolDetection, this);
}

void AEnemyCharacter::PropagatePatrolBreak(const FVector& AlertLocation)
{
	if (AEnemyCharacter* Leader = EscortLeader.Get(); Leader && Leader->IsOnPatrol())
	{
		Leader->BreakPatrol(EPatrolAlertCause::PartnerAlert, AlertLocation);
	}
	for (AEnemyCharacter* Mate : UEnemyRegistrySubsystem::GetEnemyCharacters(GetWorld()))
	{
		if (Mate != this && Mate->EscortLeader.Get() == this && Mate->IsOnPatrol())
		{
			Mate->BreakPatrol(EPatrolAlertCause::PartnerAlert, AlertLocation);
		}
	}
}

void AEnemyCharacter::NotifyTrapTriggered(const FVector& Location)
{
	if (!IsOnPatrol() || bIsDying)
	{
		return;
	}
	FPatrolAlertInput Input;
	Input.TrapDistanceCm = FVector::Dist2D(GetActorLocation(), Location);
	Input.TrapAlertRadiusCm = PatrolTrapAlertRadius;
	if (PatrolRouteRules::ShouldStartSearch(Input))
	{
		StartPatrolSearch(Location);
	}
}

void AEnemyCharacter::AlertPatrolsNearTrap(UWorld* World, const FVector& Location)
{
	if (!World)
	{
		return;
	}
	TArray<AEnemyCharacter*> Enemies;
	for (AEnemyCharacter* Mate : UEnemyRegistrySubsystem::GetEnemyCharacters(World))
	{
		Enemies.Add(Mate);
	}
	for (AEnemyCharacter* Enemy : Enemies)
	{
		if (IsValid(Enemy))
		{
			Enemy->NotifyTrapTriggered(Location);
		}
	}
}
