// AOperativeCharacter: Move orders, formation moves, sprint / carry / wound speed, traffic hold, facing point.
// Split out of OperativeCharacter.cpp verbatim (cleanup day 2026-10-14; map: docs/port/REFACTOR_OPERATIVE_2026-10.md).
// Godot reference: Scenes/movements/player.gd (set_target, stop_movement, can_sprint, is_wounded,
// _process_leader_movement).

#include "Characters/OperativeCharacter.h"
#include "Training/TrainingSectorSubsystem.h"
#include "Interactables/WindowOrderRules.h"
#include "Core/CodexGameplayRandom.h"
#include "Interactables/VaultNavigation.h"
#include "AI/PmcEncounterNav.h"
#include "Tactics/TurnBasedCombatSubsystem.h"

#include "CodexTactics.h"
#include "Characters/CriticalWoundRules.h"
#include "Characters/OperativeAIController.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/SquadGiveWayRules.h"
#include "Characters/SquadSubsystem.h"
#include "Characters/StealthModeRules.h"
#include "Combat/HealthComponent.h"
#include "Combat/KnockdownComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/SquadROE.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/MantleNavigation.h"
#include "Interactables/TripwireActor.h"
#include "Interactables/TripwireRules.h"
#include "Falls/LedgeFallComponent.h"
#include "Ladders/LadderClimbComponent.h"
#include "Level/CrawlGapActor.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationData.h"
#include "NavMesh/NavMeshPath.h"
#include "Survival/ColdSurvivalComponent.h"
#include "Tactics/LookHoldRules.h"
#include "Tactics/CoverTraceRules.h"
#include "TimerManager.h"
#include "Tutorial/TutorialRules.h"
#include "Tutorial/TutorialSubsystem.h"
#include "Interactables/DoorSubsystem.h"
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
	// Mantling slice 1 (designer 2026-11-15, trigger C): only the operative the player commands in Solo paths over the mantle links (a
	// formation follow never does); a new order replaces a walk to a clicked ledge top (OrderMantleTop sets it again after this).
	// R17-1 (designer 2026-11-17): the formation leader's own order too - when his path climbs, his followers climb in turns behind him
	// (USquadSubsystem::BeginSquadMantleFromPath; Codex.Mantle.Squad 0 = before R17-1).
	// A hostile's (PMC, TeamId != 0) commander order always may climb: the player squad's Solo / formation mode is not his
	// (095 PmcTraversalSmoke 2026-11-23; OperativeMovementRules::IsMantlePathAllowed).
	bMantleOrderPending = false;
	{
		const USquadSubsystem* MantleSquad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
		bMantlePathAllowed = OperativeMovementRules::IsMantlePathAllowed(bAutonomousOrder, IsPlayerTeam(), MantleSquad && MantleSquad->IsSoloMode(),
			USquadSubsystem::IsSquadMantleEnabled(), MantleSquad && MantleSquad->GetFormationLeader() == this);
		// The click's window permission, latched for THIS order (the squad flag is reset by the next click and read by nobody later).
		bWindowPathAllowed = !bAutonomousOrder && IsPlayerTeam() && MantleSquad && MantleSquad->IsWindowOrder();
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
		bRunCornerPeekPending = false; // any other order replaces a run-and-peek (#55)
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
	// Decision 6 (2026-11-01 night): crouched by the autonomy on a halt (the hold crouch), his next own order stands him up first.
	if (IsHoldCrouch() && !bCarrying && !bPendingMoveReplay)
	{
		SetStance(EOperativeStance::Standing);
		MarkStanceFromSquad();
	}
	// Formation wall layout (Codex.Formation.Sectors): crouched there by the squad, his next own order stands him up first (the stance clip
	// defers the move below, as the rise from prone).
	if (bFormationWallPose && !bPendingMoveReplay)
	{
		bFormationWallPose = false;
		if (Stance == EOperativeStance::Crouching && IsStanceFromSquad() && !bCarrying)
		{
			SetStance(EOperativeStance::Standing);
		}
	}
	const float RiseDelay = GetMoveHoldSeconds();
	if (OperativeMovementRules::ShouldDeferMove(RiseDelay) && GetWorld())
	{
		if ((!IsInStealthMode() || !bSprint) && bStealthFast)
		{
			bStealthFast = false;
			ApplyMovementParams();
		}
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
	if (IsInStealthMode())
	{
		// Stealth v2: single-click = Slow stealth, double-click = Fast stealth; never stands up or breaks stealth.
		bStealthFast = bSprint && CanStealthFast();
		bSprinting = false;
	}
	else if (bSprint && CanSprint())
	{
		if (HasManualLookPoint())
		{
			ClearManualLookPoint();
			UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("LOOK CLEARED"), FLinearColor(0.75f, 0.85f, 0.95f));
		}
		// A sprint order stands a crouching operative up (Godot set_target).
		if (Stance == EOperativeStance::Crouching)
		{
			Stance = EOperativeStance::Standing;
			++StanceSerial; // his own order's stance
		}
		bStealthFast = false;
		bSprinting = true;
	}
	else
	{
		bStealthFast = false;
		bSprinting = false;
	}
	ApplyMovementParams();
	// The order's destination is always the click (the far landing). Formation followers read GetLastMoveDestination; a walk to
	// his own side must not replace it (FormationWindowSmoke: the third man never crossed while the goal sat outside).
	EOperativeOrderResult Result = RequestMove(Destination);
	bOwnMoveOrder = bHasMoveOrder; // rev 5g: his own order (the formation leaves him alone until it is done)
	if (bWindowApproachPending)
	{
		const USquadSubsystem* WindowSquad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
		// The column files through behind him (FormationWindowSmoke). An outside via parks the rear man on the near side: he never
		// crosses. Solo, and a man ordered on his own, still walk to their own face with whole windows closed so nav cannot go around.
		const bool bFormationLeader = WindowSquad && !WindowSquad->IsSoloMode() && WindowSquad->GetFormationLeader() == this
			&& WindowSquad->GetMembers().Num() > 1;
		const FWindowOrderTuning& WindowTuning = WindowOrderRules::Get();
		const bool bThisWindow = FVector::Dist2D(Destination, WindowApproachLanding) <= WindowTuning.ApproachMatchCm;
		if (!bFormationLeader && Result == EOperativeOrderResult::Accepted && bHasMoveOrder && bThisWindow)
		{
			// This leg only walks to his own side, and must not enter another opening. SteerOwnMoveVia keeps the landing.
			bWindowApproachWindows = bWindowPathAllowed;
			bWindowPathAllowed = false;
			if (!SteerOwnMoveVia(WindowApproachPoint))
			{
				UE_LOG(LogCodexTactics, Display, TEXT("%s: window approach unreachable, walking to the far side"), *DisplayName.ToString());
				bWindowApproachPending = false;
				bWindowPathAllowed = bWindowApproachWindows;
				Result = RequestMove(Destination);
				bOwnMoveOrder = bHasMoveOrder;
			}
		}
		else
		{
			bWindowApproachPending = false;
		}
	}
	UE_LOG(LogCodexTactics, Display, TEXT("%s: move to (%.0f, %.0f)%s%s -> %s"), *DisplayName.ToString(), Destination.X, Destination.Y,
		bSprinting ? TEXT(" sprint") : TEXT(""), bWindowApproachPending ? TEXT(" (near side first, windows closed)") : TEXT(""),
		Result == EOperativeOrderResult::Accepted ? TEXT("accepted")
		: Result == EOperativeOrderResult::Unreachable ? TEXT("unreachable") : TEXT("no controller"));
	return Result;
}

