// ACodexTacticsPlayerController: cover clicks - cover ghost (find slot, show / hide / confirm), corner peek hover,
// cover fire mode key, the wall-edge cover switch (double-click on another cover from a wall edge, 2026-10-20).
// Split out of CodexTacticsPlayerController.cpp verbatim (cleanup 2026-10-15; map: docs/port/REFACTOR_CONTROLLER_2026-10.md).
// Godot reference: Scenes/movements/main.gd (_input and the click / key handlers it dispatches to).

#include "Core/CodexTacticsPlayerController.h"
#include "Core/CodexTacticsPlayerControllerInternal.h"
#include "Combat/EnemyGhostActor.h"
#include "Combat/CombatTimeModeRules.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Core/PlayerCommands.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "Tutorial/TutorialSubsystem.h"
#include "Tactics/CoverFacingRules.h"
#include "Tactics/CoverGhostActor.h"
#include "Tactics/CoverRules.h"
#include "Tactics/CoverSwitchRules.h"
#include "Tactics/CoverTraceRules.h"
#include "Tactics/GroupCoverRules.h"
#include "Tactics/RunCornerPeekRules.h"
#include "Data/SquadWallTuning.h"
#include "Formation/FormationRuntimeRules.h"
#include "Formation/FormationWallLeadershipRules.h"
#include "Tactics/CoverWrapRules.h"
#include "Tactics/WallSideRules.h"
#include "NavigationPath.h"
#include "Characters/SquadOrderRules.h"
#include "Interactables/InteractionSubsystem.h"
#include "CodexTactics.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"
#include "Tactics/GorkyGridManager.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Interactables/InteractableActor.h"
#include "UI/GameMessageSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "Vehicles/VehiclePawn.h"
#include "Vehicles/VehicleSubsystem.h"

#include "HAL/IConsoleManager.h"

#define LOCTEXT_NAMESPACE "CodexTacticsPlayerController"

namespace
{
	TAutoConsoleVariable<int32> CVarGroupCoverLeaderPriority(TEXT("Codex.GroupCover.LeaderPriority"), 1,
		TEXT("1 (default): in a group cover order the others wait ~1.6 m off the wall until the leader has taken his cover (game designer 2026-10-23); 0: the old rule (everybody runs straight into his slot)."));
	TAutoConsoleVariable<int32> CVarGroupCoverNearbyCovers(TEXT("Codex.GroupCover.NearbyCovers"), 1,
		TEXT("1 (default; game designer bug report 2026-11-02 item 3): in a group cover order a member with no slot at the leader's wall takes a free cover of another object within squad_walls.json follower_cover_radius_m facing the same side, prone behind the leader's cover only when none fits; 0: prone behind it at once (before 2026-11-03)."));

	TAutoConsoleVariable<int32> CVarGroupCornerPeek(TEXT("Codex.Squad.GroupCornerPeek"), 1,
		TEXT("1 (default; the game designer's bug 2026-11-03): a corner peek ordered for a box selection in cover along one wall - the selected member nearest that end takes the corner, the others side-step into a stack behind him (the formation's wall-leadership rule); 0: the selection's leader alone peeks (before)."));
	/** A box-selected member within this of his place in the group's corner stack stays where he is (no side-step), cm. */
	constexpr double GroupCornerPeekKeepCm = 30.0;

}

namespace ControllerGroupCover
{
	/** Knee height of the nearby cover traces above the slot's feet (finds 60 cm covers, as CoverTraceRules' knee trace), cm. */
	constexpr float NearbyCoverKneeCm = 45.f;
	/** The nearby cover traces start this far out from the leader's slot line (two rows), every NearbyCoverStepCm along it, cm. */
	constexpr float NearbyCoverRowsCm[] = { 250.f, 50.f };
	constexpr float NearbyCoverStepCm = 50.f;
	/** A nearby cover slot stays on the leader's level: within this height of his slot, cm. */
	constexpr float NearbyCoverMaxRiseCm = 150.f;

	/**
	 * Item 3 (2026-11-03): free cover slots of other objects round the leader's slot for the members who got no slot at his wall - traces
	 * from the squad's side towards the threat side (along -WallNormal) on two rows across RadiusCm either way along his wall; a face that
	 * looks the same way (GroupCoverRules::IsNearbyCoverUsable) gives a slot (CoverTraceRules::FindCoverSlotAt); slots of the leader's own
	 * wall are the probes' (skipped); up to Count, nearest first, one man per spot (GroupCoverRules::ChooseNearbyCovers).
	 */
	TArray<FCoverSlot> FindNearbyGroupCoverSlots(UWorld* World, const FCoverSlot& LeaderSlot, const TArray<FCoverSlot>& Claimed, int32 Count, float RadiusCm,
		float SideDeg, TFunctionRef<bool(const FVector&)> IsTaken)
	{
		TArray<FCoverSlot> Result;
		if (!World || Count <= 0 || RadiusCm <= 0.f)
		{
			return Result;
		}
		const FVector Normal = LeaderSlot.WallNormal.GetSafeNormal2D();
		const FVector Tangent = LeaderSlot.RightTangent();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(GroupCoverNearby), false);
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
		const FVector Base = LeaderSlot.WorldLocation + FVector(0.f, 0.f, NearbyCoverKneeCm);
		const FVector2D Leader2D(LeaderSlot.WorldLocation);
		TArray<FCoverSlot> Found;
		TArray<FVector2D> Points;
		for (const float Row : NearbyCoverRowsCm)
		{
			for (float Along = -RadiusCm; Along <= RadiusCm + 1.f; Along += NearbyCoverStepCm)
			{
				const FVector Start = Base + Tangent * Along + Normal * Row;
				FHitResult Hit;
				if (!World->LineTraceSingleByChannel(Hit, Start, Start - Normal * (RadiusCm + Row), ECC_Visibility, Params) || FMath::Abs(Hit.ImpactNormal.Z) > 0.3f)
				{
					continue;
				}
				const FVector HitNormal = FVector(Hit.ImpactNormal.X, Hit.ImpactNormal.Y, 0.f).GetSafeNormal();
				FCoverSlot Slot;
				if (HitNormal.IsNearlyZero() || !CoverTraceRules::FindCoverSlotAt(World, Hit.ImpactPoint, -HitNormal, Slot) || !Slot.IsValid()
					|| CoverTraceRules::IsSameWall(Slot, LeaderSlot) || FMath::Abs(Slot.WorldLocation.Z - LeaderSlot.WorldLocation.Z) > NearbyCoverMaxRiseCm)
				{
					continue;
				}
				const FVector2D Point(Slot.WorldLocation);
				if (Points.ContainsByPredicate([&Point](const FVector2D& Each) { return FVector2D::Distance(Each, Point) < 1.0; })
					|| !GroupCoverRules::IsNearbyCoverUsable(Leader2D, FVector2D(Normal), Point, FVector2D(Slot.WallNormal), IsTaken(Slot.WorldLocation), RadiusCm, SideDeg))
				{
					continue;
				}
				Found.Add(Slot);
				Points.Add(Point);
			}
		}
		TArray<FVector2D> ClaimedPoints;
		for (const FCoverSlot& Each : Claimed)
		{
			ClaimedPoints.Add(FVector2D(Each.WorldLocation));
		}
		const FGroupCoverConfig Config;
		for (const int32 Index : GroupCoverRules::ChooseNearbyCovers(Points, Leader2D, ClaimedPoints, Count, Config.SlotSpacingCm))
		{
			Result.Add(Found[Index]);
		}
		return Result;
	}
}

// --- Tactical cover (Sprint 12) ---------------------------------------------------------------------------------

bool ACodexTacticsPlayerController::HasCoverPreview() const
{
	return CoverGhost && CoverGhost->IsShown() && CoverPreviewOperative.IsValid();
}

bool ACodexTacticsPlayerController::FindCoverSlotForHit(const FHitResult& Hit, FCoverSlot& OutSlot) const
{
	OutSlot = FCoverSlot();
	AActor* HitActor = Hit.GetActor();
	// APC P1: a click anywhere on the vehicle (roof included) probes the hull side the player looks at.
	if (const AVehiclePawn* Vehicle = Cast<AVehiclePawn>(HitActor))
	{
		const FVector View = PlayerCameraManager ? PlayerCameraManager->GetCameraRotation().Vector() : FVector::ForwardVector;
		FVector Probe;
		FVector Normal;
		return Vehicle->GetCoverProbe(Hit.ImpactPoint, View, Probe, Normal) && CoverTraceRules::FindCoverSlotAt(GetWorld(), Probe, -Normal, OutSlot);
	}
	if (!HitActor || HitActor->IsA<APawn>() || HitActor->IsA<AEnemyGhostActor>() || HitActor->IsA<ACoverGhostActor>()
		|| HitActor->ActorHasTag(FName(TEXT("Enemy"))))
	{
		return false;
	}
	// Along the camera's view (planar), so the chest trace starts in front of the wall the player sees.
	FVector Direction = PlayerCameraManager ? PlayerCameraManager->GetCameraRotation().Vector() : FVector::ForwardVector;
	Direction.Z = 0.f;
	if (!Direction.Normalize())
	{
		Direction = -FVector(Hit.ImpactNormal.X, Hit.ImpactNormal.Y, 0.f).GetSafeNormal();
	}
	// The clicked surface itself decides the side: trace into it from where the player looks.
	if (FVector::DotProduct(Direction, FVector(Hit.ImpactNormal.X, Hit.ImpactNormal.Y, 0.f)) > 0.f && !FVector(Hit.ImpactNormal.X, Hit.ImpactNormal.Y, 0.f).IsNearlyZero())
	{
		Direction = -FVector(Hit.ImpactNormal.X, Hit.ImpactNormal.Y, 0.f).GetSafeNormal();
	}
	return CoverTraceRules::FindCoverSlotAt(GetWorld(), Hit.ImpactPoint, Direction, OutSlot);
}

