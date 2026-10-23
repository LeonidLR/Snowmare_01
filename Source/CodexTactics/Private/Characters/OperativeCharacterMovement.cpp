// AOperativeCharacter: Move orders, formation moves, sprint / carry / wound speed, traffic hold, facing point.
// Split out of OperativeCharacter.cpp verbatim (cleanup day 2026-10-14; map: docs/port/REFACTOR_OPERATIVE_2026-10.md).
// Godot reference: Scenes/movements/player.gd (set_target, stop_movement, can_sprint, is_wounded,
// _process_leader_movement).

#include "Characters/OperativeCharacter.h"

#include "CodexTactics.h"
#include "Characters/OperativeAIController.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/KnockdownComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/SquadROE.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/TripwireActor.h"
#include "Interactables/TripwireRules.h"
#include "Ladders/LadderClimbComponent.h"
#include "Level/CrawlGapActor.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationData.h"
#include "Survival/ColdSurvivalComponent.h"
#include "Tactics/CoverTraceRules.h"
#include "TimerManager.h"
#include "Tutorial/TutorialRules.h"
#include "Tutorial/TutorialSubsystem.h"
#include "UI/FloatingTextSubsystem.h"
#include "UI/GameMessageSubsystem.h"
#include "Vehicles/VehicleSubsystem.h"

namespace
{
	/** Speed below which an operative without a move order counts as standing still, cm/s. */
	constexpr float MovingSpeedThreshold = 15.f;

	/** Dev trace (bug hunt 2026-10-18, HordeIntroSmoke "the planned move runs"): who ends a running move order. */
	TAutoConsoleVariable<int32> CVarOperativeMoveTrace(TEXT("Codex.Debug.MoveTrace"), 0,
		TEXT("1: log (with the call stack) every StopOperative that drops a running move order, and every move completion"));
}

