#include "Core/CodexTacticsPlayerController.h"

// The custom formation's placement mode (game designer decisions 2026-11-01; docs/design/FORMATION_SECTORS.md "Custom formation", contract
// docs/design/FORMATION_CONTRACT.md 17). Opened from the hold-B menu's Custom 1 / 2 / 3 row (an empty slot, or Ctrl on a filled one): the world
// stops (a dedicated placement pause - the global time dilation at its minimum, not the tactical pause), each member has a holographic ghost
// (AFormationGhostActor). Click an operative (or his ghost), then press on his spot and drag to set his look (a plain click = the squad's
// heading); Space or the same Custom in the menu confirms (the shape stored, picked, time resumes, the squad walks into it), Esc / RMB cancel.
// Godot reference: none (UE-only). Pure state: FFormationPlacement (UI/FormationPlacementRules).

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Core/CodexTacticsPlayerControllerInternal.h"
#include "Engine/World.h"
#include "Formation/FormationCustomRules.h"
#include "Formation/FormationMenuRules.h"
#include "Formation/FormationRuntimeRules.h"
#include "Formation/FormationTuning.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "UI/FormationGhostActor.h"

#define LOCTEXT_NAMESPACE "CodexTacticsPlayerController"

namespace FormationCustomPlacementLocal
{
	/** A spot must project onto the nav mesh within this (it is no point inside / on top of a wall), cm. */
	constexpr float NavSnapCm = 60.f;

	/** The ground under a 2D point: a visibility trace down from above, else FallbackZ. */
	float GroundZAt(const UWorld& World, const FVector2D& Point, float AroundZ, const TArray<const AActor*>& Ignored)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(FormationPlacementGround), false);
		for (const AActor* Actor : Ignored)
		{
			Params.AddIgnoredActor(Actor);
		}
		FHitResult Hit;
		const FVector Start(Point, AroundZ + 200.f);
		const FVector End(Point, AroundZ - 500.f);
		return World.LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params) ? static_cast<float>(Hit.ImpactPoint.Z) : AroundZ;
	}

	float PlacementFeetZ(const AOperativeCharacter& Operative)
	{
		return static_cast<float>(Operative.GetActorLocation().Z) - Operative.GetSimpleCollisionHalfHeight();
	}
}

