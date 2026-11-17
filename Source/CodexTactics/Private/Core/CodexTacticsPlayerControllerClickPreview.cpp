// ACodexTacticsPlayerController: R17-3, the unified building click (designer request 2026-11-17, his decisions the same evening; TASK_BOARD R17-3,
// docs/design/CLICK_PREVIEW.md). In Solo, Solo with a box selection and Formation alike: hovering a surface shows nothing; the FIRST click on a
// wall / corner / obstacle top / doorway opening shows the ghost that fits it - with every member who goes along (the group cover / formation
// wall layout / corner stack / breach stack placement the order itself uses); the cursor over the same building moves it (along the wall, the
// corner's peek); the SECOND click on that building carries it out; a click elsewhere drops it and is the ordinary click (HandleWorldHit); a
// double click on an obstacle runs there and climbs. The pure decisions are ClickPreviewRules; the ghosts and orders are the existing ones
// (ShowCoverPreview / ConfirmCoverPreview, UpdateCornerPeekHover + TryHandleCoverClick, UpdateMantleHover + OrderMantleTop, the breach file).
// Godot reference: none (UE-only).

#include "Core/CodexTacticsPlayerController.h"
#include "Core/CodexTacticsPlayerControllerInternal.h"
#include "Camera/InteriorExitArrowActor.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/EnemyGhostActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Formation/FormationRuntimeRules.h"
#include "Formation/FormationTypes.h"
#include "Formation/FormationWallEndRules.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/InteractableActor.h"
#include "Interactables/MantleNavigation.h"
#include "Interactables/TripwireActor.h"
#include "Ladders/LadderActor.h"
#include "Misc/App.h"
#include "Tactics/ClickPreviewRules.h"
#include "Tactics/CoverGhostActor.h"
#include "Tactics/CoverTraceRules.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "Tactics/WallSideRules.h"
#include "Tutorial/TutorialSubsystem.h"
#include "UI/GameMessageSubsystem.h"
#include "Vehicles/VehiclePawn.h"
#include "Vehicles/VehicleSubsystem.h"

#define LOCTEXT_NAMESPACE "CodexTacticsPlayerController"

namespace ControllerClickPreviewLocal
{
	TAutoConsoleVariable<int32> CVarClickPreview(TEXT("Codex.Click.Preview"), 1,
		TEXT("R17-3 (designer 2026-11-17): 1 (default) = one building click in every mode - hovering shows nothing, the first click on a wall / corner / obstacle / ")
		TEXT("doorway shows the ghost (the whole group), the cursor over the same building moves it, the second click there does it, a ground click cancels; ")
		TEXT("0 = before (hover ghosts; a corner / climb click acts at once) - A/B."));
	TAutoConsoleVariable<int32> CVarLowerHighHits(TEXT("Codex.Click.LowerHighHits"), 1,
		TEXT("R17-3 (\"it works on some buildings, not on others\"): 1 (default) = a cursor hit high on a wall face (above the operative's chest: a tall / ")
		TEXT("two-storey building) is the same face at his chest height for every building click; 0 = the raw hit (the cover / peek probes missed such walls) - A/B."));

	/** The members' silhouettes are re-planned when the leader's slot moved this far (the group placement traces a lot), cm. */
	constexpr float GroupReplanCm = 25.f;
}

bool ACodexTacticsPlayerController::IsClickPreviewEnabled()
{
	return ControllerClickPreviewLocal::CVarClickPreview.GetValueOnGameThread() != 0;
}

FClickPreviewPiece ACodexTacticsPlayerController::MakeClickPiece(const FHitResult& Hit)
{
	FClickPreviewPiece Piece;
	const UPrimitiveComponent* Component = Hit.GetComponent();
	const AActor* Actor = Hit.GetActor();
	Piece.ComponentId = Component ? static_cast<uint64>(Component->GetUniqueID()) + 1 : 0;
	Piece.ActorId = Actor ? static_cast<uint64>(Actor->GetUniqueID()) + 1 : 0;
	if (Actor)
	{
		for (const FName& Tag : Actor->Tags)
		{
			if (Tag.ToString().StartsWith(TEXT("building=")))
			{
				Piece.Building = Tag;
				break;
			}
		}
	}
	Piece.Bounds = Component ? Component->Bounds.GetBox() : (Actor ? Actor->GetComponentsBoundingBox() : FBox(ForceInit));
	Piece.bFloorLike = FMath::Abs(Hit.ImpactNormal.Z) >= ClickPreviewRules::FloorNormalZ;
	return Piece;
}

