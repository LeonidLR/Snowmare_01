// ACodexTacticsPlayerController: items - grenade / flare aim, personal items, deployable placement (rotate, set up),
// weapon cycle, relocation pick mode.
// Split out of CodexTacticsPlayerController.cpp verbatim (cleanup 2026-10-15; map: docs/port/REFACTOR_CONTROLLER_2026-10.md).
// Godot reference: Scenes/movements/main.gd (_input and the click / key handlers it dispatches to).

#include "Core/CodexTacticsPlayerController.h"
#include "Core/CodexTacticsPlayerControllerInternal.h"
#include "Combat/GrenadeSubsystem.h"
#include "Data/TacticalHeatTuning.h"
#include "Survival/TacticalHeatSubsystem.h"
#include "Data/WeaponDataAsset.h"
#include "Core/PlayerCommands.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "Tutorial/TutorialSubsystem.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Interactables/RelocationSubsystem.h"
#include "UI/GameMessageSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "CodexTacticsPlayerController"

URelocationSubsystem* ACodexTacticsPlayerController::GetPlacingRelocation() const
{
	URelocationSubsystem* Relocation = GetWorld()->GetSubsystem<URelocationSubsystem>();
	return Relocation && Relocation->IsPlacing() ? Relocation : nullptr;
}

bool ACodexTacticsPlayerController::GetGrenadeAimPoint(FVector& OutPoint) const
{
	const UGrenadeSubsystem* Grenades = GetWorld()->GetSubsystem<UGrenadeSubsystem>();
	const AOperativeCharacter* Thrower = Grenades ? Grenades->GetThrower() : nullptr;
	if (!Thrower)
	{
		return false;
	}
	// Godot: the cursor ray hit, else the floor plane at the thrower.
	FHitResult Hit;
	if (GetHitResultUnderCursor(ECC_Visibility, false, Hit))
	{
		OutPoint = Hit.ImpactPoint;
		return true;
	}
	FVector Origin;
	FVector Direction;
	if (!DeprojectMousePositionToWorld(Origin, Direction) || FMath::IsNearlyZero(Direction.Z))
	{
		return false;
	}
	const float FloorZ = Thrower->GetActorLocation().Z - Thrower->GetSimpleCollisionHalfHeight();
	const float Distance = (FloorZ - Origin.Z) / Direction.Z;
	if (Distance <= 0.f)
	{
		return false;
	}
	OutPoint = Origin + Direction * Distance;
	return true;
}

bool ACodexTacticsPlayerController::CancelGrenadeAim()
{
	UGrenadeSubsystem* Grenades = GetWorld()->GetSubsystem<UGrenadeSubsystem>();
	if (!Grenades || !Grenades->IsAiming())
	{
		return false;
	}
	Grenades->CancelAim(true);
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(LOCTEXT("GrenadeSpeaker", "Grenade"), LOCTEXT("GrenadeCancelled", "Throw cancelled."));
	}
	return true;
}

void ACodexTacticsPlayerController::UseSquadItem(EPersonalItem Item)
{
	USquadSubsystem* Squad = GetSquad();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>();
	if (!Leader || !Messages || BlockRealTimeOrder())
	{
		return;
	}
	// Tutorial lock (Demo 01, 2026-10-13): items stay locked until the heat pack lesson ("Item.HeatPack" / "Item.Use").
	if (UTutorialSubsystem::CheckCommandLocked(this, Item == EPersonalItem::HeatPack ? PlayerCommands::UseHeatPack : PlayerCommands::UseItem))
	{
		return;
	}
	if (Item == EPersonalItem::HeatPack)
	{
		// Sprint 15: the heat pack has its own lines (cold off, buffer, turn-based AP).
		UTacticalHeatSubsystem* Heat = GetWorld()->GetSubsystem<UTacticalHeatSubsystem>();
		EHeatPackRefusal Refusal = EHeatPackRefusal::None;
		if (!Heat)
		{
			return;
		}
		if (Heat->UseHeatPack(Leader, Leader, &Refusal))
		{
			ControllerCommands::Report(this, PlayerCommands::UseHeatPack);
			return;
		}
		switch (Refusal)
		{
		case EHeatPackRefusal::NoPack:
			Messages->PostMessage(Leader->DisplayName, FText::Format(LOCTEXT("ItemMissingPack", "{0} has no {1} in personal inventory!"),
				Leader->DisplayName, PersonalItemRules::GetMissingName(Item)));
			break;
		case EHeatPackRefusal::NotYourTurn:
			Messages->PostMessage(Leader->DisplayName, LOCTEXT("PackNotTurn", "Only the operative whose turn it is can use a heat pack."));
			break;
		case EHeatPackRefusal::NotEnoughAP:
			Messages->PostMessage(Leader->DisplayName, FText::Format(LOCTEXT("PackNoAP", "Not enough AP for a heat pack ({0} needed)."),
				TacticalHeatTuning::Get().HeatPackActionPoints));
			break;
		default:
			Messages->PostMessage(Leader->DisplayName, LOCTEXT("PackWarm", "No need for a heat pack - not cold at all."));
			break;
		}
		return;
	}
	if (Leader->UsePersonalItem(Item))
	{
		ControllerCommands::Report(this, PlayerCommands::UseItem);
		Messages->PostMessage(Leader->DisplayName, FText::Format(LOCTEXT("ItemUsed", "Used {0} (+HP / warmth)! (Left: x{1})"),
			PersonalItemRules::GetName(Item), Leader->GetItemCount(Item)));
		// Turn-based: a medkit ends the operative's turn (user decision 2026-10-04).
		if (Item == EPersonalItem::Medkit)
		{
			if (UTurnBasedCombatSubsystem* TurnBased = GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>(); TurnBased && TurnBased->IsActive())
			{
				TurnBased->EndTurnAfterMedkit(Leader);
			}
		}
	}
	else if (Leader->GetItemCount(Item) <= 0)
	{
		Messages->PostMessage(Leader->DisplayName, FText::Format(LOCTEXT("ItemMissing", "{0} has no {1} in personal inventory!"),
			Leader->DisplayName, PersonalItemRules::GetMissingName(Item)));
	}
	else
	{
		Messages->PostMessage(Leader->DisplayName, LOCTEXT("ItemFull", "Operative's health and warmth are already 100%!"));
	}
}

