// AEnemyCharacter: patrol search, stealth lure investigate / re-target, the hunting query, the hearing path length.
// Split out of EnemyCharacter.cpp verbatim (cleanup 2026-10-19; map: docs/port/REFACTOR_ENEMY_2026-10.md).
// UE-only (stealth lure 2026-10-18; no Godot reference). Pure rules: AI/StealthLureRules, AI/PatrolRouteRules.

#include "Characters/EnemyCharacter.h"
#include "AI/StealthLureRules.h"
#include "AI/PatrolRouteActor.h"
#include "Data/EnemyPerception.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "CodexTactics.h"
#include "UI/FloatingTextSubsystem.h"
#include "AIController.h"
#include "Combat/HealthComponent.h"
#include "Engine/World.h"
#include "Combat/EnemyRegistrySubsystem.h"

bool AEnemyCharacter::IsHuntingSquad() const
{
	const bool bAlive = !bIsDying && HealthComponent && HealthComponent->IsAlive();
	return StealthLureRules::IsHuntingSquad(bAlive, IsOnPatrol(), CurrentTarget.IsValid(), bHasPatrolAlertLocation, bKnowsSquadPosition);
}

void AEnemyCharacter::StartPatrolSearch(const FVector& Location)
{
	StartPatrolSearchWith(Location, FPatrolSearchSpec());
}

ENoiseReaction AEnemyCharacter::StartPatrolSearchWith(const FVector& Location, const FPatrolSearchSpec& Spec)
{
	const ENoiseReaction Reaction = ApplySearchNoise(Location, Spec);
	if (!StealthLureRules::GoesToNoise(Reaction))
	{
		return Reaction;
	}
	// The whole patrol searches together, at the NEWEST noise: its leader and every escort of the patrol, searching or not
	// (fix 2026-10-18: a patrol mate already searching kept sweeping the old point - the hound stayed at the stone).
	AEnemyCharacter* Root = EscortLeader.Get() ? EscortLeader.Get() : this;
	TArray<AEnemyCharacter*> Group;
	if (Root != this)
	{
		Group.Add(Root);
	}
	for (AEnemyCharacter* Mate : UEnemyRegistrySubsystem::GetEnemyCharacters(GetWorld()))
	{
		if (Mate && Mate != this && Mate->EscortLeader.Get() == Root)
		{
			Group.Add(Mate);
		}
	}
	for (AEnemyCharacter* Mate : Group)
	{
		if (IsValid(Mate) && Mate->IsOnPatrol() && !Mate->IsDying())
		{
			Mate->ApplySearchNoise(Location, Spec);
		}
	}
	return Reaction;
}

