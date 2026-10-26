// ACodexTacticsPlayerController: selection - squad number keys, clicked member, LMB press / release, box select, Solo.
// Split out of CodexTacticsPlayerController.cpp verbatim (cleanup 2026-10-15; map: docs/port/REFACTOR_CONTROLLER_2026-10.md).
// Godot reference: Scenes/movements/main.gd (_input and the click / key handlers it dispatches to).

#include "Core/CodexTacticsPlayerController.h"
#include "Core/CodexTacticsPlayerControllerInternal.h"
#include "Combat/HordeIntroSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/GrenadeSubsystem.h"
#include "Characters/SquadTransferSubsystem.h"
#include "Core/PlayerCommands.h"
#include "UI/CodexTacticsHUD.h"
#include "UI/ProfileDialogWidget.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "Interactables/InteractableActor.h"
#include "Interactables/RelocationSubsystem.h"
#include "UI/GameMessageSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Engine/World.h"
#include "Vehicles/VehicleSubsystem.h"

#define LOCTEXT_NAMESPACE "CodexTacticsPlayerController"

bool ACodexTacticsPlayerController::HandleSquadNumberKey(const FKey& Key)
{
	const int32 Index = Key == EKeys::One ? 0 : (Key == EKeys::Two ? 1 : (Key == EKeys::Three ? 2 : (Key == EKeys::Four ? 3 : INDEX_NONE)));
	if (Index == INDEX_NONE)
	{
		return false;
	}
	SelectMember(Index);
	return true;
}

AOperativeCharacter* ACodexTacticsPlayerController::FindClickedMember(const FHitResult& Hit) const
{
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	if (!Squad)
	{
		return nullptr;
	}
	const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
	// 1. Direct hit check or owner check on operative
	AActor* HitActor = Hit.GetActor();
	AOperativeCharacter* HitOperative = Cast<AOperativeCharacter>(HitActor);
	if (!HitOperative && HitActor)
	{
		HitOperative = Cast<AOperativeCharacter>(HitActor->GetOwner());
	}
	if (HitOperative && Members.Contains(HitOperative))
	{
		return HitOperative;
	}
	// 2. Proximity check around cursor impact point (not with a group selected: a click next to one of them is the
	// group's move order — only a click on an operative's body picks him). 2026-10-22 (game designer: a failed defuse - "clicking the
	// mine again does nothing"): not on a world object either - a click ON a mine / wire / crate means it, even with an operative
	// working right next to it.
	AOperativeCharacter* Closest = nullptr;
	if (!Squad->HasMultiSelection() && !Cast<AInteractableActor>(HitActor))
	{
		float ClosestDist = SelectRadius;
		for (AOperativeCharacter* Member : Members)
		{
			if (VehicleQueries::IsMounted(Member))
			{
				continue; // inside a vehicle: picked with 1-4, never by a click near the hull
			}
			const float Dist = FVector::Dist2D(Member->GetActorLocation(), Hit.ImpactPoint);
			if (Dist <= ClosestDist)
			{
				ClosestDist = Dist;
				Closest = Member;
			}
		}
	}
	return Closest;
}