EOperativeOrderResult AOperativeCharacter::OrderMoveTo(const FVector& Destination, bool bSprint)
{
	if (!bPendingMoveReplay)
	{
		++MoveOrderSerial; // every real order (player, interaction, relocation, bot) - the weather idle pose of THIS operative ends
	}
	if (IsRaging())
	{
		UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("⚠️ ENRAGED! IGNORING ORDERS!"), FLinearColor(1.f, 0.4f, 0.1f));
		return EOperativeOrderResult::Refused;
	}
	if (IsPanicking())
	{
		UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("⚠️ PANICKING! IGNORING ORDERS!"), FLinearColor(1.f, 0.3f, 0.3f));
		return EOperativeOrderResult::Refused;
	}
	// Rev 5i: a tutorial confinement holds the squad in an area - refused before anything changes (he keeps his cover, his walk).
	if (!bPendingMoveReplay && !UTutorialSubsystem::CheckConfinedOrder(this, this, Destination))
	{
		return EOperativeOrderResult::Refused;
	}
	// Sprint 14: knocked down — the order waits and runs once he is up (the latest one wins).
	if (IsKnockedDown())
	{
		TWeakObjectPtr<AOperativeCharacter> WeakThis(this);
		KnockdownComponent->BufferOrder([WeakThis, Destination, bSprint]()
		{
			if (AOperativeCharacter* Self = WeakThis.Get())
			{
				Self->OrderMoveTo(Destination, bSprint);
			}
		});
		UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("KNOCKED DOWN - ORDER QUEUED"), FLinearColor(1.f, 0.7f, 0.2f));
		return EOperativeOrderResult::Accepted;
	}
	// The pause's release of a planned wrap round the cover object (2026-10-20): the walk to its goal is the wrap.
	if (bCoverWrapPlanned && bHasPendingCover && bInCover && !bCoverMoveOrder
		&& FVector::Dist2D(Destination, CoverWrapPlan.GoalSlot.WorldLocation) <= 60.f)
	{
		return StartPlannedCoverWrap();
	}
	// Sprint 12: a move order away from the wall leaves the cover (a cover / shimmy order keeps it; so does the planned
	// walk to the pending slot that the tactical pause releases).
	const bool bToPendingSlot = bHasPendingCover && FVector::Dist2D(Destination, PendingCoverSlot.WorldLocation) <= 60.f;
	if (!bCoverMoveOrder && !bPendingMoveReplay && !bToPendingSlot)
	{
		if (bInCover)
		{
			LeaveCover(TEXT("move order"));
		}
		bHasPendingCover = false;
	}
	else if (bToPendingSlot && bInCover && !bCoverMoveOrder && !CoverTraceRules::IsSameWall(CoverSlot, PendingCoverSlot))
	{
		// The pause's release of a planned walk to ANOTHER cover (2026-10-20, found with the cover switch): he leaves this wall and runs
		// (before, he crossed to the other wall still "in cover" here: the cover pose slid along and the arrival played no enter clip).
		LeaveCover(TEXT("another cover"));
	}
	// Commander Mode (Sprint 07-B): every player move order pins the anchor the autonomy fights around.
	if (!bAutonomousOrder && !bPendingMoveReplay)
	{
		TacticalAnchor = SquadAutonomyRules::MakeAnchor(SquadROE::Get(), GetActorLocation(), Destination, GetActorRotation());
	}
	// User decision 2026-10-01: a sprint order (double click) stands a prone operative up — he rises in place, then runs;
	// any move ordered while he is getting up starts once he is up. SetStance arms the settle time, so the hold below covers
	// both this stand-up and a stance clip that is already playing (bug 2026-10-11: a move must never slide a rooted body).
	if (bSprint && Stance == EOperativeStance::Prone && !bCarrying
		&& OperativeMovementRules::CanSprint(MovementConfig, EOperativeStance::Standing, ColdLevel, IsWounded()))
	{
		SetStance(EOperativeStance::Standing);
	}
	const float RiseDelay = GetMoveHoldSeconds();
	if (OperativeMovementRules::ShouldDeferMove(RiseDelay) && GetWorld())
	{
		TWeakObjectPtr<AOperativeCharacter> WeakThis(this);
		GetWorld()->GetTimerManager().SetTimer(PendingMoveTimer, FTimerDelegate::CreateLambda([WeakThis, Destination, bSprint]()
		{
			if (AOperativeCharacter* Self = WeakThis.Get())
			{
				TGuardValue<bool> Replay(Self->bPendingMoveReplay, true);
				Self->OrderMoveTo(Destination, bSprint);
			}
		}), RiseDelay, false);
		bHasMoveOrder = true;
		bOwnMoveOrder = true;
		return EOperativeOrderResult::Accepted;
	}
	if (bSprint && CanSprint())
	{
		// A sprint order stands a crouching operative up (Godot set_target).
		if (Stance == EOperativeStance::Crouching)
		{
			Stance = EOperativeStance::Standing;
			++StanceSerial; // his own order's stance
		}
		bSprinting = true;
	}
	else
	{
		bSprinting = false;
	}
	ApplyMovementParams();
	const EOperativeOrderResult Result = RequestMove(Destination);
	bOwnMoveOrder = bHasMoveOrder; // rev 5g: his own order (the formation leaves him alone until it is done)
	UE_LOG(LogCodexTactics, Display, TEXT("%s: move to (%.0f, %.0f)%s -> %s"), *DisplayName.ToString(), Destination.X, Destination.Y,
		bSprinting ? TEXT(" sprint") : TEXT(""), Result == EOperativeOrderResult::Accepted ? TEXT("accepted")
		: Result == EOperativeOrderResult::Unreachable ? TEXT("unreachable") : TEXT("no controller"));
	return Result;
}