bool ACodexTacticsPlayerController::TryHandleCoverClick(const FHitResult& Hit, UTurnBasedCombatSubsystem* TurnBased)
{
	USquadSubsystem* Squad = GetSquad();
	AOperativeCharacter* Operative = TurnBased ? TurnBased->GetActiveUnit() : (Squad ? Squad->GetLeader() : nullptr);
	AActor* HitActor = Hit.GetActor();
	if (!Operative || !HitActor || IsRealTimeOrderLocked() || bRelocateSelectMode)
	{
		return false;
	}
	// Cover not taught yet (tutorial lock): a click at a wall stays a plain ground order, silently (no "locked" line for a walk).
	if (const UTutorialSubsystem* Tutorial = GetWorld()->GetSubsystem<UTutorialSubsystem>(); Tutorial && Tutorial->IsCommandLocked(PlayerCommands::CoverPreview))
	{
		return false;
	}
	const bool bAlt = IsSquadWideModifierDown();
	// APC P1: the vehicle is a Pawn but its hull is cover (the click on the APC stays "take cover", user decision 2026-10-12).
	const bool bVehicle = HitActor->IsA<AVehiclePawn>();
	// Wall leadership (game designer 2026-11-01): the leader holding the corner of the squad's own peek - a click on its wall's end is the
	// squad's corner peek again (the man nearest that corner takes it and the lead), not his single peek.
	const bool bLeadershipPeeker = !TurnBased && Squad && Squad->IsWallLeadershipPeeker(Operative);
	// Manual corner peek (user request 2026-10-13, rev 5c; low cover too since rev 5e): a click on the END of his own wall (its last ~75 cm,
	// its end face or its top there) sends him to that corner, leaning out in the corner-aim pose until another order.
	// Rev 5d (user bug 2026-10-14): decided BEFORE the "a click near an operative picks him" proximity rule - the corner he
	// already stands at lies inside SelectRadius, so the near-edge click selected him and nothing happened (the hover, which
	// has no such rule, showed the peek ghost). A click on a body (the hit actor is a pawn) never reaches this.
	{
		ECoverFacing PeekSide = ECoverFacing::Right;
		float PeekEdge = 0.f;
		const UTutorialSubsystem* PeekTutorial = GetWorld()->GetSubsystem<UTutorialSubsystem>();
		const bool bPeekEdge = !bAlt && !bLeadershipPeeker && Operative->IsInPeekableCover() && !(PeekTutorial && PeekTutorial->IsCommandLocked(PlayerCommands::CoverCornerPeek))
			&& !HitActor->IsA<APawn>() && FindCornerPeekForHit(Operative, Hit, PeekSide, PeekEdge);
		if (CoverFacingRules::CornerPeekClickWins(bPeekEdge, HitActor->IsA<AOperativeCharacter>()))
		{
			HideCoverPreview();
			UpdateCornerPeekHover(Operative, nullptr);
			if (Squad)
			{
				Squad->HandOverLeadForOrder(Operative, TEXT("a corner peek order")); // bug 2026-11-08: the next order after a switch mid-move
			}
			// The game designer's bug 2026-11-03: a box selection in cover along that wall - the member nearest the end takes the corner, the
			// others side-step into a stack behind him (not the selection's leader alone, round the man between him and the corner).
			AOperativeCharacter* GroupPeeker = nullptr;
			ECoverFacing GroupSide = PeekSide;
			float GroupEdge = PeekEdge;
			TArray<TPair<AOperativeCharacter*, FCoverSlot>> GroupStack;
			const bool bGroupPeek = !TurnBased && PlanGroupCornerPeek(Operative, Hit, GroupPeeker, GroupSide, GroupEdge, &GroupStack) && GroupPeeker;
			AOperativeCharacter* PeekOperative = bGroupPeek ? GroupPeeker : Operative;
			const EOperativeOrderResult Result = PeekOperative->OrderCornerPeek(bGroupPeek ? GroupSide : PeekSide, bGroupPeek ? GroupEdge : PeekEdge);
			if (bGroupPeek && Result == EOperativeOrderResult::Accepted)
			{
				FString Line;
				for (const TPair<AOperativeCharacter*, FCoverSlot>& Each : GroupStack)
				{
					const EOperativeOrderResult Step = Each.Key->OrderShimmyTo(Each.Value);
					Line += FString::Printf(TEXT(" %s -> (%.0f, %.0f) %d;"), *Each.Key->DisplayName.ToString(), Each.Value.WorldLocation.X, Each.Value.WorldLocation.Y,
						static_cast<int32>(Step));
				}
				UE_LOG(LogCodexTactics, Display, TEXT("[Cover] group corner peek: %s (nearest the %s end) takes the corner, the selection's leader %s; the stack behind him:%s"),
					*PeekOperative->DisplayName.ToString(), GroupSide == ECoverFacing::Left ? TEXT("left") : TEXT("right"), *Operative->DisplayName.ToString(),
					Line.IsEmpty() ? TEXT(" everyone in place") : *Line);
			}
			UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>();
			if (Result == EOperativeOrderResult::Accepted)
			{
				ControllerCommands::Report(this, PlayerCommands::CoverCornerPeek, nullptr, false, bGroupPeek, PeekOperative, Hit.ImpactPoint);
				if (Messages)
				{
					Messages->PostMessage(PeekOperative->DisplayName, bGroupPeek ? LOCTEXT("GroupCornerPeek", "🎯 Covering the corner - the others stack up behind me.")
						: LOCTEXT("CornerPeek", "🎯 Covering the corner."));
				}
			}
			else if (Messages)
			{
				Messages->PostMessage(LOCTEXT("HQ", "HQ"), TurnBased ? LOCTEXT("CornerPeekTurnBased", "⚠️ Turn-based: move to the corner first (the peek itself costs no AP).")
					: LOCTEXT("CornerPeekRefused", "⚠️ Can't get to that corner."));
			}
			return true;
		}
	}
	// The squad's corner peek (user bug report 2026-10-26): the squad in the formation wall layout stands in no cover slot, so the peek above
	// never applied to it - a click on the END of the squad's wall (the window the single peek uses) sends the member nearest that end to peek
	// round it, the others stack behind him (USquadSubsystem::OrderFormationCornerPeek). Before the proximity pick, as the single peek.
	if (!TurnBased && !bAlt && (!Operative->bInCover || bLeadershipPeeker) && !HitActor->IsA<APawn>())
	{
		const UTutorialSubsystem* SquadPeekTutorial = GetWorld()->GetSubsystem<UTutorialSubsystem>();
		FSquadCornerPeekTarget Target;
		if (!(SquadPeekTutorial && SquadPeekTutorial->IsCommandLocked(PlayerCommands::CoverCornerPeek)) && FindSquadCornerPeekForHit(Hit, Target))
		{
			HideCoverPreview();
			UpdateCornerPeekHover(Operative, nullptr);
			AOperativeCharacter* Peeker = Target.Peeker.Get();
			const bool bOrdered = Squad && Squad->OrderFormationCornerPeek(Peeker, Target.CornerSlot, Target.EdgeOnFace);
			UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>();
			if (bOrdered)
			{
				ControllerCommands::Report(this, PlayerCommands::CoverCornerPeek, nullptr, false, true, Peeker, Hit.ImpactPoint);
				if (Messages)
				{
					Messages->PostMessage(Peeker->DisplayName, LOCTEXT("SquadCornerPeek", "🎯 Covering the corner - the squad stacks up behind me."));
				}
			}
			else if (Messages)
			{
				Messages->PostMessage(LOCTEXT("HQ", "HQ"), LOCTEXT("CornerPeekRefused", "⚠️ Can't get to that corner."));
			}
			return true;
		}
	}
	// Run-and-peek (#55, user request 2026-11-07): a click on the END of a wall he is not in cover at (from the open, or from cover at another
	// wall) = run to that corner and peek round it on arrival, in one click (solo; a box selection: the man nearest that end takes it, the others
	// stack behind him; formation: the squad's wall order there with its corner peek carried). Before the proximity pick, as the peeks above.
	if (TryHandleRunCornerPeekClick(Operative, Hit, bAlt, TurnBased))
	{
		return true;
	}
	// Objects (barricades, barrels, crates, quest objects) keep their click meaning; Alt + click takes cover at them. (Rev 5e: checked
	// after the corner peek - the end of the barricade he crouches behind is a peek edge, its middle still opens its menu.)
	if (HitActor->IsA<AInteractableActor>() && !bAlt)
	{
		return false;
	}
	if (FindClickedMember(Hit) || (HitActor->IsA<APawn>() && !bVehicle) || HitActor->ActorHasTag(FName(TEXT("Enemy"))))
	{
		return false;
	}
	// Game designer 2026-11-01 (fix a): the side of a high wall is decided by the cursor - its top beyond the middle / the ground right
	// behind it = the FAR face, the top's near half = the near face (ResolveWallSideSlot). Not on the turn-based grid (its own click rules).
	FCoverSlot SideSlot;
	EWallClickSide WallSide = EWallClickSide::None;
	const bool bSideSlot = !TurnBased && !bAlt && !bVehicle && ResolveWallSideSlot(Hit, SideSlot, WallSide);
	if (bSideSlot)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Cover] click at (%.0f, %.0f, %.0f) on %s: the %s face (slot at (%.0f, %.0f), outward yaw %.0f)"), Hit.ImpactPoint.X,
			Hit.ImpactPoint.Y, Hit.ImpactPoint.Z, *GetNameSafe(HitActor), WallSide == EWallClickSide::Far ? TEXT("FAR") : TEXT("near"), SideSlot.WorldLocation.X,
			SideSlot.WorldLocation.Y, SideSlot.WallNormal.Rotation().Yaw);
	}
	// A floor / ramp is a move target, not a wall (Alt + click probes anyway: a wall edge may have been clicked); the
	// vehicle's roof is always its hull.
	if (!bSideSlot && FMath::Abs(Hit.ImpactNormal.Z) > 0.3f && !bAlt && !bVehicle)
	{
		return false;
	}
	// Another face of his own cover object (2026-10-20): round it along its wall, in cover all the way (no preview; the hover shows it). His
	// own face stays the shimmy below.
	if (!TurnBased && !bAlt && Operative->bInCover)
	{
		const FCoverSlot& Own = Operative->GetCoverSlot();
		const bool bOwnObject = Own.WallComponent.IsValid() ? Hit.GetComponent() == Own.WallComponent.Get() : HitActor == Own.WallActor.Get();
		// The wall side by the cursor (2026-11-01): its face and its slot stand for the top click (the far face = round the object in cover).
		const FVector2D HitNormal = bSideSlot ? FVector2D(SideSlot.WallNormal).GetSafeNormal() : FVector2D(Hit.ImpactNormal.X, Hit.ImpactNormal.Y).GetSafeNormal();
		if (bOwnObject && FVector2D::DotProduct(HitNormal, FVector2D(Own.WallNormal).GetSafeNormal()) < 0.9f
			&& TryOrderCoverWrap(Operative, bSideSlot ? SideSlot.WorldLocation : FVector(Hit.ImpactPoint)))
		{
			return true;
		}
	}
	FCoverSlot Slot = SideSlot;
	if (!bSideSlot && !FindCoverSlotForHit(Hit, Slot))
	{
		if (bAlt)
		{
			if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
			{
				Messages->PostMessage(LOCTEXT("HQ", "HQ"), LOCTEXT("NoCoverHere", "⚠️ No cover here: needs a wall or obstacle at least 40 cm high."));
			}
			HideCoverPreview();
			return true;
		}
		return false;
	}
	// Turn-based: the slot snaps to the nearest free cell of the grid.
	if (TurnBased && TurnBased->GetGrid())
	{
		UGorkyGridManager* Grid = TurnBased->GetGrid();
		const FIntPoint Cell = Grid->FindNearestFreeCell(Grid->WorldToGrid(Slot.WorldLocation));
		if (!Grid->IsValidCell(Cell))
		{
			return false;
		}
		const FVector Centre = Grid->GridToWorld(Cell);
		Slot.WorldLocation = FVector(Centre.X, Centre.Y, Slot.WorldLocation.Z);
	}
	// The click time / place: a confirm within the double-click time of the click that showed the ghost is a double-click (the
	// wall-edge cover switch, 2026-10-20; the same thresholds as the ground's double-click = sprint).
	FVector2D CoverMouse = FVector2D::ZeroVector; // stays zero without a viewport (headless smokes)
	GetMousePosition(CoverMouse.X, CoverMouse.Y);
	const double CoverClickNow = FApp::GetCurrentTime();
	// Second click on the previewed slot: confirm.
	// A switchable preview: any click on that same object (its ghost stands at the resolved corner, maybe on another face than the click).
	const bool bOnPreviewed = FVector::Dist2D(Slot.WorldLocation, CoverPreviewSlot.WorldLocation) <= 100.f
		|| (bCoverPreviewSwitchable && CoverWrapWorld::IsSameObject(Slot, CoverPreviewSlot));
	if (HasCoverPreview() && CoverPreviewOperative.Get() == Operative && bOnPreviewed)
	{
		const bool bDoubleClick = CoverPreviewClickTime >= 0.0 && CoverClickNow - CoverPreviewClickTime <= DoubleClickSeconds
			&& FVector2D::Distance(CoverMouse, CoverPreviewClickPosition) <= DoubleClickPixels;
		ConfirmCoverPreview(bDoubleClick);
		return true;
	}
	// Along his own wall: shimmy at once (no preview).
	if (!TurnBased && Operative->bInCover && CoverTraceRules::IsSameWall(Operative->GetCoverSlot(), Slot))
	{
		HideCoverPreview();
		FCoverSlot Shimmy;
		if (CoverTraceRules::FindShimmySlot(GetWorld(), Operative->GetCoverSlot(), bSideSlot ? Slot.WallPoint : FVector(Hit.ImpactPoint), Shimmy))
		{
			Operative->OrderShimmyTo(Shimmy);
		}
		else
		{
			Operative->OrderShimmyTo(Slot);
		}
		ControllerCommands::Report(this, PlayerCommands::CoverShimmy, nullptr, false, false, Operative, Hit.ImpactPoint);
		return true;
	}
	bCoverPreviewTurnBased = TurnBased != nullptr;
	ShowCoverPreview(Operative, Slot);
	CoverPreviewClickTime = CoverClickNow;
	CoverPreviewClickPosition = CoverMouse;
	ControllerCommands::Report(this, PlayerCommands::CoverPreview, nullptr, false, false, Operative, Slot.WorldLocation);
	return true;
}