ENoiseReaction AEnemyCharacter::ApplySearchNoise(const FVector& Location, const FPatrolSearchSpec& Spec)
{
	if (!IsOnPatrol() || bIsDying)
	{
		return ENoiseReaction::Ignored;
	}
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	FNoiseReactionInput Input;
	Input.bSearching = bSearching;
	Input.DistanceToCurrentNoiseCm = FVector::Dist2D(SearchNoise, Location);
	Input.SecondsSinceCurrentNoise = SearchNoiseTime >= 0.0 ? static_cast<float>(Now - SearchNoiseTime) : TNumericLimits<float>::Max();
	Input.bSniffing = ScentState.Phase == EHoundScentPhase::Sniffing;
	Input.bFollowingScent = ScentState.Phase == EHoundScentPhase::Following;
	Input.ScentDistanceCm = FVector::Dist2D(GetActorLocation(), ScentState.ScentPoint);
	Input.DistanceToSelfCm = FVector::Dist2D(GetActorLocation(), Location);
	Input.bLure = Spec.bLure;
	const ENoiseReaction Reaction = StealthLureRules::ReactToNoise(Input);
	if (Reaction == ENoiseReaction::KeepScent)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Stealth] %s keeps following a fresh scent %.1f m away: ignores the noise at %s"), *GetName(),
			Input.ScentDistanceCm / 100.f, *Location.ToCompactString());
		return Reaction;
	}
	if (Reaction == ENoiseReaction::SameNoise)
	{
		return Reaction; // its patrol mate pulled it to this noise an instant ago
	}
	// A sniffing hound / one on a far scent: the newer noise wins, the scent is dropped and the nose held until it gets there
	// (else it would catch the same scent again at once and the noise would be lost). Its own lost-scent search keeps the nose.
	const bool bDropScent = IsOnScent() && !Spec.bScent;
	if (bDropScent)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Stealth] %s drops the scent (%s, %.1f m away) for the noise at %s"), *GetName(),
			Input.bSniffing ? TEXT("sniffing") : TEXT("following"), Input.ScentDistanceCm / 100.f, *Location.ToCompactString());
		ResetScent();
	}
	bScentHeldByNoise = bScentHeldByNoise || bDropScent;
	const bool bWasSearching = bSearching;
	const FVector OldOrigin = SearchOrigin;
	bSearching = true;
	ActiveSearchSpec = Spec;
	SearchNoise = Location;
	SearchNoiseTime = Now;
	// The walk ends where this enemy can get to (fix 2026-10-18: the whistler's own spot may be off its navmesh / unreachable).
	SearchOrigin = Spec.bLure ? ResolveSearchPoint(Location) : Location;
	SearchGoal = SearchOrigin;
	SearchListenLeft = Spec.bLure ? FMath::Max(Spec.ListenSeconds, 0.f) : 0.f;
	SearchMoveFailures = 0;
	SearchElapsed = 0.f;
	SearchLegTime = 0.f;
	SearchLookLeft = 0.f;
	SearchStuckTime = 0.f;
	bSearchReachedOrigin = false;
	bSearchMoving = false;
	bEscortMoving = false;
	bPatrolMoveIssued = false;
	if (AAIController* AIC = Cast<AAIController>(GetController()); AIC && bWasSearching)
	{
		AIC->StopMovement(); // off the old leg / sweep at once (the listen beat or the new walk follows)
	}
	if (bWasSearching)
	{
		// A newer blast / noise: the search moves there and its clock restarts.
		UE_LOG(LogCodexTactics, Display, TEXT("[Patrol] %s moves its search to %s%s"), *GetName(), *Location.ToCompactString(),
			Spec.bLure ? TEXT(" (lure)") : TEXT(""));
		UE_LOG(LogCodexTactics, Display, TEXT("[Stealth] %s re-targets its search from %s to %s (%s, newer noise) at %.1f s"), *GetName(),
			*OldOrigin.ToCompactString(), *SearchOrigin.ToCompactString(), Spec.bLure ? TEXT("lure") : (Spec.bScent ? TEXT("scent") : TEXT("search")), Now);
		if (Spec.bLure)
		{
			UFloatingTextSubsystem::SpawnAboveEnemy(this, TEXT("? NOISE"), FLinearColor(1.f, 0.8f, 0.25f));
		}
		return ENoiseReaction::Retarget;
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Patrol] %s %s around %s"), *GetName(),
		Spec.bLure ? TEXT("investigates a noise") : (Spec.bScent ? TEXT("investigates a lost scent") : TEXT("searches")), *Location.ToCompactString());
	UE_LOG(LogCodexTactics, Display, TEXT("[Stealth] %s started by %s at %.1f s"), Spec.bLure ? TEXT("lure investigation") : TEXT("search"), *GetName(), Now);
	UFloatingTextSubsystem::SpawnAboveEnemy(this, Spec.bLure ? TEXT("? NOISE") : (Spec.bScent ? TEXT("? SCENT") : TEXT("❓ SEARCHING")),
		FLinearColor(1.f, 0.8f, 0.25f));
	return ENoiseReaction::Investigate;
}

float AEnemyCharacter::GetHearingPathLengthTo(const FVector& Point) const
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!Nav)
	{
		return -1.f;
	}
	const ANavigationData* NavData = Nav->GetNavDataForProps(GetNavAgentPropertiesRef(), GetActorLocation());
	FNavLocation OnNav;
	if (!Nav->ProjectPointToNavigation(Point, OnNav, FVector(150.f, 150.f, 300.f), NavData))
	{
		return -1.f;
	}
	const UNavigationPath* Path = Nav->FindPathToLocationSynchronously(World, GetActorLocation(), OnNav.Location, const_cast<AEnemyCharacter*>(this));
	if (!Path || !Path->IsValid() || Path->IsPartial())
	{
		return -1.f;
	}
	return Path->GetPathLength() + FVector::Dist2D(OnNav.Location, Point);
}