EOperativeOrderResult AOperativeCharacter::FollowTo(const FVector& Destination, float Speed)
{
	if (IsPanicking() || IsKnockedDown())
	{
		return EOperativeOrderResult::Refused; // the panic moves him (Godot _process_panic_movement); knocked down: he lies
	}
	if (bInCover || bHasPendingCover)
	{
		return EOperativeOrderResult::Refused; // Sprint 12: he holds his wall like a guard; the formation does not pull him
	}
	if (IsHoldingForSniperShot())
	{
		return EOperativeOrderResult::Refused; // kneeling for an ordered sniper shot: the formation waits for her
	}
	if (bQuickFireActive)
	{
		return EOperativeOrderResult::Refused; // firing a quick fire burst (2026-10-19): the formation waits for him
	}
	// Rev 5i: a tutorial confinement asks for a stance ("crouch first"): a follower copies the leader's low stance before he
	// follows, else he holds (the squad retries, as for a stance clip below).
	if (const UTutorialSubsystem* Tutorial = GetWorld() ? GetWorld()->GetSubsystem<UTutorialSubsystem>() : nullptr; Tutorial && Tutorial->IsConfining())
	{
		const FName Need = Tutorial->GetConfineStance();
		if (!Need.IsNone() && !TutorialRules::StanceSatisfies(UTutorialSubsystem::GetStanceName(Stance), Need))
		{
			const USquadSubsystem* FollowSquad = GetWorld()->GetSubsystem<USquadSubsystem>();
			const AOperativeCharacter* Leader = FollowSquad ? FollowSquad->GetLeader() : nullptr;
			if (Leader && Leader != this && TutorialRules::StanceSatisfies(UTutorialSubsystem::GetStanceName(Leader->GetStance()), Need)
				&& !OperativeMovementRules::ShouldDeferMove(GetMoveHoldSeconds()))
			{
				if (Leader->GetStance() == EOperativeStance::Prone)
				{
					DropProne();
				}
				else
				{
					SetStance(Leader->GetStance());
				}
				MarkStanceFromSquad(); // the confinement's copy, not the player's stance key
			}
			return EOperativeOrderResult::Refused;
		}
	}
	// A stance change is playing (the follower rises from prone / crouch, or lies down): the clip roots him, so no formation move
	// until it ends. The squad retries after GetMoveHoldSeconds (it keeps the slot, the order is not dropped).
	if (OperativeMovementRules::ShouldDeferMove(GetMoveHoldSeconds()))
	{
		return EOperativeOrderResult::Refused;
	}
	bOwnMoveOrder = false; // a formation move replaces any own order
	ApplyMovementParams(Speed);
	return RequestMove(Destination, /*bFormationMove*/ true);
}

float AOperativeCharacter::GetMoveHoldSeconds() const
{
	const UWorld* World = GetWorld();
	const UOperativeAnimInstance* Anim = GetMesh() ? Cast<UOperativeAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
	return OperativeMovementRules::ComputeMoveHold(World ? StanceSettleUntil - World->GetTimeSeconds() : 0.f,
		Anim ? Anim->GetStanceTransitionTimeLeft() : 0.f);
}