bool ACodexTacticsPlayerController::FindCornerPeekForHit(const AOperativeCharacter* Operative, const FHitResult& Hit, ECoverFacing& OutSide,
	float& OutEdgeAlongCm) const
{
	if (!Operative || !Operative->IsInPeekableCover())
	{
		return false;
	}
	// The far (back) face of his own cover is no in-cover peek of his face (a thin block puts its end right at his own end): the run-and-peek
	// to that face's corner (the game designer 2026-11-08, RunCornerPeekRules::IsBackFaceHit).
	if (RunCornerPeekRules::IsBackFaceHit(FVector2D(Hit.ImpactNormal), static_cast<float>(Hit.ImpactNormal.Z), FVector2D(Operative->GetCoverSlot().WallNormal)))
	{
		return false;
	}
	return FindCornerPeekForSlot(Operative->GetCoverSlot(), Hit, OutSide, OutEdgeAlongCm);
}

bool ACodexTacticsPlayerController::PlanGroupCornerPeek(AOperativeCharacter* Operative, const FHitResult& Hit, AOperativeCharacter*& OutPeeker, ECoverFacing& OutSide,
	float& OutEdgeAlongCm, TArray<TPair<AOperativeCharacter*, FCoverSlot>>* OutStack) const
{
	OutPeeker = nullptr;
	if (OutStack)
	{
		OutStack->Reset();
	}
	const USquadSubsystem* Squad = GetSquad();
	UWorld* World = GetWorld();
	ECoverFacing Side = ECoverFacing::Right;
	float EdgeAlong = 0.f;
	if (CVarGroupCornerPeek.GetValueOnGameThread() == 0 || !Squad || !World || !Operative || !Squad->HasMultiSelection() || !Squad->IsGroupSelected(Operative)
		|| GetActiveTurnBased() || !FindCornerPeekForHit(Operative, Hit, Side, EdgeAlong))
	{
		return false;
	}
	// The selected members in cover at his wall, each one's place along it (from his slot, as the edge).
	const FCoverSlot Base = Operative->GetCoverSlot();
	TArray<AOperativeCharacter*> Who;
	TArray<double> Along;
	TArray<bool> Able;
	for (AOperativeCharacter* Member : Squad->GetSelectedGroup())
	{
		if (!Member || !Member->bInCover || !CoverTraceRules::IsSameWall(Base, Member->GetCoverSlot()))
		{
			continue;
		}
		Who.Add(Member);
		Along.Add(CoverTraceRules::AlongWallDistance(Base, Member->GetActorLocation()));
		Able.Add(Member->IsInPeekableCover() && !Member->IsKnockedDown() && !Member->IsPanicking() && !Member->IsRaging());
	}
	if (Who.Num() < 2)
	{
		return false;
	}
	const float StandOff = CoverFacingRules::CornerStandOff(Side, Operative->GetStance() == EOperativeStance::Crouching || Base.Height == ECoverHeight::LowCover,
		Operative->GetCoverFacingConfig());
	const FGroupCoverConfig GroupConfig;
	const FormationWallLeadership::FGroupCornerPeekPlan Plan = FormationWallLeadership::PlanGroupCornerPeek(Along, Able, EdgeAlong,
		CoverFacingRules::CornerPeekStandAlong(Side, EdgeAlong, StandOff), GroupConfig.SlotSpacingCm, GroupCornerPeekKeepCm);
	if (!Who.IsValidIndex(Plan.Peeker))
	{
		return false;
	}
	AOperativeCharacter* Peeker = Who[Plan.Peeker];
	if (Peeker == Operative)
	{
		OutSide = Side;
		OutEdgeAlongCm = EdgeAlong;
	}
	else if (!FindCornerPeekForHit(Peeker, Hit, OutSide, OutEdgeAlongCm))
	{
		return false;
	}
	OutPeeker = Peeker;
	if (OutStack)
	{
		for (int32 Position = 0; Position < Plan.Order.Num() && Position < Plan.PlaceAlong.Num(); ++Position)
		{
			AOperativeCharacter* Member = Who[Plan.Order[Position]];
			FCoverSlot Target;
			if (FMath::Abs(Plan.PlaceAlong[Position] - Along[Plan.Order[Position]]) > 1.0
				&& CoverTraceRules::FindShimmySlot(World, Member->GetCoverSlot(), Base.WallPoint + Base.RightTangent() * static_cast<float>(Plan.PlaceAlong[Position]), Target))
			{
				OutStack->Add(TPair<AOperativeCharacter*, FCoverSlot>(Member, Target));
			}
		}
	}
	return true;
}

bool ACodexTacticsPlayerController::FindCornerPeekForSlot(const FCoverSlot& Slot, const FHitResult& Hit, ECoverFacing& OutSide, float& OutEdgeAlongCm) const
{
	const AActor* HitActor = Hit.GetActor();
	if (!HitActor || (Slot.WallActor.IsValid() && HitActor != Slot.WallActor.Get()))
	{
		return false; // only the end of HIS wall (another wall's corner is a cover click there)
	}
	if (CoverFacingRules::FindPeekEdge(Slot, Hit.ImpactPoint, nullptr, OutSide, OutEdgeAlongCm))
	{
		return true;
	}
	// A long wall: its end lies beyond the corner probes of his slot - probe the wall next to the click (a click on the end face
	// or the top past the last face point: a little further in).
	const float Along = CoverTraceRules::AlongWallDistance(Slot, Hit.ImpactPoint);
	const FVector Inward = Slot.RightTangent() * (Along > 0.f ? -1.f : 1.f);
	for (const float Back : { 0.f, 40.f, 80.f })
	{
		FCoverSlot Probe;
		if (CoverTraceRules::FindShimmySlot(GetWorld(), Slot, Hit.ImpactPoint + Inward * Back, Probe))
		{
			return CoverFacingRules::FindPeekEdge(Slot, Hit.ImpactPoint, &Probe, OutSide, OutEdgeAlongCm);
		}
	}
	return false;
}

bool ACodexTacticsPlayerController::FindSquadCornerPeekForHit(const FHitResult& Hit, FSquadCornerPeekTarget& OutTarget, bool bAllowLeaderRunPeek) const
{
	OutTarget = FSquadCornerPeekTarget();
	USquadSubsystem* Squad = GetSquad();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	UWorld* World = GetWorld();
	const AActor* HitActor = Hit.GetActor();
	float NormalYaw = 0.f;
	// bAllowLeaderRunPeek (the formation's run-and-peek carry, #55 fix 2026-11-08): the leader whose own run into that corner went straight into
	// the peek stance is in cover there already - the squad's peek attaches to him (his stack forms behind him; nobody re-ordered).
	const bool bLeaderRunPeek = Leader && Squad && Squad->IsLeaderCoverPeekPlannable(*Leader, bAllowLeaderRunPeek);
	if (!Leader || !World || !HitActor || (Leader->bInCover && !Squad->IsWallLeadershipPeeker(Leader) && !bLeaderRunPeek) || GetActiveTurnBased() || HitActor->IsA<APawn>()
		|| HitActor->IsA<AInteractableActor>()
		|| HitActor->ActorHasTag(FName(TEXT("Enemy"))) || Squad->GetFormationKind() != EFormationKind::Wall || !Squad->GetFormationWallNormalYaw(NormalYaw))
	{
		return false;
	}
	// The squad's wall: the cover slot a man would take behind the leader (he stands ~45 cm off its face, his back to it).
	const FVector Normal = FRotator(0.f, NormalYaw, 0.f).Vector();
	const FVector Feet = Leader->GetActorLocation() - FVector(0.f, 0.f, Leader->GetSimpleCollisionHalfHeight());
	if (FVector::DistSquared2D(Leader->GetActorLocation(), SquadPeekBaseLeaderAt) > FMath::Square(5.f) || !FMath::IsNearlyEqual(NormalYaw, SquadPeekBaseYaw, 1.f))
	{
		SquadPeekBaseLeaderAt = Leader->GetActorLocation();
		SquadPeekBaseYaw = NormalYaw;
		bSquadPeekBaseValid = CoverTraceRules::FindCoverSlotAt(World, Feet + FVector(0.f, 0.f, 90.f) - Normal * 45.f, -Normal, SquadPeekBaseSlot)
			&& SquadPeekBaseSlot.IsValid();
	}
	if (!bSquadPeekBaseValid)
	{
		return false;
	}
	const FCoverSlot Base = SquadPeekBaseSlot;
	const bool bSquadWall = Base.WallComponent.IsValid() ? Hit.GetComponent() == Base.WallComponent.Get() : HitActor == Base.WallActor.Get();
	ECoverFacing Side = ECoverFacing::Right;
	float EdgeAlong = 0.f;
	if (!bSquadWall || !FindCornerPeekForSlot(Base, Hit, Side, EdgeAlong))
	{
		return false; // not the end of the squad's own wall (another wall is a cover click there)
	}
	const FVector Right = Base.RightTangent();
	const FVector EdgeOnFace = Base.WallPoint + Right * EdgeAlong;
	AOperativeCharacter* Peeker = nullptr;
	FFormationSquadPeek Plan;
	if (!Squad->PlanFormationCornerPeek(EdgeOnFace, Peeker, Plan, bAllowLeaderRunPeek) || !Peeker)
	{
		return false;
	}
	// His corner: the single peek's stand point short of that end, in the stance his cover entry gives him there (high: standing, low:
	// crouched; already in cover at that wall: his own).
	const bool bCrouched = Base.Height == ECoverHeight::LowCover || (Peeker->bInCover ? Peeker->GetStance() == EOperativeStance::Crouching
		: CoverRules::DefaultStanceFor(Base.Height) == EOperativeStance::Crouching);
	const float Stand = CoverFacingRules::CornerPeekStandAlong(Side, EdgeAlong, CoverFacingRules::CornerStandOff(Side, bCrouched, Peeker->GetCoverFacingConfig()));
	OutTarget.Peeker = Peeker;
	OutTarget.CornerSlot = ResolveCornerStandSlot(Base, Side, Stand);
	OutTarget.Side = Side;
	OutTarget.EdgeOnFace = EdgeOnFace;
	OutTarget.bCrouched = bCrouched;
	return true;
}

bool ACodexTacticsPlayerController::IsCornerPeekPreviewShown() const
{
	return CornerPeekGhost && CornerPeekGhost->IsShown();
}