void AOperativeCharacter::ArmWindowApproach(const FVector& ApproachFeet, const FVector& LandingFeet)
{
	bWindowApproachPending = true;
	WindowApproachPoint = ApproachFeet;
	WindowApproachLanding = LandingFeet;
}

EOperativeOrderResult AOperativeCharacter::FollowTo(const FVector& Destination, float Speed, float PathBrakingCm)
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
	if (bQuickFireActive || bSuppressActive)
	{
		return EOperativeOrderResult::Refused; // firing a quick fire burst (2026-10-19) / suppressing (2026-10-26): the formation waits for him
	}
	if (LadderClimb && LadderClimb->IsBusy())
	{
		return EOperativeOrderResult::Refused; // on a ladder / waiting for one (LADDERS.md 7.1): the formation waits for him
	}
	if (const USquadSubsystem* MantleSquad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr; MantleSquad && MantleSquad->IsInSquadMantle(this))
	{
		return EOperativeOrderResult::Refused; // R17-1: in the squad's climb (waiting his turn, climbing, covering up there): the squad moves him
	}
	if (LedgeFall && LedgeFall->IsBusy())
	{
		return EOperativeOrderResult::Refused; // dropping off an edge / waiting for his turn at a drop (2026-11-03): the formation waits for him
	}
	if (IsCommittedToNavLinkDrop())
	{
		return EOperativeOrderResult::Refused; // over a hole link's rim (the soft-lock of 2026-11-03): no re-path off the rim - he drops first
	}
	// Rev 5i: a tutorial confinement asks for a stance ("crouch first"): a follower copies the leader's low stance before he
	// follows, else he holds (the squad retries, as for a stance clip below).
	if (const UTutorialSubsystem* Tutorial = GetWorld() ? GetWorld()->GetSubsystem<UTutorialSubsystem>() : nullptr; Tutorial && Tutorial->IsConfining())
	{
		const FName Need = Tutorial->GetConfineStance();
		if (!Need.IsNone() && !TutorialRules::StanceSatisfies(UTutorialSubsystem::GetStanceName(Stance), Need))
		{
			const USquadSubsystem* FollowSquad = GetWorld()->GetSubsystem<USquadSubsystem>();
			const AOperativeCharacter* Leader = FollowSquad ? FollowSquad->GetFormationLeader() : nullptr; // the one he follows
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
	// Decision 6 (2026-11-01 night): the hold crouch the autonomy gave him on a halt ends with the formation's next move - he stands up at once
	// (the stance clip defers this move; the squad retries) when the leader walks upright.
	if (IsHoldCrouch())
	{
		const USquadSubsystem* HoldSquad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
		const AOperativeCharacter* HoldLeader = HoldSquad ? HoldSquad->GetFormationLeader() : nullptr;
		if (HoldLeader && HoldLeader != this && SquadAutonomyRules::ShouldRiseFromHoldCrouch(true, bCarrying, HoldLeader->GetStance()))
		{
			SetStance(EOperativeStance::Standing);
			MarkStanceFromSquad();
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
	if (PathBrakingCm >= 0.f)
	{
		GetCharacterMovement()->GetNavMovementProperties()->FixedPathBrakingDistance = PathBrakingCm; // ApplyMovementParams restored the config's
	}
	return RequestMove(Destination, /*bFormationMove*/ true);
}

float AOperativeCharacter::GetMoveHoldSeconds() const
{
	const UWorld* World = GetWorld();
	const UOperativeAnimInstance* Anim = GetMesh() ? Cast<UOperativeAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
	return OperativeMovementRules::ComputeMoveHold(World ? StanceSettleUntil - World->GetTimeSeconds() : 0.f,
		Anim ? Anim->GetStanceTransitionTimeLeft() : 0.f);
}

EOperativeOrderResult AOperativeCharacter::RequestMove(const FVector& RequestedDestination, bool bFormationMove)
{
	const FVector InDestination = UTrainingSectorSubsystem::LimitGoal(GetWorld(), RequestedDestination, GetSimpleCollisionRadius() + 45.0);
	if (VehicleQueries::IsMounted(this))
	{
		return EOperativeOrderResult::Refused; // APC P1: aboard he rides (Y gets him out)
	}
	if (bFormationMove)
	{
		bMantlePathAllowed = false; // formation moves keep today's paths (no mantle links)
		bWindowPathAllowed = false; // ... and no whole windows (rule 2: a follower goes through once it is broken)
		bMantleOrderPending = false;
	}
	// Mantling (OperativeCharacterMantle.cpp): a new order mid-climb is walked to from the landing (like the ladder / the drop).
	if (bMantling)
	{
		MantleResumeTarget = InDestination;
		bMantleResume = true;
		bMantleStopAfter = false;
		bMantleResumeOwnOrder = !bFormationMove;
		LastMoveDestination = InDestination;
		bHasMoveOrder = true;
		return EOperativeOrderResult::Accepted;
	}
	// Coming down off a mantle top (batch 2): a new order is walked to after the landing.
	if (IsMantleDescending())
	{
		MantleDescentGoal = InDestination;
		bMantleDescentResume = true;
		bMantleDescentResumeOwn = !bFormationMove;
		LastMoveDestination = InDestination;
		bHasMoveOrder = true;
		return EOperativeOrderResult::Accepted;
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
	// Drops (2026-11-03): mid-drop a new order is walked to after the landing; waiting at a drop link, the new order replaces the wait.
	if (LedgeFall && LedgeFall->IsWaiting())
	{
		LedgeFall->CancelWait();
	}
	else if (LedgeFall && LedgeFall->IsBusy())
	{
		LedgeFall->HandleNewOrder(InDestination);
		LastMoveDestination = InDestination;
		bHasMoveOrder = true;
		return EOperativeOrderResult::Accepted;
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
	bSteeringOwnMove = false; // a new path request (a bypass steers again after it)
	ClearIdleFacing();
	bExplicitFacing = false; // a new move: no ordered facing any more (ThreatFacingRules may turn him after it)
	if (IsInStealthMode())
	{
		SetFormationLookYaw(TOptional<float>());
		if (SmoothedVelocity.IsNearlyZero() || FVector::DotProduct(SmoothedVelocity.GetSafeNormal2D(), (Destination - GetActorLocation()).GetSafeNormal2D()) < 0.5f)
		{
			SmoothedVelocity = FVector::ZeroVector;
		}
	}
	AOperativeAIController* AIController = Cast<AOperativeAIController>(GetController());
	if (!AIController)
	{
		return EOperativeOrderResult::NoController;
	}
	EPathFollowingRequestResult::Type Result = AIController->MoveToLocation(
		Destination, MovementConfig.AcceptanceRadius, /*bStopOnOverlap*/ false, /*bUsePathfinding*/ true,
		/*bProjectDestinationToNavigation*/ true, /*bCanStrafe*/ false, ChooseMoveFilter());
	// Mantling: on a mantle top whose path off it does not start (no navmesh on the top) or ends short of the goal (an island, a partial
	// path: MantlingProblem_06 "stuck on top"), he comes down over the face nearest the goal and walks on from there (batch 2).
	// (The player's operatives only: the PMC's own operatives keep their paths - their AI is not touched by batch 2.)
	const AActor* MantleTop = MantleNavigation::IsBatch2Enabled() && IsPlayerTeam() && !bFormationMove && Result != EPathFollowingRequestResult::AlreadyAtGoal
		? GetMantleTopUnderFeet() : nullptr;
	if (MantleTop)
	{
		const UPathFollowingComponent* TopFollowing = AIController->GetPathFollowingComponent();
		const FNavPathSharedPtr TopPath = TopFollowing ? TopFollowing->GetPath() : nullptr;
		const bool bStuckOnTop = Result == EPathFollowingRequestResult::Failed || !TopPath.IsValid() || TopPath->IsPartial();
		if (bStuckOnTop && TryMantleStepDown(Destination))
		{
			return EOperativeOrderResult::Accepted;
		}
		// MantlingProblem_03 ("always jumps down on the side he climbed from"): the path's way down may be the climbed face's down link only;
		// a walk-off over another face that is descent.prefer_margin_cm shorter to the goal is taken instead.
		FVector Landing;
		float DescentCm = 0.f;
		if (!bStuckOnTop && TopPath.IsValid() && FindMantleDescent(*MantleTop, Destination, Landing, &DescentCm)
			&& DescentCm + MantleRules::GetConfig().DescentPreferMarginCm < TopPath->GetLength())
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Mantle] %s: the walk-off (%.0f cm) beats the path off the top (%.0f cm)"), *DisplayName.ToString(), DescentCm,
				TopPath->GetLength());
			if (StartMantleDescent(Landing, Destination, true, !bAutonomousOrder))
			{
				return EOperativeOrderResult::Accepted;
			}
		}
	}
	else if (Result == EPathFollowingRequestResult::Failed && TryMantleStepDown(Destination))
	{
		return EOperativeOrderResult::Accepted;
	}
	// The soft-lock safety net (user bug 2026-11-03): no path starts where he stands off the navmesh (every order "unreachable") - he is put on
	// the nearest navmesh point that reaches the main area and the order is asked again.
	if (Result == EPathFollowingRequestResult::Failed && LedgeFall && LedgeFall->RescueIfOffNavmesh(TEXT("an order could not start"), /*bResumeOrder*/ false))
	{
		Result = AIController->MoveToLocation(Destination, MovementConfig.AcceptanceRadius, false, true, true, false, ChooseMoveFilter());
	}
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
	// Doors R2 (designer 2026-11-22): a hostile fighter whose path stops at a LOCKED door breaches it (kick / shot / key / grenade by his
	// archetype in doors.json) instead of standing there. No-op while the doors switch is off.
	if (bHasMoveOrder && !IsPlayerTeam())
	{
		UDoorSubsystem::NotifyMoveRequested(this);
	}
	if (CVarOperativeMoveTrace.GetValueOnGameThread() != 0)
	{
		const UPathFollowingComponent* TracedFollowing = AIController->GetPathFollowingComponent();
		const FNavPathSharedPtr TracedPath = TracedFollowing ? TracedFollowing->GetPath() : nullptr;
		const bool bValidPath = TracedPath.IsValid() && TracedPath->IsValid() && TracedPath->GetPathPoints().Num() > 0;
		// fix/pmc-onpoint-3d-detour: a crowd-following move skips the string pulling (UCrowdFollowingComponent::OnPathfindingQuery), so its
		// path has 2 points (the ends) and GetLength is the straight line; the route itself is the navmesh corridor (polys, corridor length).
		const FNavMeshPath* MeshPath = bValidPath ? TracedPath->CastPath<FNavMeshPath>() : nullptr;
		UE_LOG(LogCodexTactics, Display, TEXT("[MoveTrace] %s: path to (%.0f, %.0f, z %.0f): result %d, %d points, length %.0f, partial %d, ends %.0f cm from the goal (z %.0f), corridor %d polys / %.0f cm, string-pulled %d"),
			*DisplayName.ToString(), Destination.X, Destination.Y, Destination.Z, static_cast<int32>(Result), bValidPath ? TracedPath->GetPathPoints().Num() : 0,
			bValidPath ? TracedPath->GetLength() : 0.f, bValidPath && TracedPath->IsPartial() ? 1 : 0,
			bValidPath ? FVector::Dist2D(TracedPath->GetEndLocation(), Destination) : -1.f, bValidPath ? TracedPath->GetEndLocation().Z : 0.0,
			MeshPath ? MeshPath->PathCorridor.Num() : -1, MeshPath ? MeshPath->GetTotalPathLength() : -1.0, MeshPath && MeshPath->IsStringPulled() ? 1 : 0);
	}
	return EOperativeOrderResult::Accepted;
}

TSubclassOf<UNavigationQueryFilter> AOperativeCharacter::ChooseMoveFilter() const
{
	// Low fences (ACrawlGapActor, 2026-10-13): only a prone body may path through a crawl-only area; standing / crouched the
	// path goes round (or ends at the fence as a partial path, where the fence refuses him with the "go prone" hint).
	// Known traps (rev 5e, game designer 2026-10-14): only the squad AUTO mode (Ctrl+T) paths round a spotted / own wire or mine;
	// a player's order is literal - through the wire if that is the way (TripwireRules::ChooseOperativeMoveFilter).
	if (!IsPlayerTeam())
	{
		// A PMC fighter (smart-enemies T-11): crawl rules as the player's men, walks into the squad's known traps, goes round the PMC's own charges.
		return PmcEncounterNav::FilterFor(Stance == EOperativeStance::Prone, bMantlePathAllowed);
	}
	const USquadSubsystem* MoveSquad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	TSubclassOf<UNavigationQueryFilter> Filter = nullptr;
	switch (TripwireRules::ChooseOperativeMoveFilter(Stance == EOperativeStance::Prone, MoveSquad && MoveSquad->IsAutonomousSquadCombat()))
	{
	case TripwireRules::EOperativeMoveFilter::NoCrawl: Filter = UNavFilter_NoCrawl::StaticClass(); break;
	case TripwireRules::EOperativeMoveFilter::TrapLiteral: Filter = UNavFilter_TrapLiteral::StaticClass(); break;
	case TripwireRules::EOperativeMoveFilter::NoCrawlTrapLiteral: Filter = UNavFilter_NoCrawlTrapLiteral::StaticClass(); break;
	default: break;
	}
	// The mantle links (UNavArea_Mantle) are open only while this order may climb (bMantlePathAllowed, OperativeMovementRules::IsMantlePathAllowed:
	// the player's Solo order / formation leader R17-1; a PMC commander order always).
	const TSubclassOf<UNavigationQueryFilter> Chosen = bMantlePathAllowed ? Filter : MantleNavigation::NoMantleVariant(Filter);
	// Window sills - designer rule 2026-11-22, every mode (Solo, box selection, formation): a WHOLE window is a way through only on the
	// player's window order (the silhouette click, SetWindowOrder) - then for everyone in the order, the followers' window costing nothing so
	// they follow the commander through it; a BROKEN window is an ordinary vault area (VaultNavigation::SetWindowBroken) any path may take
	// when it is faster - it makes no noise any more.
	// Rule 2 (designer 2026-11-23, WindowOrderRules): a formation follower never paths through a WHOLE window, even on the leader's window
	// order - he follows once the leader broke it (then it is an ordinary vault area).
	const USquadSubsystem* WindowSquad = MoveSquad;
	const bool bFormationFollower = WindowSquad && !WindowSquad->IsSoloMode() && WindowSquad->GetFormationLeader() != this;
	if (!WindowSquad || !WindowOrderRules::MayTakeWholeWindow(bWindowPathAllowed, bFormationFollower, WindowOrderRules::Get()))
	{
		return MantleNavigation::NoWindowVariant(Chosen, bMantlePathAllowed);
	}
	return bMantlePathAllowed ? Chosen : MantleNavigation::WindowFreeVariant(Chosen);
}

bool AOperativeCharacter::SteerOwnMoveVia(const FVector& Via)
{
	if (!HasOwnMoveOrder())
	{
		return false;
	}
	const UTutorialSubsystem* Tutorial = GetWorld() ? GetWorld()->GetSubsystem<UTutorialSubsystem>() : nullptr;
	if (Tutorial && (Tutorial->IsConfining() || Tutorial->IsHeldInPlace(*this)))
	{
		return false; // a confinement judges his own path; the bypass never bends it
	}
	AOperativeAIController* AIController = Cast<AOperativeAIController>(GetController());
	if (!AIController)
	{
		return false;
	}
	const FVector Goal = UTrainingSectorSubsystem::LimitGoal(GetWorld(), Via, GetSimpleCollisionRadius() + 45.0);
	const EPathFollowingRequestResult::Type Result = AIController->MoveToLocation(Goal, MovementConfig.AcceptanceRadius, /*bStopOnOverlap*/ false,
		/*bUsePathfinding*/ true, /*bProjectDestinationToNavigation*/ true, /*bCanStrafe*/ false, ChooseMoveFilter(), /*bAllowPartialPath*/ false);
	if (Result != EPathFollowingRequestResult::RequestSuccessful)
	{
		// The old request was replaced by the failed one: the order goes on to its own destination.
		bSteeringOwnMove = false;
		RequestMove(LastMoveDestination);
		return false;
	}
	bSteeringOwnMove = true;
	return true;
}

bool AOperativeCharacter::ResumeOwnMove()
{
	if (!bSteeringOwnMove)
	{
		return false;
	}
	bSteeringOwnMove = false;
	if (!HasOwnMoveOrder())
	{
		return false;
	}
	const EOperativeOrderResult Result = RequestMove(LastMoveDestination);
	bOwnMoveOrder = bHasMoveOrder;
	return Result == EOperativeOrderResult::Accepted;
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
	bSteeringOwnMove = false;
	bStealthFast = false;
	bSprinting = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PendingMoveTimer);
	}
	ApplyMovementParams();
}