bool ACodexTacticsPlayerController::BeginFormationPlacement(int32 Slot)
{
	using namespace FormationCustomPlacementLocal;
	USquadSubsystem* Squad = GetSquad();
	UWorld* World = GetWorld();
	AOperativeCharacter* LeaderRef = Squad ? Squad->GetLeader() : nullptr;
	if (FormationPlacement.IsActive())
	{
		CancelFormationPlacement();
	}
	if (!World || !Squad || !LeaderRef || Slot < 1 || Slot > FormationCustomRules::SlotCount || !FormationRuntime::IsEnabled() || Squad->IsSoloMode()
		|| GetActiveTurnBased() || IsDialogueOpen() || Squad->GetMembers().Num() < 2)
	{
		PostHeadquarters(LOCTEXT("FormationPlacementRefused", "A custom formation needs the squad together (2+ operatives, not in turn-based combat)."));
		return false;
	}
	// Every member has a ghost from the start: the slot's shape round the leader (editing), else where he stands - what is seen is what is stored.
	const float Heading = Squad->IsFormationSectorsActive() ? Squad->GetFormationHeadingYaw() : static_cast<float>(LeaderRef->GetActorRotation().Yaw);
	const FFormationCustomShape& Stored = Squad->GetFormationCustomShape(Slot);
	FormationPlacementMembers.Reset();
	FormationGhostZ.Reset();
	// The members in the formation (the leader and his followers - not a man on guard, CustomSlots' "present"), at most four.
	const TArray<int32> InFormation = Squad->GetFormationCustomPresent();
	TArray<const AActor*> Ignored;
	for (AOperativeCharacter* Member : Squad->GetMembers())
	{
		if (Member)
		{
			Ignored.Add(Member);
			const int32 Order = InFormation.IndexOfByKey(Member->SquadIndex);
			if (Order != INDEX_NONE && Order < FormationCustomRules::MaxMembers)
			{
				FormationPlacementMembers.Add(Member->SquadIndex, Member);
			}
		}
	}
	if (FormationPlacementMembers.Num() < 2)
	{
		PostHeadquarters(LOCTEXT("FormationPlacementRefused", "A custom formation needs the squad together (2+ operatives, not in turn-based combat)."));
		return false;
	}
	TArray<FFormationCustomGhost> Ghosts;
	if (!Stored.IsEmpty())
	{
		for (const FFormationCustomSlot& Each : FormationCustomRules::CustomSlots(Stored, InFormation, FVector2D(LeaderRef->GetActorLocation()), Heading, FormationTuning::Get()))
		{
			FFormationCustomGhost& Ghost = Ghosts.AddDefaulted_GetRef();
			Ghost.Squad = Each.Squad;
			Ghost.Position = Each.Position;
			Ghost.Yaw = Each.Squad == LeaderRef->SquadIndex ? Heading : Each.Yaw;
		}
	}
	for (const TPair<int32, TWeakObjectPtr<AOperativeCharacter>>& Entry : FormationPlacementMembers)
	{
		const AOperativeCharacter* Member = Entry.Value.Get();
		if (!Member || Ghosts.ContainsByPredicate([&Entry](const FFormationCustomGhost& Ghost) { return Ghost.Squad == Entry.Key; }))
		{
			continue;
		}
		FFormationCustomGhost& Ghost = Ghosts.AddDefaulted_GetRef();
		Ghost.Squad = Entry.Key;
		Ghost.Position = FVector2D(Member->GetActorLocation());
		Ghost.Yaw = Member == LeaderRef ? Heading : static_cast<float>(Member->GetActorRotation().Yaw);
	}
	for (const FFormationCustomGhost& Ghost : Ghosts)
	{
		FormationGhostZ.Add(Ghost.Squad, GroundZAt(*World, Ghost.Position, PlacementFeetZ(*LeaderRef), Ignored));
	}
	FormationPlacement.Begin(Slot, LeaderRef->SquadIndex, Ghosts);
	FormationPlacement.Select(LeaderRef->SquadIndex);
	LastFormationPlacementResult = EFormationPlacementResult::None;
	UGameplayStatics::SetGlobalTimeDilation(World, FormationPlacementTimeDilation);
	SyncFormationGhosts();
	UE_LOG(LogCodexTactics, Display, TEXT("[FormationCustom] placement for Custom %d: %d ghost(s), anchor #%d, heading %.0f, %s"), Slot, Ghosts.Num(),
		LeaderRef->SquadIndex, Heading, Stored.IsEmpty() ? TEXT("a new shape") : TEXT("editing the stored shape"));
	return true;
}

bool ACodexTacticsPlayerController::ConfirmFormationPlacement()
{
	if (!FormationPlacement.IsActive())
	{
		return false;
	}
	USquadSubsystem* Squad = GetSquad();
	const int32 Slot = FormationPlacement.GetSlot();
	const FFormationCustomShape Shape = FormationPlacement.Shape();
	const FFormationCustomGhost* AnchorGhost = FormationPlacement.FindGhost(FormationPlacement.GetAnchorSquad());
	const FVector2D AnchorPos = AnchorGhost ? AnchorGhost->Position : FVector2D::ZeroVector;
	const float AnchorYaw = AnchorGhost ? AnchorGhost->Yaw : 0.f;
	const float* AnchorZ = FormationGhostZ.Find(FormationPlacement.GetAnchorSquad());
	const TWeakObjectPtr<AOperativeCharacter>* AnchorMember = FormationPlacementMembers.Find(FormationPlacement.GetAnchorSquad());
	EndFormationPlacement();
	if (!Squad || Shape.IsEmpty())
	{
		return false;
	}
	Squad->SetFormationCustomShape(Slot, Shape);
	Squad->SetFormationMenuChoice(FormationMenuRules::CustomItem(Slot));
	// The frame: the anchor's heading becomes the squad's (the shape is relative to it); the anchor walks to his own spot when it moved,
	// the others follow into their places round him (the formation follow).
	Squad->ResetFormationState(FVector(FormationMath::Forward(AnchorYaw), 0.0));
	AOperativeCharacter* Anchor = AnchorMember ? AnchorMember->Get() : nullptr;
	if (Anchor)
	{
		Anchor->SetIdleFacingYaw(AnchorYaw);
		if (FVector2D::Distance(FVector2D(Anchor->GetActorLocation()), AnchorPos) > 50.0)
		{
			Anchor->OrderMoveTo(FVector(AnchorPos, AnchorZ ? *AnchorZ : Anchor->GetActorLocation().Z), /*bSprint*/ false);
		}
	}
	Squad->RefreshFormation();
	ControllerCommands::Report(this, PlayerCommands::FormationMenu);
	UE_LOG(LogCodexTactics, Display, TEXT("[FormationCustom] confirmed Custom %d: %d member(s), heading %.0f"), Slot, Shape.Members.Num(), AnchorYaw);
	return true;
}

