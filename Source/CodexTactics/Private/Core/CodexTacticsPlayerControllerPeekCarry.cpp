// ACodexTacticsPlayerController: the squad's corner peek moves on to the NEXT wall (the game designer's play-test 2026-11-01 on
// L_FormationLabCombat, bug_04 A; behind Codex.Formation.Sectors + Codex.Formation.PeekCarry). The squad peeking round one wall's end, the
// double-click on the parallel wall's end ("corner to fire from") is a group cover order = the formation wall layout (OrderGroupCover ->
// USquadSubsystem::OrderFormationWall, which now takes every member out of his cover / corner first); StartSquadPeekCarry remembers that
// corner, TickSquadPeekCarry orders the squad's corner peek there once the squad stands at the new wall - the same order as the player's
// click on that end (FindSquadCornerPeekForHit + USquadSubsystem::OrderFormationCornerPeek). Pure rules: FormationPeekCarry.
// Godot reference: none (UE-only; docs/systems/squad-and-orders.md "The squad's corner peek moves on to the next wall").

#include "Core/CodexTacticsPlayerController.h"
#include "Core/CodexTacticsPlayerControllerInternal.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Engine/World.h"
#include "Combat/CombatTimeModeRules.h"
#include "Formation/FormationPeekCarryRules.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Tactics/RunCornerPeekRules.h"
#include "UI/GameMessageSubsystem.h"

#define LOCTEXT_NAMESPACE "CodexTacticsPlayerController"

namespace ControllerPeekCarryLocal
{
	TAutoConsoleVariable<int32> CVarFormationPeekCarry(TEXT("Codex.Formation.PeekCarry"), 1,
		TEXT("Bug_04 A (game designer 2026-11-01): 1 = a group wall order at another wall's corner given while the squad peeks carries the peek over - at the new wall the squad's corner peek round that corner; 0 = the squad only hugs the new wall (the old rule, A/B)"));

	/** The leader counts as walking above this planar speed, cm/s. */
	constexpr float LeaderMovingSpeed = 20.f;
	/** The aim trace starts this far out from the face and ends this far behind it, cm. */
	constexpr float AimTraceOutCm = 120.f;
	constexpr float AimTraceInCm = 30.f;
}

void ACodexTacticsPlayerController::StartSquadPeekCarry(USquadSubsystem& Squad, AOperativeCharacter& Leader, const FCoverSlot& Slot, bool bPeekInForce, bool bRunPeek)
{
	using namespace ControllerPeekCarryLocal;
	PeekCarry = FSquadPeekCarry(); // any new wall order replaces a carry still waiting
	FormationPeekCarry::FCarryOrderFacts Facts;
	Facts.bPeekInForce = bPeekInForce;
	Facts.bSlotAtCorner = Slot.HasExposedEdge();
	Facts.bWallLayoutOrder = Squad.HasFormationWallOrder();
	Facts.bPlanned = false;
	if (CVarFormationPeekCarry.GetValueOnGameThread() == 0 || !FormationPeekCarry::ShouldCarry(Facts))
	{
		if (bPeekInForce)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Formation] the squad's corner peek is not carried to the new wall (%s)"),
				CVarFormationPeekCarry.GetValueOnGameThread() == 0 ? TEXT("Codex.Formation.PeekCarry 0") : TEXT("the clicked slot has no free corner"));
		}
		return;
	}
	ECoverFacing Side = ECoverFacing::Right;
	float EdgeAlong = 0.f;
	if (!FormationPeekCarry::PickCorner(Slot.bLeftEdgeExposed, Slot.LeftEdgeDistanceCm, Slot.bRightEdgeExposed, Slot.RightEdgeDistanceCm, Side, EdgeAlong))
	{
		return;
	}
	const FVector Right = Slot.RightTangent();
	PeekCarry.bActive = true;
	PeekCarry.WallActor = Slot.WallActor;
	PeekCarry.WallComponent = Slot.WallComponent;
	PeekCarry.AimPoint = Slot.WallPoint + Right * FormationPeekCarry::AimAlong(EdgeAlong);
	PeekCarry.WallNormal = FVector(Slot.WallNormal.X, Slot.WallNormal.Y, 0.f).GetSafeNormal();
	PeekCarry.Leader = &Leader;
	PeekCarry.LeaderSerial = Leader.GetMoveOrderSerial();
	PeekCarry.bRunPeek = bRunPeek;
	const FVector Corner = Slot.WallPoint + Right * EdgeAlong;
	UE_LOG(LogCodexTactics, Display, TEXT("[Formation] the squad's corner peek moves on: the next wall's %s corner at (%.0f, %.0f) - the peek there once %s"),
		Side == ECoverFacing::Left ? TEXT("left") : TEXT("right"), Corner.X, Corner.Y,
		bRunPeek ? TEXT("the leader is at that wall (run-and-peek, the game designer's answer Q2 2026-11-08)") : TEXT("the squad is at that wall"));
}