FHitResult ACodexTacticsPlayerController::LowerBuildingHit(const FHitResult& Hit) const
{
	const USquadSubsystem* Squad = GetSquad();
	const AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	UWorld* World = GetWorld();
	const AActor* HitActor = Hit.GetActor();
	if (ControllerClickPreviewLocal::CVarLowerHighHits.GetValueOnGameThread() == 0 || !Leader || !World || !HitActor || HitActor->IsA<APawn>()
		|| HitActor->IsA<AInteractableActor>() || HitActor->IsA<ACoverGhostActor>())
	{
		return Hit;
	}
	const float FeetZ = static_cast<float>(Leader->GetActorLocation().Z) - Leader->GetSimpleCollisionHalfHeight();
	if (!ClickPreviewRules::ShouldLowerWallHit(static_cast<float>(Hit.ImpactPoint.Z), static_cast<float>(Hit.ImpactNormal.Z), FeetZ))
	{
		return Hit;
	}
	const FVector Normal = FVector(Hit.ImpactNormal.X, Hit.ImpactNormal.Y, 0.f).GetSafeNormal();
	if (Normal.IsNearlyZero())
	{
		return Hit;
	}
	const FVector Start(Hit.ImpactPoint.X + Normal.X * ClickPreviewRules::LowHitOutCm, Hit.ImpactPoint.Y + Normal.Y * ClickPreviewRules::LowHitOutCm,
		FeetZ + ClickPreviewRules::LowHitChestCm);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ClickPreviewLowerHit), false);
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		if (!It->IsA<AVehiclePawn>())
		{
			Params.AddIgnoredActor(*It);
		}
	}
	FHitResult Low;
	if (World->LineTraceSingleByChannel(Low, Start, Start - Normal * ClickPreviewRules::LowHitReachCm, ECC_Visibility, Params) && !Low.bStartPenetrating
		&& ClickPreviewRules::IsSameFaceLower(FVector2D(Normal), FVector2D(Low.ImpactNormal), static_cast<float>(Low.ImpactNormal.Z)))
	{
		return Low;
	}
	return Hit;
}

bool ACodexTacticsPlayerController::IsClickPreviewBypass(const FHitResult& Hit) const
{
	const USquadSubsystem* Squad = GetSquad();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	const AActor* HitActor = Hit.GetActor();
	if (!Leader || !HitActor || IsAttackModifierDown() || IsShiftModifierDown() || IsSquadWideModifierDown() || GetActiveTurnBased() || IsRealTimeOrderLocked()
		|| bRelocateSelectMode)
	{
		return true;
	}
	if (const UVehicleSubsystem* Vehicles = GetWorld()->GetSubsystem<UVehicleSubsystem>(); Vehicles && Vehicles->IsMounted(Leader))
	{
		return true;
	}
	// The ordinary click's own targets: a ladder, an EXIT arrow, a wire, an object, a body, a silhouette, an enemy.
	if (HitActor->IsA<ALadderActor>() || HitActor->IsA<AInteriorExitArrowActor>() || HitActor->IsA<ATripwireActor>() || HitActor->IsA<AInteractableActor>()
		|| (HitActor->IsA<APawn>() && !HitActor->IsA<AVehiclePawn>()) || HitActor->IsA<AEnemyGhostActor>() || HitActor->ActorHasTag(FName(TEXT("Enemy"))))
	{
		return true;
	}
	return ATripwireActor::FindClickableWireNear(GetWorld(), Hit.ImpactPoint) != nullptr;
}

bool ACodexTacticsPlayerController::ResolveClickCoverSlot(AOperativeCharacter& Leader, const FHitResult& Hit, FCoverSlot& OutSlot, bool& bOutConfirm) const
{
	// TryHandleCoverClick's cover part (after its corner peeks), without its orders.
	OutSlot = FCoverSlot();
	bOutConfirm = false;
	AActor* HitActor = Hit.GetActor();
	if (!HitActor)
	{
		return false;
	}
	if (const UTutorialSubsystem* Tutorial = GetWorld()->GetSubsystem<UTutorialSubsystem>(); Tutorial && Tutorial->IsCommandLocked(PlayerCommands::CoverPreview))
	{
		return false;
	}
	const bool bVehicle = HitActor->IsA<AVehiclePawn>();
	if (HitActor->IsA<AInteractableActor>() || FindClickedMember(Hit) || (HitActor->IsA<APawn>() && !bVehicle) || HitActor->ActorHasTag(FName(TEXT("Enemy"))))
	{
		return false;
	}
	FCoverSlot SideSlot;
	EWallClickSide WallSide = EWallClickSide::None;
	const bool bSideSlot = !bVehicle && ResolveWallSideSlot(Hit, SideSlot, WallSide);
	if (!bSideSlot && FMath::Abs(Hit.ImpactNormal.Z) > 0.3f && !bVehicle)
	{
		return false;
	}
	FCoverSlot Slot = SideSlot;
	if (!bSideSlot && !FindCoverSlotForHit(Hit, Slot))
	{
		return false;
	}
	if (Leader.bInCover)
	{
		const FCoverSlot& Own = Leader.GetCoverSlot();
		const bool bOwnObject = Own.WallComponent.IsValid() ? Hit.GetComponent() == Own.WallComponent.Get() : HitActor == Own.WallActor.Get();
		const FVector2D HitNormal = bSideSlot ? FVector2D(SideSlot.WallNormal).GetSafeNormal() : FVector2D(Hit.ImpactNormal.X, Hit.ImpactNormal.Y).GetSafeNormal();
		if (bOwnObject && FVector2D::DotProduct(HitNormal, FVector2D(Own.WallNormal).GetSafeNormal()) < 0.9f)
		{
			OutSlot = Slot; // round his own object along its wall (TryOrderCoverWrap on the second click)
			return true;
		}
		if (CoverTraceRules::IsSameWall(Own, Slot))
		{
			FCoverSlot Shimmy;
			OutSlot = CoverTraceRules::FindShimmySlot(GetWorld(), Own, bSideSlot ? Slot.WallPoint : FVector(Hit.ImpactPoint), Shimmy) ? Shimmy : Slot;
			return true; // along his own wall: the shimmy on the second click
		}
	}
	OutSlot = Slot;
	bOutConfirm = true;
	return true;
}