EOperativeOrderResult AOperativeCharacter::RequestMove(const FVector& InDestination, bool bFormationMove)
{
	if (VehicleQueries::IsMounted(this))
	{
		return EOperativeOrderResult::Refused; // APC P1: aboard he rides (Y gets him out)
	}
	// Ladders (LADDERS.md 7.1): a new order mid-climb - he walks there after the climb (turning round at the next rung when the goal
	// lies behind him, 2026-10-23); queued at a busy ladder: the
	// new order replaces the wait.
	if (LadderClimb && LadderClimb->IsOnLadder())
	{
		LadderClimb->HandleNewOrder(InDestination); // the walk after the climb; a goal behind him turns him round at the next rung
		LastMoveDestination = InDestination;
		bHasMoveOrder = true;
		return EOperativeOrderResult::Accepted;
	}
	if (LadderClimb && LadderClimb->IsQueued())
	{
		LadderClimb->AbortClimb(ELadderAbortReason::None);
	}
	// Rev 5i (game designer 2026-10-15): a tutorial confinement keeps the squad in its area. Own orders are judged (cover orders,
	// shimmies, replays of deferred orders reach this without OrderMoveTo's check); formation moves are pulled into the area.
	FVector Destination = InDestination;
	UTutorialSubsystem* Tutorial = GetWorld() ? GetWorld()->GetSubsystem<UTutorialSubsystem>() : nullptr;
	// 2026-10-23 ("lock_in_place"): an operative locked at his spot of the lesson is judged like a confined one (CheckConfinedOrder refuses
	// him with the hold line); a formation move leaves him where he stands without a line.
	const bool bConfined = Tutorial && (Tutorial->IsConfining() || Tutorial->IsHeldInPlace(*this));
	if (bConfined)
	{
		if (bFormationMove && Tutorial->IsMoveHeldInPlace(*this, Destination))
		{
			bHasMoveOrder = false;
			return EOperativeOrderResult::Refused;
		}
		if (bFormationMove)
		{
			Tutorial->ClampIntoConfinement(Destination);
		}
		else if (!UTutorialSubsystem::CheckConfinedOrder(this, this, Destination))
		{
			bHasMoveOrder = false;
			return EOperativeOrderResult::Refused;
		}
	}
	LastMoveDestination = Destination;
	ClearIdleFacing();
	bExplicitFacing = false; // a new move: no ordered facing any more (ThreatFacingRules may turn him after it)
	AOperativeAIController* AIController = Cast<AOperativeAIController>(GetController());
	if (!AIController)
	{
		return EOperativeOrderResult::NoController;
	}
	// Low fences (ACrawlGapActor, 2026-10-13): only a prone body may path through a crawl-only area; standing / crouched the
	// path goes round (or ends at the fence as a partial path, where the fence refuses him with the "go prone" hint).
	// Known traps (rev 5e, game designer 2026-10-14): only the squad AUTO mode (Ctrl+T) paths round a spotted / own wire or mine;
	// a player's order is literal - through the wire if that is the way (TripwireRules::ChooseOperativeMoveFilter).
	const USquadSubsystem* MoveSquad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	TSubclassOf<UNavigationQueryFilter> Filter;
	switch (TripwireRules::ChooseOperativeMoveFilter(Stance == EOperativeStance::Prone, MoveSquad && MoveSquad->IsAutonomousSquadCombat()))
	{
	case TripwireRules::EOperativeMoveFilter::NoCrawl: Filter = UNavFilter_NoCrawl::StaticClass(); break;
	case TripwireRules::EOperativeMoveFilter::TrapLiteral: Filter = UNavFilter_TrapLiteral::StaticClass(); break;
	case TripwireRules::EOperativeMoveFilter::NoCrawlTrapLiteral: Filter = UNavFilter_NoCrawlTrapLiteral::StaticClass(); break;
	default: break;
	}
	const EPathFollowingRequestResult::Type Result = AIController->MoveToLocation(
		Destination, MovementConfig.AcceptanceRadius, /*bStopOnOverlap*/ false, /*bUsePathfinding*/ true,
		/*bProjectDestinationToNavigation*/ true, /*bCanStrafe*/ false, Filter);
	if (Result == EPathFollowingRequestResult::Failed)
	{
		bHasMoveOrder = false;
		return EOperativeOrderResult::Unreachable;
	}
	// Rev 5i: the PATH END counts too (a destination inside the area reached only by a partial path ending outside).
	if (bConfined && Result == EPathFollowingRequestResult::RequestSuccessful)
	{
		const UPathFollowingComponent* PathFollowing = AIController->GetPathFollowingComponent();
		const FNavPathSharedPtr Path = PathFollowing ? PathFollowing->GetPath() : nullptr;
		if (Path.IsValid() && Path->IsValid() && Path->GetPathPoints().Num() > 0)
		{
			const ETutorialConfineVerdict EndVerdict = Tutorial->JudgeMove(*this, Path->GetEndLocation());
			if (EndVerdict != ETutorialConfineVerdict::Allowed)
			{
				AIController->StopMovement();
				bHasMoveOrder = false;
				if (!bFormationMove)
				{
					Tutorial->ReportConfinedRefusal(EndVerdict, this);
				}
				return EOperativeOrderResult::Refused;
			}
		}
	}
	bHasMoveOrder = Result == EPathFollowingRequestResult::RequestSuccessful;
	if (CVarOperativeMoveTrace.GetValueOnGameThread() != 0)
	{
		const UPathFollowingComponent* TracedFollowing = AIController->GetPathFollowingComponent();
		const FNavPathSharedPtr TracedPath = TracedFollowing ? TracedFollowing->GetPath() : nullptr;
		const bool bValidPath = TracedPath.IsValid() && TracedPath->IsValid() && TracedPath->GetPathPoints().Num() > 0;
		UE_LOG(LogCodexTactics, Display, TEXT("[MoveTrace] %s: path to (%.0f, %.0f): result %d, %d points, length %.0f, partial %d, ends %.0f cm from the goal"),
			*DisplayName.ToString(), Destination.X, Destination.Y, static_cast<int32>(Result), bValidPath ? TracedPath->GetPathPoints().Num() : 0,
			bValidPath ? TracedPath->GetLength() : 0.f, bValidPath && TracedPath->IsPartial() ? 1 : 0,
			bValidPath ? FVector::Dist2D(TracedPath->GetEndLocation(), Destination) : -1.f);
	}
	return EOperativeOrderResult::Accepted;
}