void AOperativeCharacter::SetSprinting(bool bNewSprinting)
{
	if (bNewSprinting && IsInStealthMode() && !bCarrying && !IsCriticallyWounded()
		&& OperativeMovementRules::CanSprint(MovementConfig, EOperativeStance::Standing, ColdLevel, IsWounded()))
	{
		SetStealthMode(false);
		SetStance(EOperativeStance::Standing);
	}
	const bool bAllowed = bNewSprinting && CanSprint() && Stance == EOperativeStance::Standing;
	if (LookHoldRules::ShouldClearOnSprint(bAllowed) && HasManualLookPoint())
	{
		ClearManualLookPoint();
		UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("LOOK CLEARED"), FLinearColor(0.75f, 0.85f, 0.95f));
	}
	if (bSprinting != bAllowed)
	{
		bSprinting = bAllowed;
		ApplyMovementParams();
	}
}

bool AOperativeCharacter::RetryBlockedOwnMove()
{
	if (BlockedRetrySerial != MoveOrderSerial)
	{
		BlockedRetrySerial = MoveOrderSerial;
		BlockedRetryCount = 0;
	}
	// Not mid cover wrap / bypass steering / cover walk (their own ends handle a stop), only a plain own walk.
	// Never while dead / in turn-based combat (AP moves are the player's, 047-style review) (give-way v2 lead review).
	const UTurnBasedCombatSubsystem* TurnBased = GetWorld() ? GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>() : nullptr;
	if (bCoverWrapActive || bSteeringOwnMove || bHasPendingCover || bMantling || IsMantleDescending()
		|| (HealthComponent && !HealthComponent->IsAlive()) || (TurnBased && TurnBased->IsActive()))
	{
		return false;
	}
	const float ToGoalCm = static_cast<float>(FVector::Dist2D(GetActorLocation(), LastMoveDestination));
	if (!SquadGiveWayRules::ShouldRetryBlockedMove(HasOwnMoveOrder(), ToGoalCm, BlockedRetryCount, SquadGiveWayTuning::Get()))
	{
		return false;
	}
	++BlockedRetryCount;
	const EOperativeOrderResult Result = RequestMove(LastMoveDestination, /*bFormationMove*/ false);
	BlockedRetrySerial = MoveOrderSerial; // RequestMove bumps the serial: keep counting this order's retries (cap 4, review finding 1)
	UE_LOG(LogCodexTactics, Display, TEXT("[GiveWay] %s: his walk to (%.0f, %.0f) was ended as blocked %.0f cm from it - asked again (%d / %d) -> %d"),
		*DisplayName.ToString(), LastMoveDestination.X, LastMoveDestination.Y, ToGoalCm, BlockedRetryCount, SquadGiveWayTuning::Get().BlockedRetries,
		static_cast<int32>(Result));
	return Result == EOperativeOrderResult::Accepted; // an accepted re-request is never treated as an arrival
}