void ACodexTacticsPlayerController::ShowGroupGhost(AOperativeCharacter& Member, const FCoverSlot& Slot, bool bPeek, ECoverFacing Side)
{
	if (!GroupPreviewGhosts.IsValidIndex(GroupPreviewShown))
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACoverGhostActor* Ghost = GetWorld()->SpawnActor<ACoverGhostActor>(Slot.WorldLocation, FRotator::ZeroRotator, Params);
		if (!Ghost)
		{
			return;
		}
		GroupPreviewGhosts.Add(Ghost);
	}
	ACoverGhostActor* Ghost = GroupPreviewGhosts[GroupPreviewShown++];
	if (!Ghost)
	{
		return;
	}
	if (bPeek)
	{
		Ghost->ShowCornerPeekFor(Member, Slot, Side);
	}
	else
	{
		Ghost->ShowFor(Member, Slot);
	}
}

void ACodexTacticsPlayerController::HideGroupGhosts()
{
	for (ACoverGhostActor* Ghost : GroupPreviewGhosts)
	{
		if (Ghost && Ghost->IsShown())
		{
			Ghost->Hide();
		}
	}
	GroupPreviewShown = 0;
}

TArray<const ACoverGhostActor*> ACodexTacticsPlayerController::GetClickPreviewGroupGhosts() const
{
	TArray<const ACoverGhostActor*> Shown;
	for (const ACoverGhostActor* Ghost : GroupPreviewGhosts)
	{
		if (Ghost && Ghost->IsShown())
		{
			Shown.Add(Ghost);
		}
	}
	return Shown;
}

int32 ACodexTacticsPlayerController::GetClickPreviewGhostCount() const
{
	int32 Count = GetClickPreviewGroupGhosts().Num() + GetBreachGhostCount();
	for (const ACoverGhostActor* Ghost : { CoverGhost.Get(), CornerPeekGhost.Get(), MantleGhost.Get(), CoverSwitchGhost.Get(), CoverWrapGhost.Get(), StealthWallHoverGhost.Get() })
	{
		Count += Ghost && Ghost->IsShown() ? 1 : 0;
	}
	return Count;
}

bool ACodexTacticsPlayerController::GroupWallLayoutSlots(AOperativeCharacter& Leader, const TArray<AOperativeCharacter*>& Others, const FCoverSlot& Slot,
	TArray<TPair<AOperativeCharacter*, FCoverSlot>>& OutPlaces, FCoverSlot* OutLeaderSlot) const
{
	OutPlaces.Reset();
	USquadSubsystem* Squad = GetSquad();
	TArray<TPair<AOperativeCharacter*, FFormationSlot>> Places;
	if (!Squad || !Squad->PreviewFormationWall(&Leader, Others, Slot, Places))
	{
		return false;
	}
	const FVector Normal = Slot.WallNormal.GetSafeNormal2D();
	for (const TPair<AOperativeCharacter*, FFormationSlot>& Each : Places)
	{
		FCoverSlot Place = Slot;
		Place.WorldLocation = FVector(Each.Value.Position.X, Each.Value.Position.Y, Slot.WorldLocation.Z);
		Place.WallPoint = FVector(Place.WorldLocation.X, Place.WorldLocation.Y, Slot.WallPoint.Z) - Normal * 45.f;
		Place.bLeftEdgeExposed = false;
		Place.bRightEdgeExposed = false;
		if (Each.Key == &Leader)
		{
			if (OutLeaderSlot)
			{
				*OutLeaderSlot = Place;
			}
			continue;
		}
		OutPlaces.Add(TPair<AOperativeCharacter*, FCoverSlot>(Each.Key, Place));
	}
	return true;
}