bool ACodexTacticsPlayerController::UpdateCornerPeekHover(AOperativeCharacter* Operative, const FHitResult* Hit)
{
	bool bEdge = false;
	const UTutorialSubsystem* Tutorial = GetWorld()->GetSubsystem<UTutorialSubsystem>();
	const bool bPeekLocked = Tutorial && Tutorial->IsCommandLocked(PlayerCommands::CoverCornerPeek);
	// The squad's corner peek (2026-10-26): the ghost is the peeker's (the member nearest the hovered end), not the leader's.
	const FSquadCornerPeekTarget* SquadTarget = nullptr;
	const USquadSubsystem* HoverSquad = GetSquad();
	// Wall leadership (2026-11-01): the leader holding the squad peek's corner previews the squad's peek, not his single one.
	const bool bLeadershipPeeker = HoverSquad && HoverSquad->IsWallLeadershipPeeker(Operative);
	if (Operative && Hit && (!Operative->bInCover || bLeadershipPeeker) && !bPeekLocked && !GetActiveTurnBased())
	{
		if (Hit->GetActor() != SquadPeekHoverActor.Get() || FVector::DistSquared(Hit->ImpactPoint, SquadPeekHoverPoint) > FMath::Square(5.f)
			|| FVector::DistSquared2D(Operative->GetActorLocation(), SquadPeekHoverLeaderAt) > FMath::Square(5.f))
		{
			SquadPeekHoverActor = Hit->GetActor();
			SquadPeekHoverPoint = Hit->ImpactPoint;
			SquadPeekHoverLeaderAt = Operative->GetActorLocation();
			bSquadPeekHoverValid = FindSquadCornerPeekForHit(*Hit, SquadPeekHoverTarget);
		}
		SquadTarget = bSquadPeekHoverValid && SquadPeekHoverTarget.Peeker.IsValid() ? &SquadPeekHoverTarget : nullptr;
		bEdge = SquadTarget != nullptr;
	}
	else if (Operative && Hit && Operative->IsInPeekableCover() && !Operative->bShimmying && !bPeekLocked)
	{
		// The probe traces only when the cursor moved over the wall (or he moved): cached while it rests.
		if (Hit->GetActor() != PeekHoverActor.Get() || Operative != PeekHoverOperative.Get()
			|| FVector::DistSquared(Hit->ImpactPoint, PeekHoverPoint) > FMath::Square(5.f)
			|| FVector::DistSquared2D(Operative->GetCoverSlot().WorldLocation, PeekHoverSlotLocation) > FMath::Square(5.f))
		{
			PeekHoverActor = Hit->GetActor();
			PeekHoverOperative = Operative;
			PeekHoverPoint = Hit->ImpactPoint;
			PeekHoverSlotLocation = Operative->GetCoverSlot().WorldLocation;
			bPeekHoverEdge = FindCornerPeekForHit(Operative, *Hit, PeekHoverSide, PeekHoverEdgeAlong);
			// The game designer's bug 2026-11-03: a box selection along that wall - the ghost is the member who would take the corner.
			AOperativeCharacter* GroupPeeker = nullptr;
			ECoverFacing GroupSide = PeekHoverSide;
			float GroupEdge = PeekHoverEdgeAlong;
			PeekHoverGroupPeeker.Reset();
			if (bPeekHoverEdge && PlanGroupCornerPeek(Operative, *Hit, GroupPeeker, GroupSide, GroupEdge, nullptr) && GroupPeeker && GroupPeeker != Operative)
			{
				PeekHoverGroupPeeker = GroupPeeker;
				PeekHoverSide = GroupSide;
				PeekHoverEdgeAlong = GroupEdge;
			}
		}
		bEdge = bPeekHoverEdge;
	}
	// Run-and-peek (#55): the end of a wall he is not in cover at - the ghost of the man who would run there and peek (a box selection: the
	// one nearest that end), at its corner on the face looking at him.
	if (!bEdge && Operative && Hit && !bPeekLocked && !GetActiveTurnBased() && IsRunCornerPeekEnabled() && !Operative->bShimmying)
	{
		FRunCornerPeekFace RunFace;
		if (FindRunCornerPeekClickTarget(Operative, *Hit, IsSquadWideModifierDown(), nullptr, RunFace, RunPeekHoverTarget) && RunPeekHoverTarget.Peeker.IsValid())
		{
			SquadTarget = &RunPeekHoverTarget;
			bEdge = true;
		}
	}
	if (!bEdge)
	{
		if (CornerPeekGhost && CornerPeekGhost->IsShown())
		{
			CornerPeekGhost->Hide();
		}
		if (bCornerPeekCursor)
		{
			CurrentMouseCursor = DefaultMouseCursor;
			bCornerPeekCursor = false;
		}
		return false;
	}
	if (!CornerPeekGhost)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		CornerPeekGhost = GetWorld()->SpawnActor<ACoverGhostActor>(Operative->GetActorLocation(), FRotator::ZeroRotator, Params);
	}
	if (CornerPeekGhost && SquadTarget)
	{
		AOperativeCharacter* Peeker = SquadTarget->Peeker.Get();
		if (!CornerPeekGhost->IsShown() || !CornerPeekGhost->IsCornerPeekPose() || CornerPeekGhost->GetOperative() != Peeker
			|| FVector::DistSquared2D(CornerPeekGhost->GetSlot().WorldLocation, SquadTarget->CornerSlot.WorldLocation) > 1.f)
		{
			CornerPeekGhost->ShowCornerPeekFor(*Peeker, SquadTarget->CornerSlot, SquadTarget->Side, SquadTarget->bCrouched);
		}
	}
	else if (CornerPeekGhost)
	{
		// The corner stand point: the cover slot moved along the wall to the stand-off from that edge.
		if (AOperativeCharacter* GroupPeeker = PeekHoverGroupPeeker.Get(); GroupPeeker && GroupPeeker->IsInPeekableCover() && HoverSquad && HoverSquad->HasMultiSelection())
		{
			Operative = GroupPeeker; // the box selection's corner peek (bug 2026-11-03): his ghost, from his slot
		}
		FCoverSlot Corner = Operative->GetCoverSlot();
		const float StandOff = CoverFacingRules::CornerStandOff(PeekHoverSide, Operative->GetStance() == EOperativeStance::Crouching
			|| Operative->CurrentCoverHeight == ECoverHeight::LowCover, Operative->GetCoverFacingConfig());
		const FVector Shift = Corner.RightTangent() * CoverFacingRules::CornerPeekStandAlong(PeekHoverSide, PeekHoverEdgeAlong, StandOff);
		Corner.WorldLocation += Shift;
		Corner.WallPoint += Shift;
		if (!CornerPeekGhost->IsShown() || !CornerPeekGhost->IsCornerPeekPose() || CornerPeekGhost->GetOperative() != Operative
			|| FVector::DistSquared2D(CornerPeekGhost->GetSlot().WorldLocation, Corner.WorldLocation) > 1.f)
		{
			CornerPeekGhost->ShowCornerPeekFor(*Operative, Corner, PeekHoverSide);
		}
	}
	CurrentMouseCursor = EMouseCursor::Crosshairs;
	bCornerPeekCursor = true;
	return true;
}

void ACodexTacticsPlayerController::ShowCoverPreview(AOperativeCharacter* Operative, const FCoverSlot& Slot)
{
	if (!Operative || !Slot.IsValid())
	{
		return;
	}
	if (!CoverGhost)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		CoverGhost = GetWorld()->SpawnActor<ACoverGhostActor>(Slot.WorldLocation, FRotator::ZeroRotator, Params);
	}
	if (!CoverGhost)
	{
		return;
	}
	CoverPreviewSlot = Slot;
	CoverPreviewOperative = Operative;
	CoverPreviewClickTime = -1.0; // the click handler stamps the click that showed it
	// Wall-edge cover switch (2026-10-20): from a wall edge this cover is a double-click dash target (the HUD draws the dash line). The ghost
	// stands where the hover ghost stood - the other object's nearest corner in the mirrored pose - and the confirm commits exactly that.
	{
		const UTutorialSubsystem* SwitchTutorial = GetWorld()->GetSubsystem<UTutorialSubsystem>();
		bCoverPreviewSwitchable = !bCoverPreviewTurnBased && !GetActiveTurnBased() && Operative->EvaluateCoverSwitch(Slot) == ECoverSwitchVerdict::Switch
			&& !(SwitchTutorial && SwitchTutorial->IsCommandLocked(PlayerCommands::CoverSwitch));
		CoverPreviewResolvedSlot = bCoverPreviewSwitchable ? Operative->ResolveCoverSwitchSlot(Slot, &CoverPreviewArrivalFacing) : Slot;
		CoverPreviewSwitchTarget = CoverPreviewResolvedSlot.WorldLocation;
		if (bCoverPreviewSwitchable)
		{
			UpdateCoverSwitchPath(Operative, CoverPreviewSwitchTarget);
			CoverGhost->ShowForFacing(*Operative, CoverPreviewResolvedSlot, CoverPreviewArrivalFacing);
		}
		else
		{
			CoverGhost->ShowFor(*Operative, Slot);
		}
		UpdateCoverSwitchHover(nullptr, nullptr); // the click ghost replaces the hover one
	}
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		const FText Kind = Slot.Height == ECoverHeight::HighCover ? LOCTEXT("CoverHigh", "wall") : LOCTEXT("CoverLow", "low cover");
		const FText Corners = Slot.HasExposedEdge() ? LOCTEXT("CoverCorner", ", corner to fire from") : FText::GetEmpty();
		Messages->PostMessage(Operative->DisplayName, FText::Format(LOCTEXT("CoverPreview", "🧱 Cover: {0}{1}. Click again to take it."), Kind, Corners));
		if (Slot.WallActor.IsValid() && Slot.WallActor->IsA<AVehiclePawn>())
		{
			Messages->PostMessage(LOCTEXT("HQ", "HQ"), FText::FromString(PlayerCommands::ResolveKeyTokens(
				TEXT("{key:Vehicle.Board} Board the APC, [Alt+Y] all aboard."))));
		}
	}
}

void ACodexTacticsPlayerController::HideCoverPreview()
{
	if (CoverGhost && CoverGhost->IsShown())
	{
		CoverGhost->Hide();
	}
	CoverPreviewOperative.Reset();
	bCoverPreviewSwitchable = false;
}

bool ACodexTacticsPlayerController::UpdateCoverSwitchHover(AOperativeCharacter* Operative, const FHitResult* Hit)
{
	bool bShow = false;
	AActor* HitActor = Hit ? Hit->GetActor() : nullptr;
	const UTutorialSubsystem* Tutorial = GetWorld()->GetSubsystem<UTutorialSubsystem>();
	// The same surfaces a cover click takes (TryHandleCoverClick): a wall, not a body / enemy / floor; an object only with Alt. Not his
	// own wall (a shimmy / the corner peek), not over the click ghost or the corner peek preview, not while it is locked.
	if (Operative && HitActor && Operative->bInCover && !Operative->bShimmying && !GetActiveTurnBased() && !HasCoverPreview() && !bCornerPeekCursor
		&& !(Tutorial && (Tutorial->IsCommandLocked(PlayerCommands::CoverSwitch) || Tutorial->IsCommandLocked(PlayerCommands::CoverPreview)))
		&& HitActor != Operative->GetCoverSlot().WallActor.Get() && !HitActor->IsA<APawn>() && !HitActor->ActorHasTag(FName(TEXT("Enemy")))
		&& (!HitActor->IsA<AInteractableActor>() || IsSquadWideModifierDown()) && FMath::Abs(Hit->ImpactNormal.Z) <= 0.3f)
	{
		ECoverFacing Edge = ECoverFacing::Right;
		if (CoverSwitchRules::FindSwitchEdge(Operative->GetCoverSlot(), Hit->ImpactPoint, Operative->CoverSwitchEdgeReachCm, Edge))
		{
			// The slot probe traces only when the cursor moved over the wall (or he moved): cached while it rests.
			if (HitActor != SwitchHoverActor.Get() || Operative != CoverSwitchHoverOperative.Get()
				|| FVector::DistSquared(Hit->ImpactPoint, SwitchHoverPoint) > FMath::Square(5.f)
				|| FVector::DistSquared2D(Operative->GetCoverSlot().WorldLocation, SwitchHoverSlotLocation) > FMath::Square(5.f))
			{
				SwitchHoverActor = HitActor;
				CoverSwitchHoverOperative = Operative;
				SwitchHoverPoint = Hit->ImpactPoint;
				SwitchHoverSlotLocation = Operative->GetCoverSlot().WorldLocation;
				FCoverSlot Probe;
				bSwitchHoverValid = FindCoverSlotForHit(*Hit, Probe) && Operative->EvaluateCoverSwitch(Probe) == ECoverSwitchVerdict::Switch;
				if (bSwitchHoverValid)
				{
					// The other object's nearest corner and the mirrored pose he arrives in (2026-10-20, second pass); the dash's real path.
					CoverSwitchHoverSlot = Operative->ResolveCoverSwitchSlot(Probe, &CoverSwitchHoverFacing);
					UpdateCoverSwitchPath(Operative, CoverSwitchHoverSlot.WorldLocation);
				}
			}
			bShow = bSwitchHoverValid;
		}
	}
	if (!bShow)
	{
		if (CoverSwitchGhost && CoverSwitchGhost->IsShown())
		{
			CoverSwitchGhost->Hide();
		}
		bCoverSwitchHoverShown = false;
		return false;
	}
	if (!CoverSwitchGhost)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		CoverSwitchGhost = GetWorld()->SpawnActor<ACoverGhostActor>(CoverSwitchHoverSlot.WorldLocation, FRotator::ZeroRotator, Params);
	}
	if (CoverSwitchGhost && (!CoverSwitchGhost->IsShown() || CoverSwitchGhost->GetOperative() != Operative || CoverSwitchGhost->GetShownFacing() != CoverSwitchHoverFacing
		|| FVector::DistSquared2D(CoverSwitchGhost->GetSlot().WorldLocation, CoverSwitchHoverSlot.WorldLocation) > 1.f))
	{
		CoverSwitchGhost->ShowForFacing(*Operative, CoverSwitchHoverSlot, CoverSwitchHoverFacing);
	}
	bCoverSwitchHoverShown = true;
	return true;
}