void ACodexTacticsPlayerController::OnClick()
{
	if (IsDialogueOpen())
	{
		return; // the dialogue panel handles its own clicks
	}
	// Minimap (2026-10-18): a click on the corner map / its header / the full map belongs to the map (camera there).
	if (FVector2D MapMouse; GetMousePosition(MapMouse.X, MapMouse.Y) && HandleMapClick(MapMouse, false))
	{
		return;
	}
	// A click skips the horde flyover (user decision 2026-10-11) and is not an order; before the trigger it is a normal click.
	if (UHordeIntroSubsystem* HordeIntro = GetWorld()->GetSubsystem<UHordeIntroSubsystem>(); HordeIntro && HordeIntro->HandleSkipInput(false))
	{
		return;
	}
	// Real-time fight without orders (user decision 2026-10-05): a click only picks an operative.
	const bool bOrdersLocked = IsRealTimeOrderLocked();
	// Item hand-over: LMB on a squad mate (Godot _handle_transfer_click).
	if (USquadTransferSubsystem* Transfer = GetWorld()->GetSubsystem<USquadTransferSubsystem>(); Transfer && Transfer->IsTransferring() && !bOrdersLocked)
	{
		FHitResult Hit;
		if (GetHitResultUnderCursor(ECC_Visibility, false, Hit))
		{
			Transfer->HandleClick(Hit.ImpactPoint, Hit.GetActor());
		}
		return;
	}
	// Suppressive fire (2026-10-26): with the suppress aim armed (7 / SUPPRESS) the click is the SUPPRESS order (an enemy or a spot).
	if (bSuppressAimArmed && !bOrdersLocked)
	{
		bSuppressAimArmed = false;
		FHitResult Hit;
		if (GetHitResultUnderCursor(ECC_Visibility, false, Hit))
		{
			IssueSuppress(Hit);
		}
		return;
	}
	// Grenade aim: LMB throws (Godot _handle_grenade_throw_click).
	if (UGrenadeSubsystem* Grenades = GetWorld()->GetSubsystem<UGrenadeSubsystem>(); Grenades && Grenades->IsAiming() && !bOrdersLocked)
	{
		FVector Point;
		// Stealth lure: the stone aim throws a stone ("Stealth.Throw" once it flies).
		if (Grenades->IsAimingLure())
		{
			AOperativeCharacter* LureThrower = Grenades->GetThrower();
			if (GetGrenadeAimPoint(Point) && Grenades->ThrowLureAtCursor(Point))
			{
				ControllerCommands::Report(this, PlayerCommands::StealthThrow, nullptr, false, false, LureThrower, Point);
			}
			return;
		}
		if (GetGrenadeAimPoint(Point))
		{
			Grenades->ThrowAtCursor(Point);
			ControllerCommands::Report(this, PlayerCommands::GrenadeThrow, nullptr, false, false, nullptr, Point);
		}
		return;
	}
	// Turn-based combat: a turret / barricade / mine is set up on the clicked cell at once (Godot
	// _handle_tactical_deployable_placement), the operative walks up and pays AP.
	if (URelocationSubsystem* Relocation = GetPlacingRelocation(); Relocation && Relocation->IsPlacingDeployable())
	{
		if (UTurnBasedCombatSubsystem* TurnBased = GetActiveTurnBased())
		{
			FVector Point;
			const EDeployableType PlacedType = Relocation->GetPlacingType();
			if (GetPlacementPoint(Point) && TurnBased->HandleDeployPlacement(PlacedType, Point, Relocation->GetPlacingYaw()))
			{
				Relocation->CancelPlacement();
				ControllerCommands::Report(this, ControllerCommands::ForDeployable(PlacedType), nullptr, false, false, TurnBased->GetActiveUnit(), Point);
			}
			return;
		}
	}
	// Placement mode: LMB sets the new spot of the object being moved.
	if (URelocationSubsystem* Relocation = bOrdersLocked ? nullptr : GetPlacingRelocation())
	{
		FVector Point;
		if (GetPlacementPoint(Point))
		{
			Relocation->ConfirmPlacement(Point);
		}
		return;
	}

	// A world click happens on release, unless the cursor was dragged into a selection box (Godot main.gd LMB).
	bLmbDown = true;
	bBoxSelecting = false;
	GetMousePosition(BoxStart.X, BoxStart.Y);
	BoxCurrent = BoxStart;
}

void ACodexTacticsPlayerController::OnClickReleased()
{
	if (!bLmbDown)
	{
		return;
	}
	bLmbDown = false;
	if (bBoxSelecting)
	{
		bBoxSelecting = false;
		SelectInBox(FVector2D::Min(BoxStart, BoxCurrent), FVector2D::Max(BoxStart, BoxCurrent));
		return;
	}
	FHitResult Hit;
	if (GetSquad() && GetSquad()->GetLeader() && GetHitResultUnderCursor(ECC_Visibility, false, Hit))
	{
		HandleWorldHit(Hit);
	}
}

bool ACodexTacticsPlayerController::GetSelectionBox(FVector2D& OutMin, FVector2D& OutMax) const
{
	if (!bBoxSelecting)
	{
		return false;
	}
	OutMin = FVector2D::Min(BoxStart, BoxCurrent);
	OutMax = FVector2D::Max(BoxStart, BoxCurrent);
	return true;
}