void ACodexTacticsPlayerController::ShowGroupGhostsForCover(AOperativeCharacter& Leader, const FCoverSlot& Slot)
{
	USquadSubsystem* Squad = GetSquad();
	if (!Squad)
	{
		return;
	}
	const TArray<AOperativeCharacter*> Others = Squad->GetGroupCoverMembers(&Leader);
	if (Others.IsEmpty())
	{
		return;
	}
	// OrderGroupCover's two placements: the formation wall layout (group mode), else the group cover slots / prone spots.
	const bool bWallLayout = FormationRuntime::IsEnabled() && !Squad->HasMultiSelection() && !Squad->IsSoloMode() && Squad->IsGroupCoverFormation()
		&& !Leader.IsInStealthMode();
	if (bWallLayout)
	{
		TArray<TPair<AOperativeCharacter*, FCoverSlot>> Places;
		FCoverSlot LeaderPlace;
		if (GroupWallLayoutSlots(Leader, Others, Slot, Places, &LeaderPlace))
		{
			if (CoverGhost && CoverGhost->IsShown() && LeaderPlace.IsValid())
			{
				CoverGhost->ShowFor(Leader, LeaderPlace); // the leader runs to his own place of the layout (the Commander's)
			}
			for (const TPair<AOperativeCharacter*, FCoverSlot>& Each : Places)
			{
				ShowGroupGhost(*Each.Key, Each.Value);
			}
			return;
		}
	}
	FGroupCoverPlacePlan Plan;
	if (!PlanGroupCoverPlaces(&Leader, Slot, Others, false, nullptr, Plan))
	{
		return;
	}
	for (int32 Index = 0; Index < Others.Num() && Index < Plan.Plan.Num(); ++Index)
	{
		const FGroupCoverAssignment& Each = Plan.Plan[Index];
		if (Each.Kind == EGroupCoverSpotKind::Slot && Plan.ChosenSlots.IsValidIndex(Each.Index))
		{
			ShowGroupGhost(*Others[Index], Plan.ChosenSlots[Each.Index]);
		}
		else if (Plan.ProneSpots.IsValidIndex(Each.Index))
		{
			FCoverSlot Prone = Plan.DirectSlot;
			Prone.WorldLocation = Plan.ProneSpots[Each.Index];
			Prone.WallPoint = Prone.WorldLocation - Plan.Normal * 45.f;
			Prone.Height = ECoverHeight::LowCover;
			Prone.bLeftEdgeExposed = false;
			Prone.bRightEdgeExposed = false;
			ShowGroupGhost(*Others[Index], Prone);
		}
	}
}

void ACodexTacticsPlayerController::ShowGroupGhostsForPeek(AOperativeCharacter& Leader, const FHitResult& Hit)
{
	USquadSubsystem* Squad = GetSquad();
	if (!Squad)
	{
		return;
	}
	const AOperativeCharacter* Peeker = CornerPeekGhost && CornerPeekGhost->IsShown() ? CornerPeekGhost->GetOperative() : nullptr;
	const bool bLeadership = Squad->IsWallLeadershipPeeker(&Leader);
	auto ShowStack = [this](const TArray<TPair<AOperativeCharacter*, FCoverSlot>>& Stack)
	{
		for (const TPair<AOperativeCharacter*, FCoverSlot>& Each : Stack)
		{
			if (Each.Key)
			{
				ShowGroupGhost(*Each.Key, Each.Value);
			}
		}
	};
	// UpdateCornerPeekHover's branches, in its order.
	if ((!Leader.bInCover || bLeadership) && bSquadPeekHoverValid && SquadPeekHoverTarget.Peeker.IsValid() && Peeker == SquadPeekHoverTarget.Peeker.Get())
	{
		// The squad's corner peek: the corner man and the v2.4 corner stack behind him (FFormationSquadPeek).
		AOperativeCharacter* PlanPeeker = nullptr;
		FFormationSquadPeek Plan;
		if (Squad->PlanFormationCornerPeek(SquadPeekHoverTarget.EdgeOnFace, PlanPeeker, Plan))
		{
			const FCoverSlot& Corner = SquadPeekHoverTarget.CornerSlot;
			for (const FFormationSlot& Place : Plan.StackSlots)
			{
				if (Place.Role == Plan.Peeker)
				{
					continue;
				}
				for (AOperativeCharacter* Member : Squad->GetMembers())
				{
					EFormationRole MemberRole = EFormationRole::Commander;
					if (Member && Member != PlanPeeker && Squad->GetFormationRole(Member, MemberRole) && MemberRole == Place.Role)
					{
						FCoverSlot Slot = Corner;
						Slot.WorldLocation = FVector(Place.Position.X, Place.Position.Y, Corner.WorldLocation.Z);
						Slot.WallPoint = Slot.WorldLocation - Corner.WallNormal.GetSafeNormal2D() * 45.f;
						Slot.bLeftEdgeExposed = false;
						Slot.bRightEdgeExposed = false;
						ShowGroupGhost(*Member, Slot);
						break;
					}
				}
			}
		}
		return;
	}
	if (Leader.bInCover && !bLeadership && bPeekHoverEdge)
	{
		// In cover at that wall with a box selection: the man nearest the end takes it, the others side-step behind him.
		AOperativeCharacter* GroupPeeker = nullptr;
		ECoverFacing Side = ECoverFacing::Right;
		float Edge = 0.f;
		TArray<TPair<AOperativeCharacter*, FCoverSlot>> Stack;
		if (PlanGroupCornerPeek(&Leader, Hit, GroupPeeker, Side, Edge, &Stack))
		{
			ShowStack(Stack);
		}
		return;
	}
	// The run-and-peek from the open: a box selection's stack behind the corner man; formation: the wall layout at that corner.
	FRunCornerPeekFace Face;
	FSquadCornerPeekTarget Target;
	if (!FindRunCornerPeekClickTarget(&Leader, Hit, false, nullptr, Face, Target))
	{
		return;
	}
	if (Squad->HasMultiSelection())
	{
		AOperativeCharacter* GroupPeeker = nullptr;
		TArray<TPair<AOperativeCharacter*, FCoverSlot>> Stack;
		if (PlanRunGroupCornerPeek(&Leader, Face, GroupPeeker, &Stack))
		{
			ShowStack(Stack);
		}
		return;
	}
	if (!Squad->IsSoloMode())
	{
		const TArray<AOperativeCharacter*> Others = Squad->GetGroupCoverMembers(&Leader);
		TArray<TPair<AOperativeCharacter*, FCoverSlot>> Places;
		if (!Others.IsEmpty() && GroupWallLayoutSlots(Leader, Others, Face.Corner, Places, nullptr))
		{
			ShowStack(Places);
		}
	}
}