bool ACodexTacticsPlayerController::IsCoverSwitchHoverShown() const
{
	return bCoverSwitchHoverShown && CoverSwitchGhost && CoverSwitchGhost->IsShown();
}

bool ACodexTacticsPlayerController::GetCoverSwitchPreviewLine(FVector& OutFrom, FVector& OutTo) const
{
	const AOperativeCharacter* Operative = nullptr;
	if (IsCoverSwitchHoverShown() && CoverSwitchHoverOperative.IsValid())
	{
		Operative = CoverSwitchHoverOperative.Get();
		OutTo = CoverSwitchHoverSlot.WorldLocation;
	}
	else if (HasCoverPreview() && bCoverPreviewSwitchable)
	{
		Operative = CoverPreviewOperative.Get();
		OutTo = CoverPreviewSwitchTarget;
	}
	if (!Operative)
	{
		return false;
	}
	OutFrom = Operative->GetActorLocation() - FVector(0.f, 0.f, Operative->GetSimpleCollisionHalfHeight());
	return true;
}

void ACodexTacticsPlayerController::ConfirmCoverPreview(bool bDoubleClick)
{
	AOperativeCharacter* Operative = CoverPreviewOperative.Get();
	const FCoverSlot Slot = CoverPreviewSlot;
	// A switchable preview commits the slot its ghost shows (the resolved corner, its arrival side), double or slow confirm alike.
	const bool bPreviewSwitchable = bCoverPreviewSwitchable;
	const FCoverSlot Target = bPreviewSwitchable ? CoverPreviewResolvedSlot : Slot;
	const ECoverFacing Arrival = CoverPreviewArrivalFacing;
	HideCoverPreview();
	bLastCoverConfirmSwitch = false;
	if (!Operative || !Slot.IsValid())
	{
		return;
	}
	UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>();
	UCombatFeedbackSubsystem* Feedback = GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>();
	if (UTurnBasedCombatSubsystem* TurnBased = GetActiveTurnBased(); TurnBased && TurnBased->GetGrid())
	{
		// The grid walk; the operative enters the slot when the walk stops on the cell (AOperativeCharacter::UpdateCover).
		const FIntPoint Cell = TurnBased->GetGrid()->WorldToGrid(Slot.WorldLocation);
		Operative->SetPendingCover(Slot);
		if (!TurnBased->MoveActiveUnitTo(Cell))
		{
			Operative->ClearPendingCover();
			if (Messages)
			{
				Messages->PostMessage(LOCTEXT("Tactics", "TACTICS"), LOCTEXT("CoverCellUnreachable", "⚠️ Cover out of reach (no AP or path)."));
			}
		}
		else
		{
			ControllerCommands::Report(this, PlayerCommands::CoverEnter, nullptr, false, false, Operative, Slot.WorldLocation);
			if (Slot.HasExposedEdge())
			{
				ControllerCommands::Report(this, PlayerCommands::CoverCorner, nullptr, false, false, Operative, Slot.WorldLocation);
			}
		}
		return;
	}
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	const ECombatOrderDispatch Dispatch = Flow ? FCombatTimeModeRules::GetOrderDispatch(Flow->GetPhase(), Flow->GetCombatMode(),
		OrderLock::CVarRealTimeOrders.GetValueOnGameThread() != 0) : ECombatOrderDispatch::Execute;
	// Wall-edge cover switch (game designer 2026-10-20): a DOUBLE-click on another cover while he stands at a wall edge, within range;
	// 2026-10-21: a slow second click on a switchable ghost is the switch too (the quick dash); from anywhere else the ordinary cover order.
	// Locked by the tutorial: the ordinary cover order (TutorialRules::ResolveLockedCommand degrades Cover.Switch to Cover.Enter).
	const UTutorialSubsystem* SwitchTutorial = GetWorld()->GetSubsystem<UTutorialSubsystem>();
	LastCoverSwitchVerdict = Operative->EvaluateCoverSwitch(Slot);
	// Wall leadership (2026-11-01): the leader holds the corner of the SQUAD's peek - his double-click on another cover is the squad's wall
	// order there (the formation wall layout; the peek carried over, bug_04 A), not his own dash with the others' legacy group cover.
	const USquadSubsystem* SwitchSquad = GetSquad();
	const bool bSquadPeekLeader = SwitchSquad && SwitchSquad->IsWallLeadershipPeeker(Operative) && !SwitchSquad->HasMultiSelection();
	const bool bSwitch = !bSquadPeekLeader && bPreviewSwitchable && CoverSwitchRules::IsSwitchConfirm(bDoubleClick, LastCoverSwitchVerdict,
		SwitchTutorial && SwitchTutorial->IsCommandLocked(PlayerCommands::CoverSwitch));
	bLastCoverConfirmSwitch = bSwitch;
	if (bDoubleClick)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: double-click on another cover - %s -> %s"), *Operative->DisplayName.ToString(),
			CoverSwitchRules::VerdictName(LastCoverSwitchVerdict), bSwitch ? TEXT("the cover switch") : TEXT("the ordinary cover order"));
	}
	// Game designer 2026-10-20: a box selection / group mode takes cover together (the others beside him or prone behind it).
	if (OrderGroupCover(Operative, Target, Dispatch == ECombatOrderDispatch::Queue, Flow ? Flow->GetPauseOrderRadius() : 1200.f, bSwitch))
	{
		return;
	}
	if (Dispatch == ECombatOrderDispatch::Queue)
	{
		// Tactical pause: the walk is planned, the slot entered on arrival (a switch: through the run-in clip after the release).
		const FCoverSlot PlanSlot = Target;
		Operative->SetPendingCover(PlanSlot);
		Operative->SetPendingCoverSwitch(bSwitch);
		if (bPreviewSwitchable)
		{
			Operative->SetCoverArrivalFacing(Arrival);
		}
		const FVector Planned = GetSquad()->PlanMove(Operative, PlanSlot.WorldLocation, true, Flow ? Flow->GetPauseOrderRadius() : 1200.f);
		if (Feedback)
		{
			Feedback->SpawnWaypointMarker(Planned);
		}
		if (Messages)
		{
			Messages->PostMessage(Operative->DisplayName, bSwitch ? LOCTEXT("CoverSwitchPlanned", "📋 [PLAN] Dash to the other cover!")
				: LOCTEXT("CoverPlanned", "📋 [PLAN] Take cover at the wall!"));
		}
		ControllerCommands::Report(this, PlayerCommands::CoverEnter, nullptr, true, false, Operative, PlanSlot.WorldLocation);
		if (bSwitch)
		{
			ControllerCommands::Report(this, PlayerCommands::CoverSwitch, nullptr, true, false, Operative, PlanSlot.WorldLocation);
		}
		if (PlanSlot.HasExposedEdge())
		{
			ControllerCommands::Report(this, PlayerCommands::CoverCorner, nullptr, true, false, Operative, PlanSlot.WorldLocation);
		}
		return;
	}
	if (UVehicleSubsystem* Vehicles = GetWorld()->GetSubsystem<UVehicleSubsystem>())
	{
		Vehicles->CancelBoarding(Operative);
	}
	const EOperativeOrderResult Result = bSwitch ? Operative->OrderCoverSwitchResolved(Target, Arrival) : Operative->OrderTakeCover(Target, true);
	if (Result == EOperativeOrderResult::Accepted && !bSwitch && bPreviewSwitchable)
	{
		Operative->SetCoverArrivalFacing(Arrival); // the slow confirm of a switch target: the ordinary run, the previewed pose
	}
	if (Result == EOperativeOrderResult::Accepted)
	{
		ControllerCommands::Report(this, PlayerCommands::CoverEnter, nullptr, false, false, Operative, Target.WorldLocation);
		if (bSwitch)
		{
			ControllerCommands::Report(this, PlayerCommands::CoverSwitch, nullptr, false, false, Operative, Target.WorldLocation);
		}
		if (Target.HasExposedEdge())
		{
			ControllerCommands::Report(this, PlayerCommands::CoverCorner, nullptr, false, false, Operative, Target.WorldLocation);
		}
		if (Feedback)
		{
			Feedback->SpawnMovePing(Target.WorldLocation, true);
		}
		if (Messages)
		{
			Messages->PostMessage(Operative->DisplayName, bSwitch ? LOCTEXT("CoverSwitchOrdered", "🏃 Dash to the other cover!") : LOCTEXT("CoverOrdered", "🏃 Take cover!"));
		}
	}
	else if (Messages && Result == EOperativeOrderResult::Unreachable)
	{
		Messages->PostMessage(LOCTEXT("HQ", "HQ"), LOCTEXT("CoverUnreachable", "⚠️ No path to this cover."));
	}
}