int32 ACodexTacticsPlayerController::SelectInBox(const FVector2D& Min, const FVector2D& Max)
{
	USquadSubsystem* Squad = GetSquad();
	if (!Squad)
	{
		return 0;
	}
	const FBox2D Box(Min, Max);
	TArray<AOperativeCharacter*> Selected;
	for (AOperativeCharacter* Member : Squad->GetMembers())
	{
		if (!Member || !Member->HealthComponent || !Member->HealthComponent->IsAlive())
		{
			continue;
		}
		const FVector Feet = Member->GetActorLocation() - FVector(0.f, 0.f, Member->GetSimpleCollisionHalfHeight());
		for (const float Height : { 0.f, 90.f, 180.f })
		{
			FVector2D Screen;
			if (ProjectWorldLocationToScreen(Feet + FVector(0.f, 0.f, Height), Screen) && Box.IsInside(Screen))
			{
				Selected.Add(Member);
				break;
			}
		}
	}
	if (Selected.IsEmpty())
	{
		return 0;
	}
	Squad->SetSelectedGroup(Selected);
	ControllerCommands::Report(this, PlayerCommands::SelectBox);
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(LOCTEXT("SquadSender", "SQUAD"), Selected.Num() == 1
			? FText::Format(LOCTEXT("UnitSelected", "👤 Operative selected: {0}"), Selected[0]->DisplayName)
			: FText::Format(LOCTEXT("GroupSelected", "👥 Group selected: {0} operatives"), Selected.Num()));
	}
	return Selected.Num();
}

void ACodexTacticsPlayerController::SelectMember(int32 RosterIndex)
{
	if (UTurnBasedCombatSubsystem* TurnBased = GetActiveTurnBased())
	{
		USquadSubsystem* SquadSystem = GetSquad();
		for (AOperativeCharacter* Member : SquadSystem ? SquadSystem->GetMembers() : TArray<AOperativeCharacter*>())
		{
			if (Member->SquadIndex == RosterIndex)
			{
				TurnBased->SelectUnit(Member);
				ControllerCommands::Report(this, PlayerCommands::SelectMember, nullptr, false, false, Member);
			}
		}
		return;
	}
	if (USquadSubsystem* Squad = GetSquad())
	{
		// Godot _select_squad_member_by_index: the leader's own number opens the profile; another member takes the
		// lead and an open profile follows him.
		const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
		AOperativeCharacter* Target = Members.IsValidIndex(RosterIndex) ? Members[RosterIndex] : nullptr;
		ACodexTacticsHUD* Hud = GetHUD<ACodexTacticsHUD>();
		if (Target && Target == Squad->GetLeader())
		{
			if (Hud)
			{
				Hud->OpenProfileDialog(Target);
			}
			return;
		}
		if (Squad->SetLeaderByIndex(RosterIndex))
		{
			ControllerCommands::Report(this, PlayerCommands::SelectMember);
			if (Hud && Hud->GetProfileDialog() && Hud->GetProfileDialog()->IsOpen())
			{
				Hud->OpenProfileDialog(Squad->GetLeader());
			}
			if (AOperativeCharacter* NewLeader = Squad->GetLeader())
			{
				if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
				{
					Messages->PostMessage(LOCTEXT("SquadSpeaker", "SQUAD"),
						FText::Format(LOCTEXT("UnitSelected", "👤 Operative selected: {0}"), NewLeader->DisplayName));
				}
			}
		}
	}
}

void ACodexTacticsPlayerController::ToggleSoloMode()
{
	if (BlockRealTimeOrder())
	{
		return;
	}
	if (USquadSubsystem* Squad = GetSquad())
	{
		if (RefuseIfLocked(Squad->IsSoloMode() ? PlayerCommands::Regroup : PlayerCommands::SoloOn))
		{
			return;
		}
		Squad->ToggleSoloMode();
		ControllerCommands::Report(this, Squad->IsSoloMode() ? PlayerCommands::SoloOn : PlayerCommands::Regroup);
	}
}

#undef LOCTEXT_NAMESPACE