EClickPreviewKind ACodexTacticsPlayerController::UpdateClickPreviewAt(const FHitResult& Hit, FString* OutWhy)
{
	USquadSubsystem* Squad = GetSquad();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	auto Why = [OutWhy](const FString& Text)
	{
		if (OutWhy)
		{
			*OutWhy = Text;
		}
	};
	EClickPreviewKind Kind = EClickPreviewKind::None;
	// The members' silhouettes: re-planned only when the leader's ghost moved on (the placement traces a lot).
	const int32 GroupBefore = GroupPreviewShown;
	const FVector GroupAtBefore = ClickPreviewGroupAt;
	const EClickPreviewKind GroupKindBefore = ClickPreviewGroupKind;
	GroupPreviewShown = 0;
	FBreachPlan Plan;
	if (!Leader)
	{
		Why(TEXT("no operative selected"));
	}
	else if (IsClickPreviewBypass(Hit))
	{
		Why(FString::Printf(TEXT("the ordinary click's own case (%s)"), *GetNameSafe(Hit.GetActor())));
	}
	else if (FindBreachForHit(Hit, Plan))
	{
		Kind = EClickPreviewKind::Breach;
		BreachPreview = Plan;
		bBreachPreview = true;
		ShowBreachGhost(Plan);
	}
	if (Kind != EClickPreviewKind::Breach)
	{
		HideBreachGhost();
	}
	// The obstacle's top: the climb (Solo, one operative; CanOrderMantleClick / PlanMantleClick decide, as the click).
	const bool bTop = FMath::Abs(Hit.ImpactNormal.Z) >= 0.3f;
	if (Kind == EClickPreviewKind::None && Leader && !IsClickPreviewBypass(Hit) && bTop && MantleNavigation::IsBatch2Enabled() && CanOrderMantleClick(Hit)
		&& UpdateMantleHover(Leader, &Hit))
	{
		Kind = EClickPreviewKind::Mantle;
	}
	else
	{
		UpdateMantleHover(nullptr, nullptr);
	}
	// Near a wall's end: the peek round it (in cover there, the squad's corner, the run-and-peek).
	if (Kind == EClickPreviewKind::None && Leader && !IsClickPreviewBypass(Hit) && UpdateCornerPeekHover(Leader, &Hit))
	{
		Kind = EClickPreviewKind::CornerPeek;
		if (GroupKindBefore == Kind && CornerPeekGhost && FVector::Dist2D(CornerPeekGhost->GetSlot().WorldLocation, GroupAtBefore) < ControllerClickPreviewLocal::GroupReplanCm)
		{
			GroupPreviewShown = GroupBefore; // the same corner: the stack stays
		}
		else
		{
			ShowGroupGhostsForPeek(*Leader, Hit);
			ClickPreviewGroupAt = CornerPeekGhost ? CornerPeekGhost->GetSlot().WorldLocation : FVector::ZeroVector;
		}
	}
	else if (Kind != EClickPreviewKind::CornerPeek)
	{
		UpdateCornerPeekHover(Leader, nullptr);
	}
	// A wall / obstacle face: hug it.
	FCoverSlot Slot;
	bool bConfirm = false;
	if (Kind == EClickPreviewKind::None && Leader && !IsClickPreviewBypass(Hit) && ResolveClickCoverSlot(*Leader, Hit, Slot, bConfirm))
	{
		Kind = EClickPreviewKind::Cover;
		bClickPreviewCoverConfirm = bConfirm;
		bCoverPreviewTurnBased = false;
		ShowCoverPreview(Leader, Slot);
		if (bConfirm)
		{
			if (GroupKindBefore == Kind && FVector::Dist2D(Slot.WorldLocation, GroupAtBefore) < ControllerClickPreviewLocal::GroupReplanCm)
			{
				GroupPreviewShown = GroupBefore;
			}
			else
			{
				ShowGroupGhostsForCover(*Leader, Slot);
				ClickPreviewGroupAt = Slot.WorldLocation;
			}
		}
	}
	else if (Kind != EClickPreviewKind::Cover && HasCoverPreview())
	{
		HideCoverPreview();
	}
	if (Kind == EClickPreviewKind::None && Leader && OutWhy && OutWhy->IsEmpty())
	{
		FCoverSlot Probe;
		*OutWhy = FString::Printf(TEXT("no building meaning: %s, normal z %.2f, %.0f cm over his feet; the cover probe %s"), *GetNameSafe(Hit.GetActor()), Hit.ImpactNormal.Z,
			Hit.ImpactPoint.Z - (Leader->GetActorLocation().Z - Leader->GetSimpleCollisionHalfHeight()), FindCoverSlotForHit(Hit, Probe) ? TEXT("finds a slot") : TEXT("finds none"));
	}
	ClickPreviewGroupKind = Kind;
	// Silhouettes this plan did not use go.
	for (int32 Index = GroupPreviewShown; Index < GroupPreviewGhosts.Num(); ++Index)
	{
		if (GroupPreviewGhosts[Index] && GroupPreviewGhosts[Index]->IsShown())
		{
			GroupPreviewGhosts[Index]->Hide();
		}
	}
	return Kind;
}