bool ACodexTacticsPlayerController::OrderGroupCover(AOperativeCharacter* Direct, const FCoverSlot& Slot, bool bPlan, float Radius, bool bLeaderSwitch,
	const FCoverWrapPlan* LeaderWrap)
{
	USquadSubsystem* Squad = GetSquad();
	UWorld* World = GetWorld();
	if (!Squad || !Direct || !World || !Slot.IsValid() || GetActiveTurnBased())
	{
		return false;
	}
	// Bug 2026-11-08: the next order after a selection switch mid-move - the selected member takes the lead first (the old leader's walk
	// ends, he goes along as a follower), so the group's members and the wall layout are his.
	Squad->HandOverLeadForOrder(Direct, bPlan ? TEXT("a planned cover order") : TEXT("a cover order"));
	const TArray<AOperativeCharacter*> Others = Squad->GetGroupCoverMembers(Direct);
	if (!SquadOrderRules::IsGroupCoverOrder(Others.Num()))
	{
		return false;
	}
	// Formation look sectors (Codex.Formation.Sectors, docs/design/FORMATION_SECTORS.md "Wall"): in group mode (no box selection, the
	// group cover formation; not a cover switch / wrap) the group cover order is the formation wall layout - all along the wall 1 m apart,
	// crouched facing OUT, nobody looks through the wall. Else (or when no layout can be made) as before. User decision 2026-10-26: also in
	// the fight (USquadSubsystem::IsGroupCoverFormation) - in the tactical pause the layout is planned and ordered on the release.
	const bool bWallLayout = FormationRuntime::IsEnabled() && !bLeaderSwitch && !LeaderWrap && !Squad->HasMultiSelection() && !Squad->IsSoloMode()
		&& Squad->IsGroupCoverFormation();
	FVector PlannedWall = Slot.WorldLocation;
	if (bWallLayout && bPlan && Squad->PlanFormationWall(Direct, Slot, Radius, PlannedWall))
	{
		LastGroupCoverSlots = 1 + Others.Num();
		LastGroupCoverProne = 0;
		if (UCombatFeedbackSubsystem* PlanFeedback = World->GetSubsystem<UCombatFeedbackSubsystem>())
		{
			PlanFeedback->SpawnWaypointMarker(PlannedWall);
		}
		ControllerCommands::Report(this, PlayerCommands::CoverEnter, nullptr, true, true, Direct, Slot.WorldLocation);
		if (UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>())
		{
			Messages->PostMessage(LOCTEXT("SquadSender", "SQUAD"), FText::Format(LOCTEXT("GroupWallPlanned",
				"📋 [PLAN] Along the wall ({0} operatives), facing out!"), 1 + Others.Num()));
		}
		return true;
	}
	// Bug_04 A (2026-11-01): the peek in force before the order ends it (OrderFormationWall) - it decides whether the peek moves on.
	const bool bPeekInForce = Squad->GetFormationPeeker() != nullptr;
	if (bWallLayout && !bPlan && Squad->OrderFormationWall(Direct, Others, Slot))
	{
		StartSquadPeekCarry(*Squad, *Direct, Slot, bPeekInForce);
		LastGroupCoverSlots = 1 + Others.Num();
		LastGroupCoverProne = 0;
		UInteractionSubsystem* WallUseOrders = World->GetSubsystem<UInteractionSubsystem>();
		UVehicleSubsystem* WallVehicles = World->GetSubsystem<UVehicleSubsystem>();
		for (AOperativeCharacter* Member : Others)
		{
			if (WallUseOrders)
			{
				WallUseOrders->CancelUseOrder(Member); // as the cover order: it replaces a planned / running "Ignite"
			}
			if (WallVehicles)
			{
				WallVehicles->CancelBoarding(Member);
			}
			Member->CancelQuickFire();
			Member->CancelSuppressFire(TEXT("a cover order"));
		}
		if (UCombatFeedbackSubsystem* WallFeedback = World->GetSubsystem<UCombatFeedbackSubsystem>())
		{
			WallFeedback->SpawnMovePing(Slot.WorldLocation, true);
		}
		ControllerCommands::Report(this, PlayerCommands::CoverEnter, nullptr, false, true, Direct, Slot.WorldLocation);
		if (UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>())
		{
			Messages->PostMessage(LOCTEXT("SquadSender", "SQUAD"), FText::Format(LOCTEXT("GroupWallOrdered",
				"🏃 Along the wall ({0} operatives), facing out!"), 1 + Others.Num()));
		}
		// Rules v2.2: the one wall-group call on every group cover path (here: the wall layout's own looks are the allocation).
		Squad->ReallocateWallGroup(Direct, Others, TEXT("the group cover order: the formation wall layout"));
		return true;
	}
	const FGroupCoverConfig Config;
	// The direct order's slot: the clicked one. On a high wall the corner he would walk to by himself right after entering
	// (TrySnapToCoverCorner) is taken at once - the corner is his, and nobody is placed on that walk.
	FCoverSlot DirectSlot = Slot;
	if (LeaderWrap || bLeaderSwitch)
	{
		// The wrap round his object / the switch (2026-10-20): the caller's slot is exact (the plan's goal / the previewed resolved corner).
	}
	else if (Slot.Height == ECoverHeight::HighCover)
	{
		const ECoverFacing Facing = Direct->PredictCoverFacing(Slot);
		float Shift = 0.f;
		FCoverSlot Corner;
		if (CoverFacingRules::ShouldSnapToCorner(Slot, Facing, Shift, Direct->GetCoverFacingConfig(), false)
			&& CoverTraceRules::FindShimmySlot(World, Slot, Slot.WallPoint + CoverFacingRules::AlongWallDirection(Slot, Facing) * Shift, Corner)
			&& (!Slot.WallActor.IsValid() || Corner.WallActor == Slot.WallActor))
		{
			DirectSlot = Corner;
		}
	}
	TArray<const AActor*> GroupActors{ Direct };
	for (const AOperativeCharacter* Other : Others)
	{
		GroupActors.Add(Other);
	}
	// A slot / spot is taken by any other body standing there (an operative outside the order, an enemy) or by another
	// operative's cover slot.
	auto IsTaken = [World, &GroupActors, &Config](const FVector& Point)
	{
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			if (GroupActors.Contains(*It) || It->IsA<AVehiclePawn>() || It->IsHidden())
			{
				continue;
			}
			if (FVector::Dist2D(It->GetActorLocation(), Point) < Config.OccupiedRadiusCm && FMath::Abs(It->GetActorLocation().Z - Point.Z) < 250.f)
			{
				return true;
			}
			if (const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(*It);
				Operative && Operative->bInCover && FVector::Dist2D(Operative->GetCoverSlot().WorldLocation, Point) < Config.OccupiedRadiusCm)
			{
				return true;
			}
		}
		return false;
	};
	// Probes along the same wall, both sides, outward from his slot, a slot spacing apart.
	const FVector Tangent = DirectSlot.RightTangent();
	const int32 Probes = GroupCoverRules::ProbesPerSide(Others.Num(), Config);
	// A leader going round the object along its wall (2026-10-20) comes along the destination face from one side: nobody is placed there
	// (they would stand in his way); the others take the far side, else prone behind the cover.
	int32 WrapApproachSide = 0;
	if (LeaderWrap && LeaderWrap->Waypoints.Num() >= 2)
	{
		WrapApproachSide = CoverTraceRules::AlongWallDistance(DirectSlot, LeaderWrap->Waypoints[LeaderWrap->Waypoints.Num() - 2]) < 0.f ? -1 : 1;
	}
	TArray<EGroupCoverProbe> SideProbes[2];
	TMap<int32, FCoverSlot> ProbeSlots; // key: Side * 1000 + K
	float FarthestWall[2] = { 0.f, 0.f };
	for (int32 SideIndex = 0; SideIndex < 2; ++SideIndex)
	{
		const int32 Side = SideIndex == 0 ? -1 : 1;
		for (int32 K = 1; K <= Probes; ++K)
		{
			const float Offset = GroupCoverRules::ProbeOffsetCm(Side, K, Config);
			FCoverSlot Probe;
			const bool bFound = CoverTraceRules::FindShimmySlot(World, DirectSlot, DirectSlot.WallPoint + Tangent * Offset, Probe);
			const bool bSameObject = bFound && (!DirectSlot.WallActor.IsValid() || Probe.WallActor == DirectSlot.WallActor);
			const float AlongError = bFound ? CoverTraceRules::AlongWallDistance(DirectSlot, Probe.WorldLocation) - Offset : 0.f;
			const bool bOuterExposed = Side < 0 ? Probe.bLeftEdgeExposed : Probe.bRightEdgeExposed;
			const float OuterEdge = Side < 0 ? Probe.LeftEdgeDistanceCm : Probe.RightEdgeDistanceCm;
			const EGroupCoverProbe Result = Side == WrapApproachSide ? EGroupCoverProbe::NoWall
				: GroupCoverRules::ClassifyProbe(bFound, bSameObject, AlongError, bOuterExposed, OuterEdge, bFound && IsTaken(Probe.WorldLocation), Config);
			SideProbes[SideIndex].Add(Result);
			if (Result == EGroupCoverProbe::NoWall)
			{
				break;
			}
			FarthestWall[SideIndex] = Offset;
			ProbeSlots.Add(Side * 1000 + K, Probe);
		}
	}
	const TArray<float> Free = GroupCoverRules::CollectFreeOffsets(SideProbes[0], SideProbes[1], Config);
	float MeanAlong = 0.f;
	TArray<FVector2D> MemberPoints;
	for (const AOperativeCharacter* Other : Others)
	{
		MeanAlong += CoverTraceRules::AlongWallDistance(DirectSlot, Other->GetActorLocation()) / Others.Num();
		MemberPoints.Add(FVector2D(Other->GetActorLocation()));
	}
	const TArray<float> Chosen = GroupCoverRules::ChooseNearestOffsets(Free, Others.Num(), MeanAlong);
	TArray<FCoverSlot> ChosenSlots;
	TArray<FVector2D> SlotPoints;
	for (const float Offset : Chosen)
	{
		const int32 Side = Offset < 0.f ? -1 : 1;
		const int32 K = FMath::RoundToInt(FMath::Abs(Offset) / Config.SlotSpacingCm);
		if (const FCoverSlot* Found = ProbeSlots.Find(Side * 1000 + K))
		{
			ChosenSlots.Add(*Found);
			SlotPoints.Add(FVector2D(Found->WorldLocation));
		}
	}
	// Item 3 (game designer bug report 2026-11-02, "prone behind him ONLY when there is NO cover nearby"): who has no slot at the leader's
	// wall takes a free cover of another object nearby facing the same side (squad_walls.json follower_cover_radius_m / _side_deg).
	int32 NearbyCount = 0;
	if (Others.Num() > ChosenSlots.Num() && CVarGroupCoverNearbyCovers.GetValueOnGameThread() != 0)
	{
		const FSquadWallTuning& WallTuning = SquadWallTuning::Get();
		TArray<FCoverSlot> Claimed = ChosenSlots;
		Claimed.Add(DirectSlot);
		for (const FCoverSlot& Nearby : ControllerGroupCover::FindNearbyGroupCoverSlots(World, DirectSlot, Claimed, Others.Num() - ChosenSlots.Num(), WallTuning.FollowerCoverRadiusCm,
			WallTuning.FollowerCoverSideDeg, IsTaken))
		{
			ChosenSlots.Add(Nearby);
			SlotPoints.Add(FVector2D(Nearby.WorldLocation));
			++NearbyCount;
		}
	}
	// Who does not fit: prone directly behind the cover (on the navmesh, free), facing it; spares for a refused slot order.
	const int32 ProneNeeded = Others.Num() - ChosenSlots.Num();
	const FVector2D Span = GroupCoverRules::ComputeCoverSpan(DirectSlot.bLeftEdgeExposed, DirectSlot.LeftEdgeDistanceCm, DirectSlot.bRightEdgeExposed,
		DirectSlot.RightEdgeDistanceCm, FarthestWall[0], FarthestWall[1], Config);
	const TArray<FVector2D> LocalSpots = GroupCoverRules::ComputeProneSpots(ProneNeeded, Span, Others.Num() + 2, Config);
	const FVector Normal = DirectSlot.WallNormal.GetSafeNormal2D();
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	TArray<FVector> ProneSpots;
	for (const FVector2D& Local : LocalSpots)
	{
		FVector Spot = FVector(DirectSlot.WallPoint.X, DirectSlot.WallPoint.Y, DirectSlot.WorldLocation.Z) + Tangent * Local.X + Normal * Local.Y;
		if (Nav)
		{
			FNavLocation OnNav;
			if (!Nav->ProjectPointToNavigation(Spot, OnNav, FVector(60.f, 60.f, 150.f)) || FVector::Dist2D(OnNav.Location, Spot) > 40.f)
			{
				continue;
			}
			Spot = OnNav.Location;
		}
		if (!IsTaken(Spot))
		{
			ProneSpots.Add(Spot);
		}
	}
	TArray<FVector2D> PronePoints;
	for (const FVector& Spot : ProneSpots)
	{
		PronePoints.Add(FVector2D(Spot));
	}
	const TArray<FGroupCoverAssignment> Plan = GroupCoverRules::PlanAssignments(MemberPoints, SlotPoints, PronePoints);
	TArray<bool> ProneUsed;
	ProneUsed.Init(false, ProneSpots.Num());
	for (const FGroupCoverAssignment& Each : Plan)
	{
		if (Each.Kind == EGroupCoverSpotKind::Prone && ProneUsed.IsValidIndex(Each.Index))
		{
			ProneUsed[Each.Index] = true;
		}
	}

	UInteractionSubsystem* UseOrders = World->GetSubsystem<UInteractionSubsystem>();
	UVehicleSubsystem* Vehicles = World->GetSubsystem<UVehicleSubsystem>();
	UCombatFeedbackSubsystem* Feedback = World->GetSubsystem<UCombatFeedbackSubsystem>();
	const bool bNeighbours = !ChosenSlots.IsEmpty();
	const float FaceYaw = (-Normal).Rotation().Yaw;
	// Leader priority (game designer 2026-10-23): the others wait off the wall until the leader has taken his cover, then take theirs
	// (a slot taken meanwhile -> a spare of this order: the free probed slots nobody was given, then the unused prone spots).
	const bool bLeaderPriority = !bPlan && CVarGroupCoverLeaderPriority.GetValueOnGameThread() != 0;
	const TSharedRef<FGroupCoverSpares> Spares = MakeShared<FGroupCoverSpares>();
	Spares->FaceYaw = FaceYaw;
	for (const float Offset : Free)
	{
		const int32 Side = Offset < 0.f ? -1 : 1;
		const int32 K = FMath::RoundToInt(FMath::Abs(Offset) / Config.SlotSpacingCm);
		const FCoverSlot* Found = ProbeSlots.Find(Side * 1000 + K);
		if (Found && !Chosen.Contains(Offset))
		{
			Spares->Slots.Add(*Found);
			Spares->SlotUsed.Add(false);
		}
	}
	// Into a slot: the same order as the single one (OrderTakeCover / the planned walk + SetPendingCover), always a run.
	// The wall-edge cover switch (2026-10-20): the direct order's leader dashes in through the run-in clip, the others follow the
	// group cover rules at that wall.
	auto SendToSlot = [&](AOperativeCharacter* Member, const FCoverSlot& MemberSlot, bool bPrimary)
	{
		const bool bMemberSwitch = bPrimary && bLeaderSwitch;
		const FCoverWrapPlan* MemberWrap = bPrimary ? LeaderWrap : nullptr;
		if (bPlan)
		{
			Member->SetPendingCover(MemberSlot);
			Member->SetPendingCoverSwitch(bMemberSwitch);
			if (MemberWrap)
			{
				Member->SetPendingCoverWrap(*MemberWrap);
			}
			const FVector Planned = Squad->PlanMove(Member, MemberSlot.WorldLocation, true, Radius);
			if (Feedback)
			{
				Feedback->SpawnWaypointMarker(Planned);
			}
		}
		else if (!bPrimary && bLeaderPriority && Squad->OrderGroupCoverWait(Member, MemberSlot, Direct, bNeighbours, Spares))
		{
			if (Feedback)
			{
				Feedback->SpawnMovePing(MemberSlot.WorldLocation, false);
			}
			return true; // into the slot once the leader is in cover (USquadSubsystem::UpdateGroupCoverWait)
		}
		else
		{
			const EOperativeOrderResult MemberResult = MemberWrap ? Member->OrderCoverWrap(*MemberWrap)
				: bMemberSwitch ? Member->OrderCoverSwitchResolved(MemberSlot, Member->GetCoverSwitchArrivalFacing(MemberSlot)) : Member->OrderTakeCover(MemberSlot, true);
			if (MemberResult != EOperativeOrderResult::Accepted)
			{
				return false;
			}
			if (Feedback)
			{
				Feedback->SpawnMovePing(MemberSlot.WorldLocation, bPrimary);
			}
		}
		Member->SetHoldGroupCoverSlot(bNeighbours);
		return true;
	};
	auto SendProne = [&](AOperativeCharacter* Member, int32 SpotIndex)
	{
		FVector Destination;
		if (!ProneSpots.IsValidIndex(SpotIndex) || !Squad->OrderGroupCoverProne(Member, ProneSpots[SpotIndex], FaceYaw, true, bPlan, Radius, Destination))
		{
			return false;
		}
		if (Feedback)
		{
			if (bPlan)
			{
				Feedback->SpawnWaypointMarker(Destination);
			}
			else
			{
				Feedback->SpawnMovePing(Destination, false);
			}
		}
		return true;
	};
	auto NextSpareSpot = [&ProneUsed]()
	{
		for (int32 Index = 0; Index < ProneUsed.Num(); ++Index)
		{
			if (!ProneUsed[Index])
			{
				ProneUsed[Index] = true;
				return Index;
			}
		}
		return static_cast<int32>(INDEX_NONE);
	};

	if (Vehicles)
	{
		Vehicles->CancelBoarding(Direct);
	}
	if (!SendToSlot(Direct, DirectSlot, true))
	{
		LastGroupCoverSlots = 0;
		LastGroupCoverProne = 0;
		if (UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>())
		{
			Messages->PostMessage(LOCTEXT("HQ", "HQ"), LOCTEXT("CoverUnreachable", "⚠️ No path to this cover."));
		}
		return true; // the group follows the direct order: no cover for him, no order for them
	}
	int32 InSlots = 1;
	int32 Prone = 0;
	for (int32 Index = 0; Index < Others.Num(); ++Index)
	{
		AOperativeCharacter* Member = Others[Index];
		if (UseOrders)
		{
			UseOrders->CancelUseOrder(Member); // a cover order replaces a planned / running "Ignite"
		}
		if (Vehicles)
		{
			Vehicles->CancelBoarding(Member);
		}
		Member->CancelQuickFire();
		Member->CancelSuppressFire(TEXT("a group cover order"));
		const FGroupCoverAssignment& Each = Plan[Index];
		bool bSent = false;
		if (Each.Kind == EGroupCoverSpotKind::Slot && ChosenSlots.IsValidIndex(Each.Index))
		{
			bSent = SendToSlot(Member, ChosenSlots[Each.Index], false);
			InSlots += bSent ? 1 : 0;
			if (!bSent)
			{
				bSent = SendProne(Member, NextSpareSpot()); // no way to that slot: prone behind the cover instead
				Prone += bSent ? 1 : 0;
			}
		}
		else
		{
			bSent = SendProne(Member, Each.Index != INDEX_NONE ? Each.Index : NextSpareSpot());
			Prone += bSent ? 1 : 0;
		}
		if (!bSent)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[GroupCover] %s: no place at that cover (refused / unreachable)"), *Member->DisplayName.ToString());
		}
	}
	for (int32 Index = 0; Index < ProneSpots.Num(); ++Index)
	{
		Spares->ProneSpots.Add(ProneSpots[Index]);
		Spares->ProneUsed.Add(ProneUsed.IsValidIndex(Index) && ProneUsed[Index]);
	}
	LastGroupCoverSlots = InSlots;
	LastGroupCoverProne = Prone;
	LastGroupCoverNearby = NearbyCount;
	UE_LOG(LogCodexTactics, Display, TEXT("[GroupCover] %s + %d: %d in the cover (%d free slots, %d probes per side, %d nearby covers), %d prone behind it%s%s; span %.0f..%.0f cm"),
		*Direct->DisplayName.ToString(), Others.Num(), InSlots, Free.Num(), Probes, NearbyCount, Prone, bPlan ? TEXT(" (planned)") : TEXT(""),
		bLeaderSwitch ? TEXT(", the leader by the cover switch") : LeaderWrap ? TEXT(", the leader round the object along its wall") : TEXT(""), Span.X, Span.Y);

	ControllerCommands::Report(this, LeaderWrap ? PlayerCommands::CoverShimmy : PlayerCommands::CoverEnter, nullptr, bPlan, true, Direct, DirectSlot.WorldLocation);
	if (bLeaderSwitch)
	{
		ControllerCommands::Report(this, PlayerCommands::CoverSwitch, nullptr, bPlan, true, Direct, DirectSlot.WorldLocation);
	}
	if (DirectSlot.HasExposedEdge())
	{
		ControllerCommands::Report(this, PlayerCommands::CoverCorner, nullptr, bPlan, true, Direct, DirectSlot.WorldLocation);
	}
	if (UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>())
	{
		const FText Line = Prone > 0
			? FText::Format(bPlan ? LOCTEXT("GroupCoverPlannedProne", "📋 [PLAN] Group into cover: {0} at the wall, {1} prone behind it!")
				: LOCTEXT("GroupCoverProne", "🏃 Group into cover: {0} at the wall, {1} prone behind it!"), InSlots, Prone)
			: FText::Format(bPlan ? LOCTEXT("GroupCoverPlanned", "📋 [PLAN] Group into cover ({0} operatives)!")
				: LOCTEXT("GroupCoverOrdered", "🏃 Group into cover ({0} operatives)!"), InSlots);
		Messages->PostMessage(LOCTEXT("SquadSender", "SQUAD"), Line);
	}
	// Rules v2.2 (Codex.Formation.Sectors, group mode): the leader's cover facing / corner aim at the new wall is his own; the others get
	// the looks that close the open arc (FormationWallGroup) - not all the same way as he (user playtest 2026-10-25). Real time only.
	if (!bPlan)
	{
		Squad->ReallocateWallGroup(Direct, Others, bLeaderSwitch ? TEXT("the group cover order: the cover switch")
			: LeaderWrap ? TEXT("the group cover order: the wrap round the object") : TEXT("the group cover order: the cover slots"));
	}
	return true;
}

