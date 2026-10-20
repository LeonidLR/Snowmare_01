// ACodexTacticsPlayerController: cover clicks - cover ghost (find slot, show / hide / confirm), corner peek hover,
// cover fire mode key.
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
#include "Tactics/CoverTraceRules.h"
#include "Tactics/GroupCoverRules.h"
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
#include "Vehicles/VehiclePawn.h"
#include "Vehicles/VehicleSubsystem.h"

#define LOCTEXT_NAMESPACE "CodexTacticsPlayerController"

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
	// Manual corner peek (user request 2026-10-13, rev 5c; low cover too since rev 5e): a click on the END of his own wall (its last ~75 cm,
	// its end face or its top there) sends him to that corner, leaning out in the corner-aim pose until another order.
	// Rev 5d (user bug 2026-10-14): decided BEFORE the "a click near an operative picks him" proximity rule - the corner he
	// already stands at lies inside SelectRadius, so the near-edge click selected him and nothing happened (the hover, which
	// has no such rule, showed the peek ghost). A click on a body (the hit actor is a pawn) never reaches this.
	{
		ECoverFacing PeekSide = ECoverFacing::Right;
		float PeekEdge = 0.f;
		const UTutorialSubsystem* PeekTutorial = GetWorld()->GetSubsystem<UTutorialSubsystem>();
		const bool bPeekEdge = !bAlt && Operative->IsInPeekableCover() && !(PeekTutorial && PeekTutorial->IsCommandLocked(PlayerCommands::CoverCornerPeek))
			&& !HitActor->IsA<APawn>() && FindCornerPeekForHit(Operative, Hit, PeekSide, PeekEdge);
		if (CoverFacingRules::CornerPeekClickWins(bPeekEdge, HitActor->IsA<AOperativeCharacter>()))
		{
			HideCoverPreview();
			UpdateCornerPeekHover(Operative, nullptr);
			const EOperativeOrderResult Result = Operative->OrderCornerPeek(PeekSide, PeekEdge);
			UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>();
			if (Result == EOperativeOrderResult::Accepted)
			{
				ControllerCommands::Report(this, PlayerCommands::CoverCornerPeek, nullptr, false, false, Operative, Hit.ImpactPoint);
				if (Messages)
				{
					Messages->PostMessage(Operative->DisplayName, LOCTEXT("CornerPeek", "🎯 Covering the corner."));
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
	// A floor / ramp is a move target, not a wall (Alt + click probes anyway: a wall edge may have been clicked); the
	// vehicle's roof is always its hull.
	if (FMath::Abs(Hit.ImpactNormal.Z) > 0.3f && !bAlt && !bVehicle)
	{
		return false;
	}
	FCoverSlot Slot;
	if (!FindCoverSlotForHit(Hit, Slot))
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
	// Second click on the previewed slot: confirm.
	if (HasCoverPreview() && CoverPreviewOperative.Get() == Operative && FVector::Dist2D(Slot.WorldLocation, CoverPreviewSlot.WorldLocation) <= 100.f)
	{
		ConfirmCoverPreview();
		return true;
	}
	// Along his own wall: shimmy at once (no preview).
	if (!TurnBased && Operative->bInCover && CoverTraceRules::IsSameWall(Operative->GetCoverSlot(), Slot))
	{
		HideCoverPreview();
		FCoverSlot Shimmy;
		if (CoverTraceRules::FindShimmySlot(GetWorld(), Operative->GetCoverSlot(), Hit.ImpactPoint, Shimmy))
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
	const FCoverSlot& Slot = Operative->GetCoverSlot();
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

bool ACodexTacticsPlayerController::IsCornerPeekPreviewShown() const
{
	return CornerPeekGhost && CornerPeekGhost->IsShown();
}

bool ACodexTacticsPlayerController::UpdateCornerPeekHover(AOperativeCharacter* Operative, const FHitResult* Hit)
{
	bool bEdge = false;
	const UTutorialSubsystem* Tutorial = GetWorld()->GetSubsystem<UTutorialSubsystem>();
	if (Operative && Hit && Operative->IsInPeekableCover() && !Operative->bShimmying
		&& !(Tutorial && Tutorial->IsCommandLocked(PlayerCommands::CoverCornerPeek)))
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
		}
		bEdge = bPeekHoverEdge;
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
	if (CornerPeekGhost)
	{
		// The corner stand point: the cover slot moved along the wall to the stand-off from that edge.
		FCoverSlot Corner = Operative->GetCoverSlot();
		const float StandOff = CoverFacingRules::CornerStandOff(PeekHoverSide, Operative->GetStance() == EOperativeStance::Crouching
			|| Operative->CurrentCoverHeight == ECoverHeight::LowCover, Operative->GetCoverFacingConfig());
		const FVector Shift = Corner.RightTangent() * CoverFacingRules::CornerPeekStandAlong(PeekHoverSide, PeekHoverEdgeAlong, StandOff);
		Corner.WorldLocation += Shift;
		Corner.WallPoint += Shift;
		if (!CornerPeekGhost->IsShown() || !CornerPeekGhost->IsCornerPeekPose()
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
	CoverGhost->ShowFor(*Operative, Slot);
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
}

void ACodexTacticsPlayerController::ConfirmCoverPreview()
{
	AOperativeCharacter* Operative = CoverPreviewOperative.Get();
	const FCoverSlot Slot = CoverPreviewSlot;
	HideCoverPreview();
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
	// Game designer 2026-10-20: a box selection / group mode takes cover together (the others beside him or prone behind it).
	if (OrderGroupCover(Operative, Slot, Dispatch == ECombatOrderDispatch::Queue, Flow ? Flow->GetPauseOrderRadius() : 1200.f))
	{
		return;
	}
	if (Dispatch == ECombatOrderDispatch::Queue)
	{
		// Tactical pause: the walk is planned, the slot entered on arrival.
		Operative->SetPendingCover(Slot);
		const FVector Planned = GetSquad()->PlanMove(Operative, Slot.WorldLocation, true, Flow ? Flow->GetPauseOrderRadius() : 1200.f);
		if (Feedback)
		{
			Feedback->SpawnWaypointMarker(Planned);
		}
		if (Messages)
		{
			Messages->PostMessage(Operative->DisplayName, LOCTEXT("CoverPlanned", "📋 [PLAN] Take cover at the wall!"));
		}
		ControllerCommands::Report(this, PlayerCommands::CoverEnter, nullptr, true, false, Operative, Slot.WorldLocation);
		if (Slot.HasExposedEdge())
		{
			ControllerCommands::Report(this, PlayerCommands::CoverCorner, nullptr, true, false, Operative, Slot.WorldLocation);
		}
		return;
	}
	if (UVehicleSubsystem* Vehicles = GetWorld()->GetSubsystem<UVehicleSubsystem>())
	{
		Vehicles->CancelBoarding(Operative);
	}
	const EOperativeOrderResult Result = Operative->OrderTakeCover(Slot, true);
	if (Result == EOperativeOrderResult::Accepted)
	{
		ControllerCommands::Report(this, PlayerCommands::CoverEnter, nullptr, false, false, Operative, Slot.WorldLocation);
		if (Slot.HasExposedEdge())
		{
			ControllerCommands::Report(this, PlayerCommands::CoverCorner, nullptr, false, false, Operative, Slot.WorldLocation);
		}
		if (Feedback)
		{
			Feedback->SpawnMovePing(Slot.WorldLocation, true);
		}
		if (Messages)
		{
			Messages->PostMessage(Operative->DisplayName, LOCTEXT("CoverOrdered", "🏃 Take cover!"));
		}
	}
	else if (Messages && Result == EOperativeOrderResult::Unreachable)
	{
		Messages->PostMessage(LOCTEXT("HQ", "HQ"), LOCTEXT("CoverUnreachable", "⚠️ No path to this cover."));
	}
}

bool ACodexTacticsPlayerController::OrderGroupCover(AOperativeCharacter* Direct, const FCoverSlot& Slot, bool bPlan, float Radius)
{
	USquadSubsystem* Squad = GetSquad();
	UWorld* World = GetWorld();
	if (!Squad || !Direct || !World || !Slot.IsValid() || GetActiveTurnBased())
	{
		return false;
	}
	const TArray<AOperativeCharacter*> Others = Squad->GetGroupCoverMembers(Direct);
	if (!SquadOrderRules::IsGroupCoverOrder(Others.Num()))
	{
		return false;
	}
	const FGroupCoverConfig Config;
	// The direct order's slot: the clicked one. On a high wall the corner he would walk to by himself right after entering
	// (TrySnapToCoverCorner) is taken at once - the corner is his, and nobody is placed on that walk.
	FCoverSlot DirectSlot = Slot;
	if (Slot.Height == ECoverHeight::HighCover)
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
			const EGroupCoverProbe Result = GroupCoverRules::ClassifyProbe(bFound, bSameObject, AlongError, bOuterExposed, OuterEdge,
				bFound && IsTaken(Probe.WorldLocation), Config);
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
	// Into a slot: the same order as the single one (OrderTakeCover / the planned walk + SetPendingCover), always a run.
	auto SendToSlot = [&](AOperativeCharacter* Member, const FCoverSlot& MemberSlot, bool bPrimary)
	{
		if (bPlan)
		{
			Member->SetPendingCover(MemberSlot);
			const FVector Planned = Squad->PlanMove(Member, MemberSlot.WorldLocation, true, Radius);
			if (Feedback)
			{
				Feedback->SpawnWaypointMarker(Planned);
			}
		}
		else
		{
			if (Member->OrderTakeCover(MemberSlot, true) != EOperativeOrderResult::Accepted)
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
	LastGroupCoverSlots = InSlots;
	LastGroupCoverProne = Prone;
	UE_LOG(LogCodexTactics, Display, TEXT("[GroupCover] %s + %d: %d in the cover (%d free slots, %d probes per side), %d prone behind it%s; span %.0f..%.0f cm"),
		*Direct->DisplayName.ToString(), Others.Num(), InSlots, Free.Num(), Probes, Prone, bPlan ? TEXT(" (planned)") : TEXT(""), Span.X, Span.Y);

	ControllerCommands::Report(this, PlayerCommands::CoverEnter, nullptr, bPlan, true, Direct, DirectSlot.WorldLocation);
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
	return true;
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