bool ACodexTacticsPlayerController::CancelFormationPlacement()
{
	if (!FormationPlacement.IsActive())
	{
		return false;
	}
	const int32 Slot = FormationPlacement.GetSlot();
	EndFormationPlacement();
	UE_LOG(LogCodexTactics, Display, TEXT("[FormationCustom] placement for Custom %d cancelled (the slot unchanged)"), Slot);
	return true;
}

void ACodexTacticsPlayerController::EndFormationPlacement()
{
	FormationPlacement.End();
	for (AFormationGhostActor* Ghost : FormationGhosts)
	{
		if (Ghost)
		{
			Ghost->Hide();
		}
	}
	FormationPlacementMembers.Reset();
	FormationGhostZ.Reset();
	bLmbDown = false;
	bBoxSelecting = false;
	// The world clock back to what the game flow wants (exploration 1, the tactical pause's 0.02, ...).
	if (UWorld* World = GetWorld())
	{
		if (const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>())
		{
			Flow->ApplyTimeDilation();
		}
		else
		{
			UGameplayStatics::SetGlobalTimeDilation(World, 1.f);
		}
	}
}

AFormationGhostActor* ACodexTacticsPlayerController::GetFormationGhost(int32 Squad) const
{
	if (!FormationPlacement.IsActive())
	{
		return nullptr;
	}
	int32 Index = 0;
	for (const FFormationCustomGhost& Ghost : FormationPlacement.GetGhosts())
	{
		if (Ghost.Squad == Squad)
		{
			return FormationGhosts.IsValidIndex(Index) ? FormationGhosts[Index].Get() : nullptr;
		}
		++Index;
	}
	return nullptr;
}

void ACodexTacticsPlayerController::SyncFormationGhosts()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const TArray<FFormationCustomGhost>& Ghosts = FormationPlacement.GetGhosts();
	while (FormationGhosts.Num() < Ghosts.Num())
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		FormationGhosts.Add(World->SpawnActor<AFormationGhostActor>(AFormationGhostActor::StaticClass(), FTransform::Identity, Params));
	}
	const TOptional<int32> Selected = FormationPlacement.GetSelected();
	for (int32 Index = 0; Index < FormationGhosts.Num(); ++Index)
	{
		AFormationGhostActor* Actor = FormationGhosts[Index];
		if (!Actor)
		{
			continue;
		}
		if (!Ghosts.IsValidIndex(Index))
		{
			Actor->Hide();
			continue;
		}
		const FFormationCustomGhost& Ghost = Ghosts[Index];
		const TWeakObjectPtr<AOperativeCharacter>* Member = FormationPlacementMembers.Find(Ghost.Squad);
		const float* Z = FormationGhostZ.Find(Ghost.Squad);
		const FVector Feet(Ghost.Position, Z ? *Z : 0.f);
		if (!Actor->IsShown() && Member && Member->IsValid())
		{
			Actor->ShowFor(*Member->Get(), Feet, Ghost.Yaw);
		}
		else
		{
			Actor->SetPlacement(Feet, Ghost.Yaw);
		}
		Actor->SetSelected(Selected.IsSet() && Selected.GetValue() == Ghost.Squad);
	}
}