void ACodexTacticsPlayerController::UpdateCoverSwitchPath(const AOperativeCharacter* Operative, const FVector& To)
{
	CoverSwitchPathPoints.Reset();
	if (!Operative)
	{
		return;
	}
	const FVector Feet = Operative->GetActorLocation() - FVector(0.f, 0.f, Operative->GetSimpleCollisionHalfHeight());
	if (const UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(), Feet, To, const_cast<AOperativeCharacter*>(Operative));
		Path && Path->IsValid() && Path->PathPoints.Num() >= 2)
	{
		CoverSwitchPathPoints = Path->PathPoints;
		CoverSwitchPathPoints[0] = Feet;
		return;
	}
	CoverSwitchPathPoints = { Feet, To };
}

bool ACodexTacticsPlayerController::GetCoverPreviewPath(TArray<FVector>& OutPoints) const
{
	OutPoints.Reset();
	if (IsCoverWrapHoverShown())
	{
		OutPoints = CoverWrapHoverPlan.Waypoints;
		return OutPoints.Num() >= 2;
	}
	FVector From;
	FVector To;
	if (!GetCoverSwitchPreviewLine(From, To))
	{
		return false;
	}
	// The dash's navmesh path when it is for this target (diagonal from his corner to the other cover's corner); else the straight line.
	if (CoverSwitchPathPoints.Num() >= 2 && FVector::Dist2D(CoverSwitchPathPoints.Last(), To) < 30.f && FVector::Dist2D(CoverSwitchPathPoints[0], From) < 30.f)
	{
		OutPoints = CoverSwitchPathPoints;
		OutPoints[0] = From;
	}
	else
	{
		OutPoints = { From, To };
	}
	return true;
}