EClickPreviewKind ACodexTacticsPlayerController::ClassifyClickPreview(const FHitResult& Hit, FString* OutWhy)
{
	if (IsClickPreviewArmed())
	{
		CancelClickPreview(TEXT("a classification"));
	}
	const bool bQuietBefore = bQuietCoverPreview;
	bQuietCoverPreview = true;
	const EClickPreviewKind Kind = UpdateClickPreviewAt(LowerBuildingHit(Hit), OutWhy);
	bQuietCoverPreview = bQuietBefore;
	CancelClickPreview(TEXT("a classification"));
	return Kind;
}

bool ACodexTacticsPlayerController::GetClickPreviewGhostFeet(FVector& OutFeet) const
{
	const ACoverGhostActor* Ghost = nullptr;
	switch (ClickPreviewKind)
	{
	case EClickPreviewKind::Cover:
		Ghost = CoverGhost;
		break;
	case EClickPreviewKind::CornerPeek:
		Ghost = CornerPeekGhost;
		break;
	case EClickPreviewKind::Mantle:
		Ghost = MantleGhost;
		break;
	default:
		break;
	}
	if (!Ghost || !Ghost->IsShown())
	{
		return false;
	}
	OutFeet = ClickPreviewKind == EClickPreviewKind::Mantle ? Ghost->GetActorLocation() : Ghost->GetSlot().WorldLocation;
	return true;
}

void ACodexTacticsPlayerController::CancelClickPreview(const TCHAR* Why)
{
	const bool bWasArmed = IsClickPreviewArmed();
	HideCoverPreview();
	UpdateCornerPeekHover(nullptr, nullptr);
	UpdateMantleHover(nullptr, nullptr);
	HideGroupGhosts();
	HideBreachGhost();
	ClickPreviewKind = EClickPreviewKind::None;
	ClickPreviewGroupKind = EClickPreviewKind::None;
	ClickPreviewStructure.Reset();
	ClickPreviewLeader.Reset();
	ClickPreviewArmTime = -1.0;
	bClickPreviewCoverConfirm = false;
	bBreachPreview = false;
	if (bWasArmed)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[ClickPreview] the ghost goes (%s)"), Why);
	}
}