bool ACodexTacticsPlayerController::IsPausePlanningOrders() const
{
	const UGameFlowSubsystem* Flow = GetWorld() ? GetWorld()->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	return Flow && FCombatTimeModeRules::GetOrderDispatch(Flow->GetPhase(), Flow->GetCombatMode(), OrderLock::CVarRealTimeOrders.GetValueOnGameThread() != 0)
		== ECombatOrderDispatch::Queue;
}

void ACodexTacticsPlayerController::TickSquadPeekCarry(float DeltaSeconds)
{
	using namespace ControllerPeekCarryLocal;
	// #55 Q3 (2026-11-08): the formation run-and-peek planned in the tactical pause - once the pause is over its planned wall order runs
	// (USquadSubsystem::ExecutePlannedFormationWall) and the carried peek starts from there (fires on the leader's arrival, Q2).
	if (PlannedRunPeekCarry.bActive && !IsPausePlanningOrders())
	{
		const FPlannedRunPeekCarry Planned = PlannedRunPeekCarry;
		PlannedRunPeekCarry = FPlannedRunPeekCarry();
		USquadSubsystem* PlanSquad = GetSquad();
		AOperativeCharacter* PlanLeader = Planned.Leader.Get();
		if (PlanSquad && PlanLeader && PlanSquad->GetLeader() == PlanLeader && PlanSquad->HasFormationWallOrder())
		{
			StartSquadPeekCarry(*PlanSquad, *PlanLeader, Planned.Slot, true, true);
		}
		else
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Formation] the planned run-and-peek is dropped on the pause's release (no wall order of %s)"),
				PlanLeader ? *PlanLeader->DisplayName.ToString() : TEXT("-"));
		}
	}
	if (!PeekCarry.bActive)
	{
		return;
	}
	USquadSubsystem* Squad = GetSquad();
	AOperativeCharacter* Leader = PeekCarry.Leader.Get();
	PeekCarry.Seconds += DeltaSeconds;
	if (Leader && Leader->GetVelocity().Size2D() > LeaderMovingSpeed)
	{
		PeekCarry.bLeaderMoved = true;
		PeekCarry.LeaderStillSeconds = 0.f;
	}
	else
	{
		PeekCarry.LeaderStillSeconds += DeltaSeconds;
	}
	FormationPeekCarry::FCarryProgressFacts Facts;
	Facts.bSameLeader = Leader && Squad && Squad->GetLeader() == Leader;
	Facts.bLeaderOrderUnchanged = Leader && Leader->GetMoveOrderSerial() == PeekCarry.LeaderSerial;
	Facts.bWallOrder = Squad && Squad->HasFormationWallOrder();
	Facts.bApproachDone = Squad && Squad->IsFormationWallApproachDone() && Squad->AreFormationWallMembersSettled();
	Facts.bLeaderMoved = PeekCarry.bLeaderMoved;
	Facts.LeaderStillSeconds = PeekCarry.LeaderStillSeconds;
	Facts.Seconds = PeekCarry.Seconds;
	// #55 Q2 (2026-11-08): a formation run-and-peek fires once the LEADER is at the wall (his wall slot), the others keep forming up behind.
	bool bLeaderAtWall = false;
	FVector LeaderSlot = FVector::ZeroVector;
	float LeaderSlotYaw = 0.f;
	if (PeekCarry.bRunPeek && Leader && Squad && Squad->GetFormationTarget(Leader, LeaderSlot, LeaderSlotYaw))
	{
		bLeaderAtWall = FVector::Dist2D(Leader->GetActorLocation(), LeaderSlot) <= RunCornerPeekRules::LeaderAtWallCm;
	}
	const FormationPeekCarry::EPeekCarryStep Step = RunCornerPeekRules::DecideFormationCarry(Facts, PeekCarry.bRunPeek, bLeaderAtWall);
	if (Step == FormationPeekCarry::EPeekCarryStep::Wait)
	{
		return;
	}
	PeekCarry.bActive = false;
	if (Step == FormationPeekCarry::EPeekCarryStep::Drop)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Formation] the carried corner peek is dropped after %.1f s (%s)"), PeekCarry.Seconds,
			!Facts.bSameLeader ? TEXT("the leader changed") : !Facts.bLeaderOrderUnchanged ? TEXT("the leader's next order")
			: !Facts.bWallOrder ? TEXT("the wall order is over") : TEXT("the squad never got there"));
		return;
	}
	// The player's click on that wall's end: a cursor-like hit on its face (a real trace; the remembered wall when the trace misses it).
	AActor* WallActor = PeekCarry.WallActor.Get();
	UPrimitiveComponent* WallComponent = PeekCarry.WallComponent.Get();
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SquadPeekCarryTrace), false);
	for (AOperativeCharacter* Member : Squad->GetMembers())
	{
		Params.AddIgnoredActor(Member);
	}
	const bool bTraced = GetWorld()->LineTraceSingleByChannel(Hit, PeekCarry.AimPoint + PeekCarry.WallNormal * AimTraceOutCm,
		PeekCarry.AimPoint - PeekCarry.WallNormal * AimTraceInCm, ECC_Visibility, Params)
		&& (WallComponent ? Hit.GetComponent() == WallComponent : Hit.GetActor() == WallActor);
	if (!bTraced)
	{
		Hit = FHitResult(WallActor, WallComponent, PeekCarry.AimPoint, PeekCarry.WallNormal);
	}
	FSquadCornerPeekTarget Target;
	AOperativeCharacter* Peeker = nullptr;
	const bool bOrdered = FindSquadCornerPeekForHit(Hit, Target) && (Peeker = Target.Peeker.Get()) != nullptr
		&& Squad->OrderFormationCornerPeek(Peeker, Target.CornerSlot, Target.EdgeOnFace);
	UE_LOG(LogCodexTactics, Display, TEXT("[Formation] the carried corner peek after %.1f s (everyone at the wall %d, the leader at the wall %d, the leader still %.1f s%s): %s"),
		PeekCarry.Seconds, Facts.bApproachDone ? 1 : 0, bLeaderAtWall ? 1 : 0, Facts.LeaderStillSeconds, PeekCarry.bRunPeek ? TEXT(", run-and-peek") : TEXT(""), bOrdered ? *FString::Printf(TEXT("%s takes the corner at (%.0f, %.0f)"), *Peeker->DisplayName.ToString(),
			Target.EdgeOnFace.X, Target.EdgeOnFace.Y) : TEXT("no squad peek at that corner (the squad is not at that wall's end)"));
	if (!bOrdered)
	{
		return;
	}
	++PeekCarryFired;
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(Peeker->DisplayName, LOCTEXT("SquadCornerPeekCarried", "🎯 Covering the corner - the squad stacks up behind me."));
	}
}

#undef LOCTEXT_NAMESPACE