void ACodexTacticsPlayerController::StartPlacementForType(EDeployableType Type)
{
	USquadSubsystem* Squad = GetSquad();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	URelocationSubsystem* Relocation = GetWorld()->GetSubsystem<URelocationSubsystem>();
	UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>();
	if (!Leader || !Relocation || !Messages || BlockRealTimeOrder())
	{
		return;
	}
	Leader->SelectedDeployType = Type;
	const FText Name = URelocationSubsystem::GetDeployableName(Type);
	if (Leader->GetDeployableCount(Type) <= 0)
	{
		AOperativeCharacter* Carrier = nullptr;
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (Member != Leader && Member->GetDeployableCount(Type) > 0)
			{
				Carrier = Member;
				break;
			}
		}
		if (!Carrier)
		{
			Messages->PostMessage(Leader->DisplayName, FText::Format(LOCTEXT("NoneInSquad", "⚠️ The squad has no {0} left!"), Name));
			return;
		}
		Carrier->MoveDeployableTo(*Leader, Type);
		Messages->PostMessage(Leader->DisplayName, FText::Format(LOCTEXT("HandsOver", "🛠️ {0} hands {1} to {2} for placement!"),
			Carrier->DisplayName, Name, Leader->DisplayName));
	}
	Relocation->StartDeployPlacement(Type, Leader);
}

void ACodexTacticsPlayerController::StartTripwirePlacement()
{
	USquadSubsystem* Squad = GetSquad();
	URelocationSubsystem* Relocation = GetWorld()->GetSubsystem<URelocationSubsystem>();
	if (!Squad || !Squad->GetLeader() || !Relocation || BlockRealTimeOrder())
	{
		return;
	}
	Relocation->StartTripwirePlacement(Squad->GetLeader());
}

void ACodexTacticsPlayerController::StartFlareAim()
{
	USquadSubsystem* Squad = GetSquad();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	UGrenadeSubsystem* Grenades = GetWorld()->GetSubsystem<UGrenadeSubsystem>();
	if (!Leader || !Grenades || BlockRealTimeOrder() || RefuseIfLocked(PlayerCommands::FlareAim))
	{
		return;
	}
	if (Grenades->IsAimingFlare())
	{
		Grenades->CancelAim(false);
		return;
	}
	Grenades->StartFlareAim(Leader);
	ControllerCommands::Report(this, PlayerCommands::FlareAim);
}