bool ACodexTacticsPlayerController::FormationPlacementGround(FVector& OutGround, float PlaneZ) const
{
	FHitResult Hit;
	if (GetHitResultUnderCursor(ECC_Visibility, false, Hit) && Hit.bBlockingHit && FMath::Abs(Hit.ImpactNormal.Z) > 0.7f && !Cast<APawn>(Hit.GetActor()))
	{
		OutGround = Hit.ImpactPoint;
		return true;
	}
	FVector Origin;
	FVector Direction;
	if (!DeprojectMousePositionToWorld(Origin, Direction) || FMath::Abs(Direction.Z) < 1e-3)
	{
		return false;
	}
	const double T = (static_cast<double>(PlaneZ) - Origin.Z) / Direction.Z;
	if (T <= 0.0)
	{
		return false;
	}
	OutGround = Origin + Direction * T;
	return true;
}

bool ACodexTacticsPlayerController::HandleFormationPlacementPress(const FHitResult& Hit)
{
	using namespace FormationCustomPlacementLocal;
	if (!FormationPlacement.IsActive())
	{
		return false;
	}
	// An operative's body (the hit itself - not FindClickedMember's 120 cm pick radius, a spot next to a team-mate is a spot), or a click near
	// his ghost, selects him; anywhere else is the selected operative's spot.
	const AActor* HitActor = Hit.GetActor();
	const AOperativeCharacter* Clicked = Cast<AOperativeCharacter>(HitActor);
	if (!Clicked && HitActor)
	{
		Clicked = Cast<AOperativeCharacter>(HitActor->GetOwner());
	}
	if (Clicked && FormationPlacementMembers.Contains(Clicked->SquadIndex))
	{
		LastFormationPlacementResult = FormationPlacement.Select(Clicked->SquadIndex);
		SyncFormationGhosts();
		return true;
	}
	const FVector2D Ground(Hit.ImpactPoint);
	const TOptional<int32> Near = FormationPlacement.GhostAt(Ground, GhostPickRadius);
	if (Near.IsSet() && (!FormationPlacement.GetSelected().IsSet() || FormationPlacement.GetSelected().GetValue() != Near.GetValue()))
	{
		LastFormationPlacementResult = FormationPlacement.Select(Near.GetValue());
		SyncFormationGhosts();
		return true;
	}
	LastFormationPlacementResult = FormationPlacement.Press(Ground);
	if (LastFormationPlacementResult == EFormationPlacementResult::NoOperative)
	{
		PostHeadquarters(LOCTEXT("FormationPlacementPickFirst", "Click an operative first, then his spot."));
		return true;
	}
	// The spot must be walkable ground (on the nav mesh).
	const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FNavLocation Projected;
	if (Nav && !Nav->ProjectPointToNavigation(Hit.ImpactPoint, Projected, FVector(NavSnapCm, NavSnapCm, 200.f)))
	{
		FormationPlacement.CancelPress();
		LastFormationPlacementResult = EFormationPlacementResult::None;
		PostHeadquarters(LOCTEXT("FormationPlacementNotWalkable", "Nobody can stand there."));
		return true;
	}
	FormationPressZ = static_cast<float>(Hit.ImpactPoint.Z);
	return true;
}