void AEnemyCharacter::TickPatrolSearch(float DeltaTime)
{
	const FPatrolSearchParams& Search = EnemyPerception::GetSearch();
	SearchElapsed += DeltaTime;
	const float Duration = ActiveSearchSpec.DurationSeconds >= 0.f ? ActiveSearchSpec.DurationSeconds
		: ULevelEncounterSubsystem::GetPatrolSearchSecondsIn(GetWorld());
	const float SweepRadius = ActiveSearchSpec.SweepRadiusCm >= 0.f ? ActiveSearchSpec.SweepRadiusCm : Search.SweepRadiusCm;
	if (PatrolRouteRules::IsSearchOver(SearchElapsed, Duration))
	{
		EndPatrolSearch();
		return;
	}
	AAIController* AIC = Cast<AAIController>(GetController());
	if (SearchLookLeft > 0.f)
	{
		// Looks around on the spot, then picks the next sweep point (reachable, within the sweep radius of the blast).
		SearchLookLeft -= DeltaTime;
		FaceYaw(GetActorRotation().Yaw + 60.f, DeltaTime, 1.5f);
		if (SearchLookLeft <= 0.f)
		{
			SearchGoal = PatrolRouteRules::PickSearchPoint(SearchOrigin, SweepRadius, FMath::FRand(), FMath::FRand());
			if (const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
			{
				FNavLocation Reachable;
				if (SweepRadius > 0.f && Nav->GetRandomReachablePointInRadius(SearchOrigin, SweepRadius, Reachable))
				{
					SearchGoal = Reachable.Location;
				}
			}
			SearchLegTime = 0.f;
			bSearchMoving = false;
		}
		return;
	}
	const bool bLureFirstLeg = ActiveSearchSpec.bLure && !bSearchReachedOrigin;
	if (bLureFirstLeg && SearchListenLeft > 0.f)
	{
		// A lure: it stops, turns towards the noise and listens, then walks there.
		SearchListenLeft -= DeltaTime;
		if (AIC && GetVelocity().Size2D() > 1.f)
		{
			AIC->StopMovement();
		}
		const FVector ToNoise = SearchNoise - GetActorLocation();
		if (!ToNoise.IsNearlyZero(1.f))
		{
			FaceYaw(ToNoise.Rotation().Yaw, DeltaTime, 6.f);
		}
		return;
	}
	const FVector Goal = bSearchReachedOrigin ? SearchGoal : SearchOrigin;
	SearchLegTime += DeltaTime;
	// Arrived: within the arrival radius, or held just short of it (a patrol mate on the point: no 10 s push against him).
	if (StealthLureRules::HasArrived(FVector::Dist2D(GetActorLocation(), Goal), StealthLureRules::GetArriveRadius(Search.ArriveCm, bLureFirstLeg), SearchStuckTime)
		|| SearchLegTime > Search.LegTimeoutSeconds || SearchMoveFailures >= StealthLureRules::GiveUpAfterFailures)
	{
		if (SearchMoveFailures >= StealthLureRules::GiveUpAfterFailures || SearchLegTime > Search.LegTimeoutSeconds)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Stealth] %s cannot get to %s (%d refused moves, %.1f s): searches from %.0f m off"), *GetName(),
				*Goal.ToCompactString(), SearchMoveFailures, SearchLegTime, FVector::Dist2D(GetActorLocation(), Goal) / 100.f);
		}
		SearchMoveFailures = 0;
		if (AIC)
		{
			AIC->StopMovement();
		}
		bScentHeldByNoise = false; // at the noise: the nose works again
		SearchStuckTime = 0.f; // the next leg starts its own stuck clock
		bSearchReachedOrigin = true;
		bSearchMoving = false;
		SearchLookLeft = FMath::Max(Search.LookAroundSeconds, 0.1f);
		return;
	}
	// (Re)issue the walk at the search pace: first time, or standing still for 1 s.
	SearchStuckTime = GetVelocity().Size2D() < 10.f ? SearchStuckTime + DeltaTime : 0.f;
	if (!bSearchMoving || SearchStuckTime > 1.f)
	{
		SearchStuckTime = 0.f;
		bSearchMoving = true;
		const float SearchSpeed = PatrolRouteRules::GetSearchSpeed(PatrolWalkSpeed, Search.SpeedMultiplier, BaseWalkSpeed);
		// A lure: the first leg (to the noise) at the enemy's own pace - hounds come fast, Brutes slowly.
		const bool bIssued = IssuePatrolMove(Goal, bLureFirstLeg
			? StealthLureRules::GetApproachSpeed(PatrolWalkSpeed, BaseWalkSpeed, ActiveSearchSpec.ApproachSpeedFraction, SearchSpeed) : SearchSpeed);
		SearchMoveFailures = bIssued ? 0 : SearchMoveFailures + 1;
		if (!bIssued)
		{
			bSearchMoving = false; // ask again next tick (no 1 s wait for a request that was never accepted)
			if (SearchMoveFailures == StealthLureRules::RepathAfterFailures)
			{
				// Re-path: the goal resolved again (the navmesh may have changed), or a reachable point next to it.
				FVector Repath = bLureFirstLeg ? ResolveSearchPoint(SearchNoise) : Goal;
				if (const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
				{
					FNavLocation Near;
					if (Nav->GetRandomReachablePointInRadius(Repath, 300.f, Near))
					{
						Repath = Near.Location;
					}
				}
				(bLureFirstLeg ? SearchOrigin : SearchGoal) = Repath;
			}
		}
	}
}