void ACodexTacticsPlayerController::HandlePlayerClick(const FHitResult& InHit)
{
	if (!IsClickPreviewEnabled())
	{
		HandleWorldHit(InHit);
		return;
	}
	FVector2D Mouse = FVector2D::ZeroVector; // stays zero without a viewport (headless smokes)
	GetMousePosition(Mouse.X, Mouse.Y);
	const double Now = FApp::GetCurrentTime();
	// R17-5: a stacked breach - a click inside its room is the entry (a click on a jamb man's body in the frame: the room behind him).
	if (Breach.Phase == EBreachPhase::Stacked)
	{
		const FHitResult EntryHit = BreachEntryHit(InHit);
		if (!IsClickPreviewBypass(EntryHit) && TryBreachEntryClick(EntryHit))
		{
			CancelClickPreview(TEXT("the breach entry"));
			return;
		}
	}
	const FHitResult Hit = LowerBuildingHit(InHit);
	const USquadSubsystem* Squad = GetSquad();
	const AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	FClickPreviewClickFacts Facts;
	Facts.bEnabled = true;
	Facts.bArmed = IsClickPreviewArmed() && ClickPreviewLeader.Get() == Leader;
	Facts.bBypass = IsClickPreviewBypass(Hit);
	const FClickPreviewPiece Piece = MakeClickPiece(Hit);
	Facts.bOnArmedStructure = Facts.bArmed && ClickPreviewRules::IsOnStructure(ClickPreviewStructure, Piece);
	FVector GhostFeet;
	Facts.bOnGhost = Facts.bArmed && !Facts.bOnArmedStructure && GetClickPreviewGhostFeet(GhostFeet)
		&& ClickPreviewRules::IsClickOnGhost(Hit.ImpactPoint, Piece.bFloorLike, GhostFeet);
	EClickPreviewKind NewKind = EClickPreviewKind::None;
	if (!Facts.bBypass && !(Facts.bArmed && (Facts.bOnArmedStructure || Facts.bOnGhost)))
	{
		bQuietCoverPreview = false; // the arming click's feed line
		NewKind = UpdateClickPreviewAt(Hit);
		Facts.bSurface = NewKind != EClickPreviewKind::None;
	}
	const EClickPreviewAction Action = ClickPreviewRules::DecideClick(Facts);
	if (Action != EClickPreviewAction::Execute && Breach.Phase != EBreachPhase::None && Action != EClickPreviewAction::Arm)
	{
		EndBreach(TEXT("a new order"));
	}
	switch (Action)
	{
	case EClickPreviewAction::Arm:
	{
		ClickPreviewKind = NewKind;
		ClickPreviewStructure.Reset();
		ClickPreviewRules::AddPiece(ClickPreviewStructure, Piece);
		ClickPreviewHit = Hit;
		ClickPreviewLeader = const_cast<AOperativeCharacter*>(Leader);
		ClickPreviewArmTime = Now;
		ClickPreviewArmMouse = Mouse;
		ClickPreviewTrackTime = Now;
		ClickPreviewTriedAt = Hit.ImpactPoint;
		UE_LOG(LogCodexTactics, Display, TEXT("[ClickPreview] first click on %s at %s (normal z %.2f): the %s ghost (%d silhouettes) - a second click on it carries it out"),
			*GetNameSafe(Hit.GetActor()), *Hit.ImpactPoint.ToCompactString(), Hit.ImpactNormal.Z,
			NewKind == EClickPreviewKind::Cover ? TEXT("cover") : NewKind == EClickPreviewKind::CornerPeek ? TEXT("corner peek") : NewKind == EClickPreviewKind::Mantle ? TEXT("climb") : TEXT("breach"),
			GetClickPreviewGhostCount());
		UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>();
		if (Messages && Leader)
		{
			if (NewKind == EClickPreviewKind::CornerPeek)
			{
				Messages->PostMessage(Leader->DisplayName, LOCTEXT("ClickPreviewPeek", "🎯 Corner: click again to peek round it (double click: at a run)."));
			}
			else if (NewKind == EClickPreviewKind::Mantle)
			{
				Messages->PostMessage(Leader->DisplayName, LOCTEXT("ClickPreviewMantle", "🧗 Obstacle: click again to climb it (double click: run and climb)."));
			}
			else if (NewKind == EClickPreviewKind::Breach)
			{
				Messages->PostMessage(Leader->DisplayName, LOCTEXT("ClickPreviewBreach", "🚪 Doorway: click again to stack on it."));
			}
		}
		if (NewKind == EClickPreviewKind::Cover)
		{
			ControllerCommands::Report(this, PlayerCommands::CoverPreview, nullptr, false, false, const_cast<AOperativeCharacter*>(Leader), Hit.ImpactPoint);
		}
		return;
	}
	case EClickPreviewAction::Execute:
	{
		const bool bDouble = ClickPreviewRules::IsDoubleClick(ClickPreviewArmTime >= 0.0 ? Now - ClickPreviewArmTime : -1.0,
			static_cast<float>(FVector2D::Distance(Mouse, ClickPreviewArmMouse)), DoubleClickSeconds, DoubleClickPixels);
		ExecuteClickPreview(Facts.bOnArmedStructure ? Hit : ClickPreviewHit, bDouble);
		return;
	}
	case EClickPreviewAction::CancelAndPass:
		CancelClickPreview(TEXT("a click elsewhere"));
		HandleWorldHit(InHit);
		return;
	default:
		HandleWorldHit(InHit);
		return;
	}
}