void AOperativeCharacter::SetTrafficHold(const AActor* Source, bool bHeld)
{
	if (!Source)
	{
		return;
	}
	TrafficHoldSources.RemoveAll([Source](const TWeakObjectPtr<const AActor>& Entry) { return !Entry.IsValid() || Entry.Get() == Source; });
	if (bHeld)
	{
		TrafficHoldSources.Add(Source);
	}
}

bool AOperativeCharacter::IsTrafficHeld() const
{
	TrafficHoldSources.RemoveAll([](const TWeakObjectPtr<const AActor>& Entry) { return !Entry.IsValid(); });
	return TrafficHoldSources.Num() > 0;
}

void AOperativeCharacter::StopOperative()
{
	if (bHasMoveOrder && CVarOperativeMoveTrace.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[MoveTrace] %s: StopOperative drops the move order to (%.0f, %.0f)"), *DisplayName.ToString(),
			LastMoveDestination.X, LastMoveDestination.Y);
		FDebug::DumpStackTraceToLog(ELogVerbosity::Display);
	}
	if (AController* OwnerController = GetController())
	{
		OwnerController->StopMovement();
	}
	GetCharacterMovement()->StopMovementImmediately();
	bHasMoveOrder = false;
	bOwnMoveOrder = false;
	bSprinting = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PendingMoveTimer);
	}
	ApplyMovementParams();
}

void AOperativeCharacter::SetSprinting(bool bNewSprinting)
{
	const bool bAllowed = bNewSprinting && CanSprint() && Stance == EOperativeStance::Standing;
	if (bSprinting != bAllowed)
	{
		bSprinting = bAllowed;
		ApplyMovementParams();
	}
}

void AOperativeCharacter::HandleMoveFinished()
{
	// During a wrap round the cover object (2026-10-20) he steers himself: a path following's end (an earlier move stopped) is no arrival.
	if (ContinueCoverWrapOnArrival())
	{
		return;
	}
	bHasMoveOrder = false;
	bOwnMoveOrder = false;
	bSprinting = false;
	ApplyMovementParams();

	// Sprint 12: arrived at the cover slot ordered -> press against the wall.
	if (bHasPendingCover)
	{
		if (FVector::Dist2D(GetActorLocation(), PendingCoverSlot.WorldLocation) <= 150.f)
		{
			bHasPendingCover = false;
			EnterCover(PendingCoverSlot);
			return;
		}
		UE_LOG(LogCodexTactics, Display, TEXT("%s: cover slot not reached (%.0f cm off), stays in the open"), *DisplayName.ToString(),
			FVector::Dist2D(GetActorLocation(), PendingCoverSlot.WorldLocation));
		bHasPendingCover = false;
		if (bInCover)
		{
			LeaveCover(TEXT("shimmy target not reached"));
		}
	}

	// Godot _on_movement_destination_reached: behind a barricade in combat (not frostbitten) the operative takes cover.
	const UGameFlowSubsystem* Flow = GetWorld() ? GetWorld()->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	const bool bCombat = (Flow && Flow->GetPhase() != ECodexGamePhase::Exploration) || CurrentCombatTarget.IsValid();
	if (!bCombat || Stance == EOperativeStance::Crouching || (ColdSurvival && ColdSurvival->IsFrostbitten()) || !IsBehindBarricade())
	{
		return;
	}
	SetStance(EOperativeStance::Crouching);
	UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("🛡️ IN COVER (-35% damage)"), FLinearColor(0.3f, 0.9f, 1.f));
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - LastCoverChatterTime >= 5.0)
	{
		LastCoverChatterTime = Now;
		const TCHAR* Callouts[] = { TEXT("🛡️ In cover!"), TEXT("🛡️ In cover, holding the sector!"),
			TEXT("🛡️ Behind the barricade, ready to fight!"), TEXT("🛡️ In position behind the shield, watching!") };
		if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
		{
			Messages->PostMessage(DisplayName, FText::FromString(Callouts[FMath::RandRange(0, 3)]));
		}
	}
}