void AEnemyCharacter::EndPatrolSearch()
{
	if (!bSearching)
	{
		return;
	}
	bSearching = false;
	bSearchMoving = false;
	bScentHeldByNoise = false;
	PatrolSuspicion = 0.f;
	const bool bWasLure = ActiveSearchSpec.bLure;
	ActiveSearchSpec = FPatrolSearchSpec();
	if (AAIController* AIC = Cast<AAIController>(GetController()))
	{
		AIC->StopMovement();
	}
	// Back on the route at its nearest waypoint (an escort returns to its tether by itself).
	if (const APatrolRouteActor* Route = AssignedPatrolRoute; Route && Route->GetNumberOfWaypoints() > 0)
	{
		TArray<FVector> Points;
		for (int32 Index = 0; Index < Route->GetNumberOfWaypoints(); ++Index)
		{
			Points.Add(Route->GetWaypointWorldLocation(Index));
		}
		PatrolWaypointIndex = FMath::Max(PatrolRouteRules::FindNearestWaypoint(Points, GetActorLocation()), 0);
		PatrolPhase = EPatrolPhase::Moving;
	}
	bPatrolMoveIssued = false;
	PatrolStuckTime = 0.f;
	bEscortMoving = false;
	if (bGuardingPost && !AssignedPatrolRoute)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Patrol] %s gives up the search, back to its post"), *GetName());
	}
	else
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Patrol] %s gives up the search, back to waypoint %d"), *GetName(), PatrolWaypointIndex);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Stealth] %s timed out for %s at %.1f s"), bWasLure ? TEXT("lure investigation") : TEXT("search"), *GetName(),
		GetWorld()->GetTimeSeconds());
	if (!bIsDying)
	{
		UFloatingTextSubsystem::SpawnAboveEnemy(this, TEXT("STAND DOWN"), FLinearColor(0.7f, 0.8f, 0.9f));
	}
}

FVector AEnemyCharacter::ResolveSearchPoint(const FVector& Noise) const
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!Nav)
	{
		return Noise;
	}
	const ANavigationData* NavData = Nav->GetNavDataForProps(GetNavAgentPropertiesRef(), GetActorLocation());
	FNavLocation OnNav;
	const bool bOnNav = Nav->ProjectPointToNavigation(Noise, OnNav, FVector(150.f, 150.f, 300.f), NavData)
		|| Nav->ProjectPointToNavigation(Noise, OnNav, FVector(400.f, 400.f, 600.f), NavData);
	ELureGoalPath PathState = ELureGoalPath::None;
	FVector PathEnd = bOnNav ? OnNav.Location : Noise;
	if (bOnNav)
	{
		const UNavigationPath* Path = Nav->FindPathToLocationSynchronously(World, GetActorLocation(), OnNav.Location, const_cast<AEnemyCharacter*>(this));
		if (Path && Path->IsValid() && Path->PathPoints.Num() > 0)
		{
			PathState = Path->IsPartial() ? ELureGoalPath::Partial : ELureGoalPath::Full;
			PathEnd = Path->PathPoints.Last();
		}
	}
	const FVector Goal = StealthLureRules::ChooseInvestigatePoint(Noise, bOnNav, bOnNav ? OnNav.Location : Noise, PathState, PathEnd);
	UE_LOG(LogCodexTactics, Display, TEXT("[Stealth] %s walks to %s for the noise at %s (%s, %.0f cm off it)"), *GetName(), *Goal.ToCompactString(),
		*Noise.ToCompactString(), !bOnNav ? TEXT("off the navmesh") : PathState == ELureGoalPath::Full ? TEXT("reachable")
		: PathState == ELureGoalPath::Partial ? TEXT("partial path: as close as it gets") : TEXT("no path yet"), FVector::Dist2D(Goal, Noise));
	return Goal;
}