bool ACodexTacticsPlayerController::TryOrderCoverWrap(AOperativeCharacter* Operative, const FVector& Point)
{
	UWorld* World = GetWorld();
	if (!Operative || !World || !Operative->bInCover || GetActiveTurnBased())
	{
		return false;
	}
	if (const UTutorialSubsystem* Tutorial = World->GetSubsystem<UTutorialSubsystem>();
		Tutorial && (Tutorial->IsCommandLocked(PlayerCommands::CoverPreview) || Tutorial->IsCommandLocked(PlayerCommands::CoverShimmy)))
	{
		return false; // not taught yet: the click keeps its ordinary meaning
	}
	FCoverWrapPlan Plan;
	LastCoverWrapVerdict = Operative->PlanCoverWrap(Point, Plan);
	if (LastCoverWrapVerdict != ECoverWrapVerdict::Wrap)
	{
		UE_LOG(LogCodexTactics, Verbose, TEXT("[Cover] %s: click round the cover object - %s"), *Operative->DisplayName.ToString(), CoverWrapRules::VerdictName(LastCoverWrapVerdict));
		return false;
	}
	HideCoverPreview();
	UpdateCoverWrapHover(nullptr, nullptr);
	UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>();
	UCombatFeedbackSubsystem* Feedback = World->GetSubsystem<UCombatFeedbackSubsystem>();
	const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
	const ECombatOrderDispatch Dispatch = Flow ? FCombatTimeModeRules::GetOrderDispatch(Flow->GetPhase(), Flow->GetCombatMode(),
		OrderLock::CVarRealTimeOrders.GetValueOnGameThread() != 0) : ECombatOrderDispatch::Execute;
	const bool bPlan = Dispatch == ECombatOrderDispatch::Queue;
	const float Radius = Flow ? Flow->GetPauseOrderRadius() : 1200.f;
	UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: click round the cover object -> along its wall, %d corner(s), %.0f cm%s"), *Operative->DisplayName.ToString(),
		Plan.Path.CornerCount(), Plan.Path.LengthCm, bPlan ? TEXT(" (planned)") : TEXT(""));
	// Round a corner with others along (a box selection / group mode): the others take the destination face by the group cover rules. Along
	// his own face it stays his shimmy alone (as a click along his wall).
	if (Plan.Path.CornerCount() > 0 && OrderGroupCover(Operative, Plan.GoalSlot, bPlan, Radius, false, &Plan))
	{
		return true;
	}
	if (bPlan)
	{
		Operative->SetPendingCover(Plan.GoalSlot);
		Operative->SetPendingCoverWrap(Plan);
		const FVector Planned = GetSquad() ? GetSquad()->PlanMove(Operative, Plan.GoalSlot.WorldLocation, false, Radius) : Plan.GoalSlot.WorldLocation;
		if (Feedback)
		{
			Feedback->SpawnWaypointMarker(Planned);
		}
		if (Messages)
		{
			Messages->PostMessage(Operative->DisplayName, LOCTEXT("CoverWrapPlanned", "📋 [PLAN] Along the wall, round the corner!"));
		}
		ControllerCommands::Report(this, PlayerCommands::CoverShimmy, nullptr, true, false, Operative, Plan.GoalSlot.WorldLocation);
		return true;
	}
	const EOperativeOrderResult Result = Operative->OrderCoverWrap(Plan);
	if (Result == EOperativeOrderResult::Accepted)
	{
		ControllerCommands::Report(this, PlayerCommands::CoverShimmy, nullptr, false, false, Operative, Plan.GoalSlot.WorldLocation);
		if (Feedback)
		{
			Feedback->SpawnMovePing(Plan.GoalSlot.WorldLocation, false);
		}
		if (Messages && Plan.Path.CornerCount() > 0)
		{
			Messages->PostMessage(Operative->DisplayName, LOCTEXT("CoverWrapOrdered", "🧱 Along the wall, round the corner."));
		}
	}
	else if (Messages)
	{
		Messages->PostMessage(LOCTEXT("HQ", "HQ"), LOCTEXT("CoverWrapRefused", "⚠️ Can't get round there along the wall."));
	}
	return true;
}

bool ACodexTacticsPlayerController::TryHandleCoverWrapClick(const FHitResult& Hit)
{
	USquadSubsystem* Squad = GetSquad();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	AActor* HitActor = Hit.GetActor();
	if (!Leader || !Leader->bInCover || Leader->bShimmying || IsRealTimeOrderLocked() || bRelocateSelectMode || GetActiveTurnBased())
	{
		return false;
	}
	// Bodies, enemies and objects keep their meaning; another object's wall is a cover click (handled before).
	if (HitActor && (HitActor->IsA<APawn>() || HitActor->IsA<AInteractableActor>() || HitActor->ActorHasTag(FName(TEXT("Enemy"))) || FindClickedMember(Hit)))
	{
		return false;
	}
	FCoverSlot Own = Leader->GetCoverSlot();
	const bool bOwnObject = HitActor && ((Own.WallComponent.IsValid() && Hit.GetComponent() == Own.WallComponent.Get()) || (!Own.WallComponent.IsValid() && HitActor == Own.WallActor.Get()));
	if (FMath::Abs(Hit.ImpactNormal.Z) <= 0.3f && !bOwnObject)
	{
		return false;
	}
	return TryOrderCoverWrap(Leader, Hit.ImpactPoint);
}

bool ACodexTacticsPlayerController::UpdateCoverWrapHover(AOperativeCharacter* Operative, const FHitResult* Hit)
{
	bool bShow = false;
	AActor* HitActor = Hit ? Hit->GetActor() : nullptr;
	const UTutorialSubsystem* Tutorial = GetWorld()->GetSubsystem<UTutorialSubsystem>();
	if (Operative && Hit && Operative->bInCover && !Operative->bShimmying && !Operative->IsCoverWrapping() && !GetActiveTurnBased() && !HasCoverPreview()
		&& !bCornerPeekCursor && !IsCoverSwitchHoverShown()
		&& !(Tutorial && (Tutorial->IsCommandLocked(PlayerCommands::CoverPreview) || Tutorial->IsCommandLocked(PlayerCommands::CoverShimmy)))
		&& !(HitActor && (HitActor->IsA<APawn>() || HitActor->IsA<AInteractableActor>() || HitActor->ActorHasTag(FName(TEXT("Enemy"))))))
	{
		const FCoverSlot& Own = Operative->GetCoverSlot();
		const bool bOwnObject = HitActor && ((Own.WallComponent.IsValid() && Hit->GetComponent() == Own.WallComponent.Get())
			|| (!Own.WallComponent.IsValid() && HitActor == Own.WallActor.Get()));
		if (bOwnObject || FMath::Abs(Hit->ImpactNormal.Z) > 0.3f)
		{
			// The plan traces only when the cursor moved (or he moved): cached while it rests.
			if (HitActor != WrapHoverActor.Get() || Operative != CoverWrapHoverOperative.Get() || FVector::DistSquared(Hit->ImpactPoint, WrapHoverPoint) > FMath::Square(5.f)
				|| FVector::DistSquared2D(Operative->GetActorLocation(), WrapHoverSlotLocation) > FMath::Square(5.f))
			{
				WrapHoverActor = HitActor;
				CoverWrapHoverOperative = Operative;
				WrapHoverPoint = Hit->ImpactPoint;
				WrapHoverSlotLocation = Operative->GetActorLocation();
				// Round a corner only: along his own face the click is the plain shimmy (no preview, as a click on his wall).
				bWrapHoverValid = Operative->PlanCoverWrap(Hit->ImpactPoint, CoverWrapHoverPlan) == ECoverWrapVerdict::Wrap && CoverWrapHoverPlan.Path.CornerCount() > 0;
			}
			bShow = bWrapHoverValid;
		}
	}
	if (!bShow)
	{
		if (CoverWrapGhost && CoverWrapGhost->IsShown())
		{
			CoverWrapGhost->Hide();
		}
		bCoverWrapHoverShown = false;
		return false;
	}
	if (!CoverWrapGhost)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		CoverWrapGhost = GetWorld()->SpawnActor<ACoverGhostActor>(CoverWrapHoverPlan.GoalSlot.WorldLocation, FRotator::ZeroRotator, Params);
	}
	if (CoverWrapGhost && (!CoverWrapGhost->IsShown() || CoverWrapGhost->GetOperative() != Operative || CoverWrapGhost->GetShownFacing() != CoverWrapHoverPlan.GoalFacing
		|| FVector::DistSquared2D(CoverWrapGhost->GetSlot().WorldLocation, CoverWrapHoverPlan.GoalSlot.WorldLocation) > 1.f))
	{
		CoverWrapGhost->ShowForFacing(*Operative, CoverWrapHoverPlan.GoalSlot, CoverWrapHoverPlan.GoalFacing);
	}
	bCoverWrapHoverShown = true;
	return true;
}

bool ACodexTacticsPlayerController::IsCoverWrapHoverShown() const
{
	return bCoverWrapHoverShown && CoverWrapGhost && CoverWrapGhost->IsShown();
}

bool ACodexTacticsPlayerController::UpdateSquadMoveHover(const FHitResult* Hit)
{
	// Bug C (game designer 2026-10-26): the single operative in cover gets the wrap preview (UpdateCoverWrapHover); the squad in the
	// formation wall layout stands in no cover slot, so it had no guide line at all - its own preview: USquadSubsystem::PreviewFormationMove.
	USquadSubsystem* Squad = GetSquad();
	const AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	AActor* HitActor = Hit ? Hit->GetActor() : nullptr;
	const bool bGround = Hit && FMath::Abs(Hit->ImpactNormal.Z) > 0.7f && !(HitActor && (HitActor->IsA<APawn>() || HitActor->IsA<AInteractableActor>()
		|| HitActor->ActorHasTag(FName(TEXT("Enemy")))));
	if (!bGround || !Leader || Leader->bInCover || GetActiveTurnBased() || HasCoverPreview() || IsCoverWrapHoverShown() || IsCoverSwitchHoverShown()
		|| bCornerPeekCursor || bRelocateSelectMode || IsRealTimeOrderLocked())
	{
		SquadMovePreviewPaths.Reset();
		SquadMoveHoverPoint = FVector(TNumericLimits<float>::Max());
		bSquadMoveHoverShown = false;
		return false;
	}
	// The paths trace the nav mesh only when the cursor or the leader moved (cached while both rest).
	if (FVector::DistSquared(Hit->ImpactPoint, SquadMoveHoverPoint) > FMath::Square(5.f)
		|| FVector::DistSquared2D(Leader->GetActorLocation(), SquadMoveHoverLeaderAt) > FMath::Square(5.f))
	{
		SquadMoveHoverPoint = Hit->ImpactPoint;
		SquadMoveHoverLeaderAt = Leader->GetActorLocation();
		Squad->PreviewFormationMove(Hit->ImpactPoint, SquadMovePreviewPaths);
	}
	bSquadMoveHoverShown = !SquadMovePreviewPaths.IsEmpty();
	return bSquadMoveHoverShown;
}

bool ACodexTacticsPlayerController::GetSquadMovePreviewPaths(TArray<TArray<FVector>>& OutPaths) const
{
	OutPaths = SquadMovePreviewPaths;
	return bSquadMoveHoverShown && !OutPaths.IsEmpty();
}

void ACodexTacticsPlayerController::ToggleCoverFireModeKey()
{
	if (IsDialogueOpen() || RefuseIfLocked(PlayerCommands::CoverFireMode))
	{
		return;
	}
	USquadSubsystem* Squad = GetSquad();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (!Leader)
	{
		return;
	}
	if (UTurnBasedCombatSubsystem* TurnBased = GetActiveTurnBased())
	{
		if (AOperativeCharacter* Active = TurnBased->GetActiveUnit())
		{
			Leader = Active;
		}
	}
	TArray<AOperativeCharacter*> Targets = Squad->HasMultiSelection() ? Squad->GetSelectedGroup() : TArray<AOperativeCharacter*>{ Leader };
	ECoverFireMode Mode = ECoverFireMode::CornerLean;
	for (AOperativeCharacter* Operative : Targets)
	{
		if (Operative)
		{
			Mode = Operative->ToggleCoverFireMode();
		}
	}
	ControllerCommands::Report(this, PlayerCommands::CoverFireMode, nullptr, false, Targets.Num() > 1);
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(Leader->DisplayName, Mode == ECoverFireMode::BlindFire
			? LOCTEXT("CoverBlind", "🙈 From cover - blind fire (accuracy −40%, head stays down).")
			: LOCTEXT("CoverLean", "👁️ From cover - aimed fire around the corner."));
	}
}

#undef LOCTEXT_NAMESPACE