bool ACodexTacticsPlayerController::HandleFormationPlacementRelease(const FVector& Ground)
{
	if (!FormationPlacement.IsActive())
	{
		return false;
	}
	if (!FormationPlacement.GetPressPoint().IsSet())
	{
		return true; // a selection click: nothing to place
	}
	const TOptional<int32> Selected = FormationPlacement.GetSelected();
	LastFormationPlacementResult = FormationPlacement.Release(FVector2D(Ground), FormationTuning::Get());
	const int32 Squad = Selected.Get(-1);
	if (LastFormationPlacementResult == EFormationPlacementResult::Placed)
	{
		FormationGhostZ.Add(Squad, FormationPressZ);
	}
	SyncFormationGhosts();
	if (LastFormationPlacementResult == EFormationPlacementResult::TooFar || LastFormationPlacementResult == EFormationPlacementResult::TooClose)
	{
		if (AFormationGhostActor* Ghost = GetFormationGhost(Squad))
		{
			Ghost->FlashRefused();
		}
		PostHeadquarters(LastFormationPlacementResult == EFormationPlacementResult::TooFar
			? LOCTEXT("FormationPlacementTooFar", "Too far: everyone stays within 10 m of the leader.")
			: LOCTEXT("FormationPlacementTooClose", "Too close to another operative."));
	}
	else if (LastFormationPlacementResult == EFormationPlacementResult::Placed)
	{
		const FFormationCustomGhost* Ghost = FormationPlacement.FindGhost(Squad);
		UE_LOG(LogCodexTactics, Display, TEXT("[FormationCustom] #%d placed at (%.0f, %.0f) looking %.0f"), Squad, Ghost ? Ghost->Position.X : 0.0,
			Ghost ? Ghost->Position.Y : 0.0, Ghost ? Ghost->Yaw : 0.f);
	}
	return true;
}

void ACodexTacticsPlayerController::TickFormationPlacement(float RealDeltaSeconds)
{
	if (!FormationPlacement.IsActive())
	{
		return;
	}
	const USquadSubsystem* Squad = GetSquad();
	if (!Squad || Squad->IsSoloMode() || GetActiveTurnBased() || IsDialogueOpen() || !FormationRuntime::IsEnabled() || Squad->GetMembers().Num() < 2)
	{
		CancelFormationPlacement(); // the squad split / a fight's grid / a dialogue took over
		return;
	}
	// Another system may have set the world clock meanwhile (a game flow state change): the placement pause holds.
	if (UWorld* World = GetWorld(); World && !FMath::IsNearlyEqual(UGameplayStatics::GetGlobalTimeDilation(World), FormationPlacementTimeDilation, 1e-6f))
	{
		UGameplayStatics::SetGlobalTimeDilation(World, FormationPlacementTimeDilation);
	}
	for (AFormationGhostActor* Ghost : FormationGhosts)
	{
		if (Ghost)
		{
			Ghost->TickFlash(RealDeltaSeconds);
		}
	}
}

bool ACodexTacticsPlayerController::GetFormationPlacementView(FFormationPlacementView& Out) const
{
	if (!FormationPlacement.IsActive())
	{
		return false;
	}
	Out = FFormationPlacementView();
	Out.Slot = FormationPlacement.GetSlot();
	const TOptional<int32> Selected = FormationPlacement.GetSelected();
	for (const FFormationCustomGhost& Ghost : FormationPlacement.GetGhosts())
	{
		FFormationPlacementGhostView& View = Out.Ghosts.AddDefaulted_GetRef();
		const float* Z = FormationGhostZ.Find(Ghost.Squad);
		View.Feet = FVector(Ghost.Position, Z ? *Z : 0.f);
		View.Yaw = Ghost.Yaw;
		View.bSelected = Selected.IsSet() && Selected.GetValue() == Ghost.Squad;
		View.bAnchor = Ghost.Squad == FormationPlacement.GetAnchorSquad();
	}
	if (Selected.IsSet())
	{
		if (const TWeakObjectPtr<AOperativeCharacter>* Member = FormationPlacementMembers.Find(Selected.GetValue()); Member && Member->IsValid())
		{
			Out.SelectedName = Member->Get()->DisplayName;
		}
	}
	const TOptional<FVector2D> Press = FormationPlacement.GetPressPoint();
	if (Press.IsSet() && Selected.IsSet())
	{
		const float PlaneZ = FormationPressZ;
		Out.PressFeet = FVector(Press.GetValue(), PlaneZ);
		FVector Cursor;
		Out.PressLook = FormationPlacementGround(Cursor, PlaneZ) ? FormationPlacement.PreviewLook(FVector2D(Cursor)).Get(FormationPlacement.DefaultYaw())
			: FormationPlacement.DefaultYaw();
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