void AOperativeCharacter::HandleMoveFinished()
{
	// The far-side leg aborts the near-side path. That completion is not an arrival.
	if (bWindowApproachRelease)
	{
		return;
	}
	// During a wrap round the cover object (2026-10-20) he steers himself: a path following's end (an earlier move stopped) is no arrival.
	if (ContinueCoverWrapOnArrival())
	{
		return;
	}
	// G024: the via to his own side of the ordered window ended. The formation goal is already the far landing. Open the
	// window and climb in from this face; do not treat the via as an arrival. Before the generic bypass resume, which
	// would walk on with whole windows still closed.
	if (bWindowApproachPending)
	{
		const FWindowOrderTuning& WindowTuning = WindowOrderRules::Get();
		const float Off = FVector::Dist2D(GetActorLocation(), WindowApproachPoint);
		const bool bAtFace = Off <= WindowTuning.ApproachArriveCm;
		const bool bRun = bSprinting;
		const FVector Landing = WindowApproachLanding;
		const FVector Dir = (Landing - GetActorLocation()).GetSafeNormal2D();
		bWindowApproachPending = false;
		bWindowPathAllowed = bWindowApproachWindows;
		if (!bAtFace)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("%s: window approach %.0f cm short, walking to the far side"), *DisplayName.ToString(), Off);
		}
		bWindowApproachRelease = true;
		if (!bSteeringOwnMove || !ResumeOwnMove())
		{
			OrderMoveTo(Landing, bRun);
		}
		const bool bDeferred = GetWorld() && GetWorld()->GetTimerManager().IsTimerActive(PendingMoveTimer);
		if (bAtFace && !bDeferred && !Dir.IsNearlyZero())
		{
			TryVault(Dir, /*bForceWhenBlocked*/ true);
		}
		bWindowApproachRelease = false;
		return;
	}
	// Formation rules v2.1 bypass: the end of the walk to a waypoint round a wall is no arrival - the order goes on to its destination.
	if (bSteeringOwnMove && ResumeOwnMove())
	{
		return;
	}
	bSteeringOwnMove = false;
	bHasMoveOrder = false;
	bOwnMoveOrder = false;
	bStealthFast = false;
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
		bRunCornerPeekPending = false;
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
			Messages->PostMessage(DisplayName, FText::FromString(Callouts[CodexRandom::RandRange(ECodexRandomStream::Cosmetic, this, 0, 3)]));
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
	OrderedFacingYaw = GetActorRotation().Yaw; // he returns to it after the fight (designer 2026-11-17)
	FightTurnQuietSeconds = 0.f;
}