void AOperativeCharacter::SetFacingPoint(const FVector& Point)
{
	StopOperative();
	ClearIdleFacing(); // an ordered facing is not undone by a parked follower's stale idle facing (it turned him back right after the disarm menu opened)
	bExplicitFacing = true; // nor by the turn to the last threat (ThreatFacingRules)
	const FVector Direction = (Point - GetActorLocation()).GetSafeNormal2D();
	if (!Direction.IsNearlyZero())
	{
		SetActorRotation(FRotator(0.f, Direction.Rotation().Yaw, 0.f));
	}
}

void AOperativeCharacter::SetCarrying(bool bNewCarrying)
{
	bCarrying = bNewCarrying;
	if (bCarrying)
	{
		bSprinting = false; // Godot can_sprint: no sprint while carrying
	}
	ApplyMovementParams();
}

bool AOperativeCharacter::IsWounded() const
{
	// Godot is_wounded: below the wounded threshold of max health (bWounded forces it, e.g. in tests).
	return bWounded || (HealthComponent && HealthComponent->GetMaxHealth() > 0.f
		&& HealthComponent->GetCurrentHealth() / HealthComponent->GetMaxHealth() < MovementConfig.WoundedHealthThreshold);
}

bool AOperativeCharacter::CanSprint() const
{
	return !bCarrying && OperativeMovementRules::CanSprint(MovementConfig, Stance, ColdLevel, IsWounded());
}

float AOperativeCharacter::GetMaxSpeed() const
{
	return OperativeMovementRules::ComputeMaxSpeed(MovementConfig, Stance, bSprinting, IsWounded(), bCarrying) * ColdSpeedMultiplier;
}

void AOperativeCharacter::HandleHealthChanged(float NewHealth, float MaxHealth, float Delta)
{
	if (bSprinting && !CanSprint())
	{
		bSprinting = false;
	}
	ApplyMovementParams(); // the wounded speed follows the health
}

void AOperativeCharacter::SetColdSpeedMultiplier(float Multiplier)
{
	if (!FMath::IsNearlyEqual(ColdSpeedMultiplier, Multiplier))
	{
		ColdSpeedMultiplier = Multiplier;
		ApplyMovementParams();
	}
}

bool AOperativeCharacter::IsMoving() const
{
	return bHasMoveOrder || GetVelocity().Size2D() > MovingSpeedThreshold;
}

void AOperativeCharacter::ApplyMovementParams(float SpeedOverride)
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->MaxWalkSpeed = SpeedOverride >= 0.f ? SpeedOverride : GetMaxSpeed();
	Movement->RotationRate = FRotator(0.f, OperativeMovementRules::GetTurnRate(MovementConfig, Stance), 0.f);
}

EOperativeOrderResult AOperativeCharacter::AutonomousMoveTo(const FVector& Destination)
{
	TGuardValue<bool> Autonomous(bAutonomousOrder, true);
	return OrderMoveTo(Destination, false);
}