void ACodexTacticsPlayerController::ExecuteClickPreview(const FHitResult& Hit, bool bDoubleClick)
{
	USquadSubsystem* Squad = GetSquad();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (!Leader)
	{
		CancelClickPreview(TEXT("no operative"));
		return;
	}
	// What the ghost shows at the click (the cursor's spot on the building); nothing there - the last spot it showed.
	bQuietCoverPreview = true;
	FHitResult Use = Hit;
	EClickPreviewKind Kind = UpdateClickPreviewAt(Use);
	if (Kind == EClickPreviewKind::None)
	{
		Use = ClickPreviewHit;
		Kind = UpdateClickPreviewAt(Use);
	}
	bQuietCoverPreview = false;
	UE_LOG(LogCodexTactics, Display, TEXT("[ClickPreview] second click on %s at %s%s: carries out the %s ghost"), *GetNameSafe(Use.GetActor()),
		*Use.ImpactPoint.ToCompactString(), bDoubleClick ? TEXT(" (double click)") : TEXT(""),
		Kind == EClickPreviewKind::Cover ? TEXT("cover") : Kind == EClickPreviewKind::CornerPeek ? TEXT("corner peek") : Kind == EClickPreviewKind::Mantle ? TEXT("climb")
		: Kind == EClickPreviewKind::Breach ? TEXT("breach") : TEXT("(none)"));
	if (Kind != EClickPreviewKind::Breach && Breach.Phase != EBreachPhase::None)
	{
		EndBreach(TEXT("a new order"));
	}
	const bool bCoverConfirm = bClickPreviewCoverConfirm;
	const FBreachPlan PlanCopy = BreachPreview;
	// The ghosts go (the cover order's own preview stays for ConfirmCoverPreview).
	UpdateCornerPeekHover(nullptr, nullptr);
	UpdateMantleHover(nullptr, nullptr);
	HideGroupGhosts();
	HideBreachGhost();
	ClickPreviewKind = EClickPreviewKind::None;
	ClickPreviewGroupKind = EClickPreviewKind::None;
	ClickPreviewStructure.Reset();
	ClickPreviewLeader.Reset();
	ClickPreviewArmTime = -1.0;
	bBreachPreview = false;
	switch (Kind)
	{
	case EClickPreviewKind::Mantle:
	{
		HideCoverPreview();
		FString Why;
		if (Leader->OrderMantleTop(Use, bDoubleClick, &Why))
		{
			ControllerCommands::Report(this, PlayerCommands::Move, nullptr, false, false, Leader, Use.ImpactPoint);
			UE_LOG(LogCodexTactics, Display, TEXT("[ClickPreview] %s %s to %s and climbs it"), *Leader->DisplayName.ToString(), bDoubleClick ? TEXT("runs") : TEXT("walks"),
				*GetNameSafe(Use.GetActor()));
		}
		else if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
		{
			Messages->PostMessage(LOCTEXT("HQ", "HQ"), LOCTEXT("ClickPreviewNoClimb", "⚠️ Can't climb that from here."));
			UE_LOG(LogCodexTactics, Display, TEXT("[ClickPreview] no climb: %s"), *Why);
		}
		return;
	}
	case EClickPreviewKind::Cover:
	{
		// A double click on a climbable obstacle's face: run there and climb (the designer's decision 2026-11-17; not in cover: there it is the switch).
		if (bDoubleClick && !Leader->bInCover && MantleNavigation::IsBatch2Enabled() && CanOrderMantleClick(Use))
		{
			FString Why;
			if (Leader->OrderMantleTop(Use, true, &Why))
			{
				HideCoverPreview();
				ControllerCommands::Report(this, PlayerCommands::Move, nullptr, false, false, Leader, Use.ImpactPoint);
				UE_LOG(LogCodexTactics, Display, TEXT("[ClickPreview] double click on the face of %s: %s runs to it and climbs it"), *GetNameSafe(Use.GetActor()),
					*Leader->DisplayName.ToString());
				return;
			}
		}
		if (bCoverConfirm && HasCoverPreview())
		{
			ConfirmCoverPreview(bDoubleClick);
		}
		else
		{
			HideCoverPreview();
			HandleWorldHit(Use); // along his own wall (the shimmy) / round his own object (the wrap): the cover click itself
		}
		return;
	}
	case EClickPreviewKind::CornerPeek:
		HideCoverPreview();
		bClickPreviewSprint = bDoubleClick;
		HandleWorldHit(Use); // TryHandleCoverClick: the in-cover / squad / run-and-peek order the ghost showed
		bClickPreviewSprint = false;
		return;
	case EClickPreviewKind::Breach:
		HideCoverPreview();
		OrderBreach(PlanCopy, bDoubleClick);
		return;
	default:
		HideCoverPreview();
		HandleWorldHit(Hit);
		return;
	}
}

void ACodexTacticsPlayerController::TickClickPreview(const FHitResult* CursorHit)
{
	if (!IsClickPreviewArmed())
	{
		return;
	}
	const USquadSubsystem* Squad = GetSquad();
	if (!Squad || Squad->GetLeader() != ClickPreviewLeader.Get() || GetActiveTurnBased() || IsDialogueOpen())
	{
		CancelClickPreview(TEXT("the selection / mode changed"));
		return;
	}
	if (!CursorHit || !CursorHit->bBlockingHit)
	{
		return; // the ghost stays
	}
	const FHitResult Hit = LowerBuildingHit(*CursorHit);
	const double Now = FApp::GetCurrentTime();
	if (Now - ClickPreviewTrackTime < ClickPreviewRules::TrackIntervalSeconds
		|| FVector::Dist(Hit.ImpactPoint, ClickPreviewTriedAt) < ClickPreviewRules::TrackMoveCm)
	{
		return;
	}
	const FClickPreviewPiece Piece = MakeClickPiece(Hit);
	if (!ClickPreviewRules::IsOnStructure(ClickPreviewStructure, Piece) || IsClickPreviewBypass(Hit))
	{
		return; // off the building: the ghost stays where it was
	}
	ClickPreviewTrackTime = Now;
	ClickPreviewTriedAt = Hit.ImpactPoint;
	bQuietCoverPreview = true;
	const EClickPreviewKind Kind = UpdateClickPreviewAt(Hit);
	if (Kind == EClickPreviewKind::None)
	{
		UpdateClickPreviewAt(ClickPreviewHit); // nothing at this spot of the building: the last ghost again
	}
	else
	{
		if (Kind != ClickPreviewKind)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[ClickPreview] the cursor along %s: the ghost turns into the %s"), *GetNameSafe(Hit.GetActor()),
				Kind == EClickPreviewKind::Cover ? TEXT("cover") : Kind == EClickPreviewKind::CornerPeek ? TEXT("corner peek") : Kind == EClickPreviewKind::Mantle ? TEXT("climb") : TEXT("breach"));
		}
		ClickPreviewKind = Kind;
		ClickPreviewHit = Hit;
		ClickPreviewRules::AddPiece(ClickPreviewStructure, Piece);
	}
	bQuietCoverPreview = false;
}

#undef LOCTEXT_NAMESPACE