void AOperativeCharacter::SetCarrying(bool bNewCarrying)
{
	bCarrying = bNewCarrying;
	if (bCarrying)
	{
		bStealthFast = false;
		bSprinting = false; // Godot can_sprint: no sprint while carrying
		ClearManualLookPoint();
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
	return !bCarrying && OperativeMovementRules::CanSprint(MovementConfig, Stance, ColdLevel, IsWounded())
		&& CriticalWoundRules::AllowsSprint(IsCriticallyWounded()); // the critical wound: down, crawling only
}

float AOperativeCharacter::GetMaxSpeed() const
{
	if (IsStealthFast() && CanStealthFast())
	{
		return FMath::Max(MovementConfig.CrouchSpeed, StealthModeRules::GetConfig().FastSpeedCm) * ColdSpeedMultiplier * GetWoundTierSpeedScale();
	}
	// The heavy wound tier walks slower than the light one (feat/injured-tiers-fullbody: Walk_Injured_Slow; standing only).
	return OperativeMovementRules::ComputeMaxSpeed(MovementConfig, Stance, bSprinting, IsWounded(), bCarrying) * ColdSpeedMultiplier * GetWoundTierSpeedScale();
}

float AOperativeCharacter::GetAllowedWalkSpeed() const
{
	return OperativeMovementRules::ComputeMaxSpeed(MovementConfig, Stance, /*bSprinting*/ false, IsWounded(), bCarrying) * ColdSpeedMultiplier * GetWoundTierSpeedScale();
}

void AOperativeCharacter::HandleHealthChanged(float NewHealth, float MaxHealth, float Delta)
{
	if (bSprinting && !CanSprint())
	{
		bSprinting = false;
	}
	if (bStealthFast && (IsWounded() || IsCriticallyWounded()))
	{
		bStealthFast = false;
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

void AOperativeCharacter::SteerFormationMove(const FVector2D& Direction, float Speed)
{
	if (bHasMoveOrder)
	{
		if (AController* OwnerController = GetController())
		{
			OwnerController->StopMovement(); // the path move ends here; the steering carries his speed on (no StopMovementImmediately)
		}
		bHasMoveOrder = false;
		bOwnMoveOrder = false;
		bSteeringOwnMove = false;
	}
	ApplyMovementParams(Speed);
	AddMovementInput(FVector(Direction.X, Direction.Y, 0.f), 1.f);
}

bool AOperativeCharacter::IsMoving() const
{
	return bHasMoveOrder || GetVelocity().Size2D() > MovingSpeedThreshold;
}

void AOperativeCharacter::ApplyMovementParams(float SpeedOverride)
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	// Formation patrol pace (Codex.Formation.Sectors): the walk of the leader in a patrol formation, never the sprint nor a formation move's
	// own speed (FormationPaceMultiplier stays 1 with the console off).
	Movement->MaxWalkSpeed = SpeedOverride >= 0.f ? SpeedOverride : GetMaxSpeed() * (bSprinting ? 1.f : FormationPaceMultiplier);
	Movement->RotationRate = FRotator(0.f, OperativeMovementRules::GetTurnRate(MovementConfig, Stance), 0.f);
	Movement->GetNavMovementProperties()->FixedPathBrakingDistance = MovementConfig.PathBrakingDistance; // FollowTo may shorten it for one move
}

void AOperativeCharacter::SetFormationPaceMultiplier(float Multiplier)
{
	if (FMath::IsNearlyEqual(FormationPaceMultiplier, Multiplier))
	{
		return;
	}
	FormationPaceMultiplier = Multiplier;
	// A formation move (FollowTo) runs at its own speed: only his own walk (or standing) takes the new pace now.
	if (!bHasMoveOrder || bOwnMoveOrder)
	{
		ApplyMovementParams();
	}
}

EOperativeOrderResult AOperativeCharacter::AutonomousMoveTo(const FVector& Destination)
{
	TGuardValue<bool> Autonomous(bAutonomousOrder, true);
	return OrderMoveTo(Destination, false);
}