void ACodexTacticsPlayerController::GrenadeKey()
{
	// Sprint 15: Shift + G = the signal flare (same slot).
	if (IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift))
	{
		StartFlareAim();
		return;
	}
	USquadSubsystem* Squad = GetSquad();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	UGrenadeSubsystem* Grenades = GetWorld()->GetSubsystem<UGrenadeSubsystem>();
	UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>();
	if (!Leader || !Grenades || !Messages || BlockRealTimeOrder() || RefuseIfLocked(PlayerCommands::GrenadeAim))
	{
		return;
	}
	UTurnBasedCombatSubsystem* TurnBased = GetActiveTurnBased();
	const bool bGrenadeInHands = Leader->CurrentWeapon && Leader->CurrentWeapon->WeaponId == TEXT("grenade");
	if (bGrenadeInHands)
	{
		if (Grenades->IsAiming())
		{
			Grenades->CancelAim(true);
			return;
		}
		const bool bSwitched = TurnBased ? (TurnBased->SwitchActiveUnitWeapon(TEXT("m16")) || TurnBased->SwitchActiveUnitWeapon(TEXT("pistol")))
			: (Leader->SwitchToWeaponById(TEXT("m16")) || Leader->SwitchToWeaponById(TEXT("pistol")) || Leader->SwitchToWeaponById(TEXT("knife")));
		if (bSwitched)
		{
			const FString Ammo = Leader->UsesAmmo() ? FString::Printf(TEXT("%d / %d"), Leader->CurrentClip, Leader->ReserveAmmo) : TEXT("∞");
			Messages->PostMessage(Leader->DisplayName, FText::Format(LOCTEXT("WeaponBack", "🔫 Weapon: {0} [{1}] (Damage: {2})"),
				Leader->CurrentWeapon ? Leader->CurrentWeapon->WeaponName : FText::GetEmpty(), FText::FromString(Ammo),
				Leader->CurrentWeapon ? FMath::FloorToInt(Leader->CurrentWeapon->BaseDamage) : 0));
		}
		return;
	}
	if (Leader->GrenadesCount <= 0)
	{
		Messages->PostMessage(Leader->DisplayName, LOCTEXT("NoGrenade", "🧨 The operative has no grenades!"));
		return;
	}
	if (TurnBased)
	{
		TurnBased->SwitchActiveUnitWeapon(TEXT("grenade")); // a grid weapon in turn-based combat
	}
	else
	{
		Leader->SwitchToWeaponById(TEXT("grenade"));
		Grenades->StartAim(Leader);
	}
	ControllerCommands::Report(this, PlayerCommands::GrenadeAim);
	Messages->PostMessage(Leader->DisplayName, FText::Format(LOCTEXT("GrenadeTaken", "🧨 Grenade selected [x{0}] (Damage: {1}, Radius: {2}m)"),
		Leader->GrenadesCount, FMath::FloorToInt(Leader->GrenadeDamage),
		FText::AsNumber(Leader->GrenadeEffectRadius / 100.f, &FNumberFormattingOptions().SetMinimumFractionalDigits(1).SetMaximumFractionalDigits(1))));
}

bool ACodexTacticsPlayerController::GetPlacementPoint(FVector& OutPoint) const
{
	const URelocationSubsystem* Relocation = GetWorld()->GetSubsystem<URelocationSubsystem>();
	FVector Origin;
	FVector Direction;
	if (!Relocation || !DeprojectMousePositionToWorld(Origin, Direction) || FMath::IsNearlyZero(Direction.Z))
	{
		return false;
	}
	// Godot intersects the cursor ray with the ground plane (Plane(Vector3.UP, 0)); here: the object's floor height.
	const float Distance = (Relocation->GetPlacementGroundZ() - Origin.Z) / Direction.Z;
	if (Distance <= 0.f)
	{
		return false;
	}
	OutPoint = Origin + Direction * Distance;
	return true;
}

void ACodexTacticsPlayerController::DeployAbility()
{
	// Godot main.gd KEY_F in the turn-based fight: the weapon aim mode (the deployables come from the action bar there).
	if (UTurnBasedCombatSubsystem* AttackTurnBased = GetActiveTurnBased())
	{
		if (!AttackTurnBased->IsBusy())
		{
			AttackTurnBased->ToggleAttackMode();
		}
		return;
	}
	if (BlockRealTimeOrder() || RefuseIfLocked(PlayerCommands::DeployStart))
	{
		return;
	}
	USquadSubsystem* Squad = GetSquad();
	URelocationSubsystem* Relocation = GetWorld()->GetSubsystem<URelocationSubsystem>();
	// Turn-based combat: the active operative sets it up (Godot current_leader follows the selected unit).
	const UTurnBasedCombatSubsystem* TurnBased = GetActiveTurnBased();
	AOperativeCharacter* Worker = TurnBased && TurnBased->GetActiveUnit() ? TurnBased->GetActiveUnit() : (Squad ? Squad->GetLeader() : nullptr);
	if (Relocation && Worker)
	{
		Relocation->HandleDeployKey(Worker);
		ControllerCommands::Report(this, PlayerCommands::DeployStart, nullptr, false, false, Worker);
	}
}

void ACodexTacticsPlayerController::RotatePlacement()
{
	if (URelocationSubsystem* Relocation = GetPlacingRelocation())
	{
		Relocation->RotatePreview(+1);
	}
	else if (UTurnBasedCombatSubsystem* TurnBased = GetActiveTurnBased())
	{
		if (TurnBased->IsRelocating())
		{
			TurnBased->RotateRelocation(+1);
		}
		else
		{
			TurnBased->RotateActiveUnitClockwise();
		}
	}
	else if (IsDialogueOpen())
	{
		return;
	}
	else if (RefuseIfLocked(PlayerCommands::Reload))
	{
		return;
	}
	else if (AOperativeCharacter* Leader = GetSquad() ? GetSquad()->GetLeader() : nullptr)
	{
		// Godot main.gd KEY_R outside placement / the grid fight: reload.
		Leader->StartReload();
		if (Leader->bIsReloading)
		{
			ControllerCommands::Report(this, PlayerCommands::Reload);
			if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
			{
				Messages->PostMessage(Leader->DisplayName, FText::Format(LOCTEXT("Reloading", "🔄 Reloading {0}..."),
					Leader->CurrentWeapon ? Leader->CurrentWeapon->WeaponName : FText::GetEmpty()));
			}
		}
	}
}

void ACodexTacticsPlayerController::CycleWeaponKey()
{
	// Godot main.gd KEY_X -> player.gd switch_weapon: the next arsenal weapon, with the grenade aim when it is the grenade.
	USquadSubsystem* Squad = GetSquad();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (!Leader || Leader->AvailableWeapons.IsEmpty() || GetActiveTurnBased() || IsDialogueOpen() || BlockRealTimeOrder()
		|| RefuseIfLocked(PlayerCommands::CycleWeapon))
	{
		return;
	}
	const int32 Current = Leader->AvailableWeapons.IndexOfByKey(Leader->CurrentWeapon);
	const UWeaponDataAsset* Next = Leader->AvailableWeapons[(Current + 1) % Leader->AvailableWeapons.Num()];
	if (!Next || !Leader->SwitchToWeaponById(Next->WeaponId))
	{
		return;
	}
	ControllerCommands::Report(this, PlayerCommands::CycleWeapon);
	UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>();
	UGrenadeSubsystem* Grenades = GetWorld()->GetSubsystem<UGrenadeSubsystem>();
	if (Messages)
	{
		const FString Ammo = Leader->UsesAmmo() ? FString::Printf(TEXT("%d / %d"), Leader->CurrentClip, Leader->ReserveAmmo) : TEXT("∞");
		Messages->PostMessage(Leader->DisplayName, FText::Format(LOCTEXT("WeaponCycle", "🔫 Weapon: {0} [{1}] (Damage: {2})"),
			Next->WeaponName, FText::FromString(Ammo), FMath::FloorToInt(Next->BaseDamage)));
	}
	if (Next->WeaponId == TEXT("grenade"))
	{
		if (Leader->GrenadesCount > 0 && Grenades)
		{
			Grenades->StartAim(Leader);
		}
		else if (Messages)
		{
			Messages->PostMessage(Leader->DisplayName, LOCTEXT("GrenadesOut", "🧨 Out of grenades!"));
		}
	}
	else if (Grenades && Grenades->IsAiming())
	{
		Grenades->CancelAim(false);
	}
}

void ACodexTacticsPlayerController::ToggleRelocateSelectMode()
{
	URelocationSubsystem* Relocation = GetWorld()->GetSubsystem<URelocationSubsystem>();
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>();
	if (BlockRealTimeOrder() || RefuseIfLocked(PlayerCommands::Relocate))
	{
		return;
	}
	if (Relocation && !Relocation->CanRelocateNow())
	{
		PostHeadquarters(LOCTEXT("RelocateCombat", "⚠️ Objects cannot be relocated during combat! Use tactical pause [SPACE]."));
		return;
	}
	if (bRelocateSelectMode || (Relocation && Relocation->IsPlacing()))
	{
		bRelocateSelectMode = false;
		if (Relocation && Relocation->IsPlacing())
		{
			Relocation->CancelPlacement();
		}
		if (Messages)
		{
			Messages->PostMessage(LOCTEXT("Engineering", "ENGINEERING"), LOCTEXT("RelocateCancelled", "Relocate mode cancelled."));
		}
		return;
	}
	bRelocateSelectMode = true;
	if (Messages)
	{
		Messages->PostMessage(LOCTEXT("Engineering", "ENGINEERING"),
			LOCTEXT("RelocatePick", "📦 [RELOCATE] 1️⃣ Click the object to move (barrel, barricade, crate, turret, mine)."));
	}
}

#undef LOCTEXT_NAMESPACE
