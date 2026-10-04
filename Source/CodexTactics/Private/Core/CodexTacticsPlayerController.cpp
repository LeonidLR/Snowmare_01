#include "Core/CodexTacticsPlayerController.h"
#include "Characters/SquadFormation.h"
#include "Camera/TacticalCameraPawn.h"
#include "Characters/RecruitSubsystem.h"
#include "Combat/HoldSphereActor.h"
#include "Interactables/DeployableActor.h"
#include "UI/FloatingTextSubsystem.h"
#include "Combat/HealthComponent.h"
#include "CodexTactics.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Combat/GrenadeSubsystem.h"
#include "Characters/SquadTransferSubsystem.h"
#include "Data/WeaponDataAsset.h"
#include "Combat/EncounterQueries.h"
#include "Core/MissionSubsystem.h"
#include "Core/SaveGameSubsystem.h"
#include "UI/SaveLoadDialogWidget.h"
#include "UI/PauseMenuWidget.h"
#include "UI/CodexTacticsHUD.h"
#include "UI/ProfileDialogWidget.h"
#include "UI/DialogueSubsystem.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "Interactables/LootCrateActor.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Interactables/InteractableActor.h"
#include "Interactables/InteractionSubsystem.h"
#include "Interactables/RelocationRules.h"
#include "Interactables/RelocationSubsystem.h"
#include "Misc/App.h"
#include "UI/GameMessageSubsystem.h"

#define LOCTEXT_NAMESPACE "CodexTacticsPlayerController"
#include "InputActionValue.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "InputAction.h"
#include "InputMappingContext.h"

ACodexTacticsPlayerController::ACodexTacticsPlayerController()
{
	// Top-down tactical game: cursor is always visible.
	bShowMouseCursor = true;
}

void ACodexTacticsPlayerController::BeginPlay()
{
	Super::BeginPlay();

	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);

	if (MappingContext)
	{
		if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				InputSubsystem->AddMappingContext(MappingContext, 0);
			}
		}
	}
}

void ACodexTacticsPlayerController::CreateInputActions()
{
	MappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Squad"));

	auto MakeAction = [this](const TCHAR* Name, FKey Key) -> UInputAction*
	{
		UInputAction* Action = NewObject<UInputAction>(this, Name);
		Action->ValueType = EInputActionValueType::Boolean;
		MappingContext->MapKey(Action, Key);
		return Action;
	};

	ClickAction = MakeAction(TEXT("IA_Click"), EKeys::LeftMouseButton);
	SelectActions = {
		MakeAction(TEXT("IA_SelectMember1"), EKeys::One),
		MakeAction(TEXT("IA_SelectMember2"), EKeys::Two),
		MakeAction(TEXT("IA_SelectMember3"), EKeys::Three),
		MakeAction(TEXT("IA_SelectMember4"), EKeys::Four) };
	StanceActions = {
		MakeAction(TEXT("IA_StanceStand"), EKeys::Z),
		MakeAction(TEXT("IA_StanceCrouch"), EKeys::C),
		MakeAction(TEXT("IA_StanceProne"), EKeys::V) };
	SoloModeAction = MakeAction(TEXT("IA_SoloMode"), EKeys::B);

	CameraRotateLeftAction = MakeAction(TEXT("IA_CameraRotateLeft"), EKeys::Q);
	MappingContext->MapKey(CameraRotateLeftAction, EKeys::Left);
	CameraRotateRightAction = MakeAction(TEXT("IA_CameraRotateRight"), EKeys::E);
	MappingContext->MapKey(CameraRotateRightAction, EKeys::Right);
	CameraDragRotateAction = MakeAction(TEXT("IA_CameraDragRotate"), EKeys::RightMouseButton);
	CameraDragPanAction = MakeAction(TEXT("IA_CameraDragPan"), EKeys::MiddleMouseButton);
	SpaceAction = MakeAction(TEXT("IA_Space"), EKeys::SpaceBar);
	RotatePlacementAction = MakeAction(TEXT("IA_RotatePlacement"), EKeys::R);
	DeployAction = MakeAction(TEXT("IA_Deploy"), EKeys::F);
	GrenadeAction = MakeAction(TEXT("IA_Grenade"), EKeys::G);
	GuardAction = MakeAction(TEXT("IA_Guard"), EKeys::T);
	ItemActions = {
		MakeAction(TEXT("IA_UseMedkit"), EKeys::H),
		MakeAction(TEXT("IA_UseCannedFood"), EKeys::J),
		MakeAction(TEXT("IA_UseBread"), EKeys::K),
		MakeAction(TEXT("IA_UseChocolate"), EKeys::L) };
	QuickSaveAction = MakeAction(TEXT("IA_QuickSave"), EKeys::F5);
}

void ACodexTacticsPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	CreateInputActions();

	if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			InputSubsystem->ClearAllMappings();
			InputSubsystem->AddMappingContext(MappingContext, 0);
		}
	}

	// Mouse wheel via BindKey (architect decision: MouseWheelAxis is unreliable with the Slate cursor).
	// Keys (Z/C/V stances, B solo, 1-3 selection) go through Enhanced Input only; binding them here as well
	// would fire every press twice.
	InputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &ACodexTacticsPlayerController::OnMouseWheelUp);
	InputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &ACodexTacticsPlayerController::OnMouseWheelDown);
	// Ctrl + X: quick restart, handled before every game mode (Godot main.gd _unhandled_input).
	InputComponent->BindKey(FInputChord(EKeys::X, false, true, false, false), IE_Pressed, this, &ACodexTacticsPlayerController::RestartMission);
	// Plain X (the chord without Ctrl): Godot switch_weapon cycle.
	InputComponent->BindKey(FInputChord(EKeys::X), IE_Pressed, this, &ACodexTacticsPlayerController::CycleWeaponKey);
	InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &ACodexTacticsPlayerController::EnterPressed);
	InputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &ACodexTacticsPlayerController::TabPressed);
	InputComponent->BindKey(EKeys::P, IE_Pressed, this, &ACodexTacticsPlayerController::ProfilePressed);
	// Esc also closes the pause menu, so it runs while the world is paused.
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ACodexTacticsPlayerController::DialogueSkip).bExecuteWhenPaused = true;

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(InputComponent);
	if (!Input)
	{
		return;
	}
	Input->BindAction(ClickAction, ETriggerEvent::Started, this, &ACodexTacticsPlayerController::OnClick);
	Input->BindAction(ClickAction, ETriggerEvent::Completed, this, &ACodexTacticsPlayerController::OnClickReleased);
	Input->BindAction(SelectActions[0], ETriggerEvent::Started, this, &ACodexTacticsPlayerController::SelectMember1);
	Input->BindAction(SelectActions[1], ETriggerEvent::Started, this, &ACodexTacticsPlayerController::SelectMember2);
	Input->BindAction(SelectActions[2], ETriggerEvent::Started, this, &ACodexTacticsPlayerController::SelectMember3);
	Input->BindAction(SelectActions[3], ETriggerEvent::Started, this, &ACodexTacticsPlayerController::SelectMember4);
	Input->BindAction(StanceActions[0], ETriggerEvent::Started, this, &ACodexTacticsPlayerController::StanceStand);
	Input->BindAction(StanceActions[1], ETriggerEvent::Started, this, &ACodexTacticsPlayerController::StanceCrouch);
	Input->BindAction(StanceActions[2], ETriggerEvent::Started, this, &ACodexTacticsPlayerController::StanceProne);
	Input->BindAction(SoloModeAction, ETriggerEvent::Started, this, &ACodexTacticsPlayerController::ToggleSoloMode);

	Input->BindAction(CameraRotateLeftAction, ETriggerEvent::Started, this, &ACodexTacticsPlayerController::CameraRotateLeft);
	Input->BindAction(CameraRotateRightAction, ETriggerEvent::Started, this, &ACodexTacticsPlayerController::CameraRotateRight);
	Input->BindAction(CameraDragRotateAction, ETriggerEvent::Started, this, &ACodexTacticsPlayerController::CameraDragRotateStart);
	Input->BindAction(CameraDragRotateAction, ETriggerEvent::Completed, this, &ACodexTacticsPlayerController::CameraDragRotateStop);
	Input->BindAction(CameraDragPanAction, ETriggerEvent::Started, this, &ACodexTacticsPlayerController::CameraDragPanStart);
	Input->BindAction(CameraDragPanAction, ETriggerEvent::Completed, this, &ACodexTacticsPlayerController::CameraDragPanStop);
	Input->BindAction(SpaceAction, ETriggerEvent::Started, this, &ACodexTacticsPlayerController::SpacePressed);
	Input->BindAction(SpaceAction, ETriggerEvent::Completed, this, &ACodexTacticsPlayerController::SpaceReleased);
	Input->BindAction(RotatePlacementAction, ETriggerEvent::Started, this, &ACodexTacticsPlayerController::RotatePlacement);
	Input->BindAction(DeployAction, ETriggerEvent::Started, this, &ACodexTacticsPlayerController::DeployAbility);
	Input->BindAction(GrenadeAction, ETriggerEvent::Started, this, &ACodexTacticsPlayerController::GrenadeKey);
	Input->BindAction(GuardAction, ETriggerEvent::Started, this, &ACodexTacticsPlayerController::GuardKey);
	Input->BindAction(ItemActions[0], ETriggerEvent::Started, this, &ACodexTacticsPlayerController::UseMedkit);
	Input->BindAction(ItemActions[1], ETriggerEvent::Started, this, &ACodexTacticsPlayerController::UseCannedFood);
	Input->BindAction(ItemActions[2], ETriggerEvent::Started, this, &ACodexTacticsPlayerController::UseBread);
	Input->BindAction(ItemActions[3], ETriggerEvent::Started, this, &ACodexTacticsPlayerController::UseChocolate);
	Input->BindAction(QuickSaveAction, ETriggerEvent::Started, this, &ACodexTacticsPlayerController::QuickSaveKey);
}

void ACodexTacticsPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	// Selection box: LMB held and dragged past the threshold (not in the grid fight, where clicks are grid orders).
	if (bLmbDown)
	{
		GetMousePosition(BoxCurrent.X, BoxCurrent.Y);
		if (!bBoxSelecting && FVector2D::Distance(BoxStart, BoxCurrent) > BoxSelectThreshold && !GetActiveTurnBased())
		{
			bBoxSelecting = true;
		}
	}

	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	const float HoldDuration = Flow ? Flow->GetConfig().TurnBasedHoldDuration : 1.5f;
	// Space hold is measured in real time: the tactical pause slows the world down.
	const bool bTurnBasedNow = Flow && Flow->GetCombatMode() == ECodexCombatMode::TurnBased;
	if (SpaceInput.IsPressed())
	{
		// Godot: while Space is held the squad holds fire and (outside turn-based combat) the dome grows.
		SetSquadCeaseFire(!bTurnBasedNow);
		const AOperativeCharacter* HoldLeader = GetSquad() ? GetSquad()->GetLeader() : nullptr;
		if (!bTurnBasedNow && HoldLeader)
		{
			if (!HoldSphere)
			{
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				HoldSphere = GetWorld()->SpawnActor<AHoldSphereActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
			}
			if (HoldSphere)
			{
				const FVector Ground = HoldLeader->GetActorLocation() - FVector(0.f, 0.f, HoldLeader->GetSimpleCollisionHalfHeight());
				HoldSphere->UpdateProgress(Ground, SpaceInput.GetHeldTime(), HoldDuration);
			}
		}
		else if (HoldSphere)
		{
			HoldSphere->HideSphere();
		}
	}
	if (SpaceInput.Tick(static_cast<float>(FApp::GetDeltaTime()), HoldDuration) == ESpaceInputAction::Hold)
	{
		SetSquadCeaseFire(false);
		if (HoldSphere)
		{
			HoldSphere->HideSphere();
		}
		HandleSpaceHold();
	}

	// Turn-based: the cursor frame on the hovered cell and, in attack mode, its hit chance (Godot set_hovered_cell).
	if (UTurnBasedCombatSubsystem* HoverTurnBased = GetActiveTurnBased())
	{
		FHitResult Hit;
		if (GetHitResultUnderCursor(ECC_Visibility, false, Hit))
		{
			HoverTurnBased->SetHoveredPoint(Hit.ImpactPoint, Hit.GetActor());
		}
	}
	// Turn-based relocation: the object's hologram follows the hovered cell.
	if (UTurnBasedCombatSubsystem* GhostTurnBased = GetActiveTurnBased(); GhostTurnBased && GhostTurnBased->IsRelocating())
	{
		FHitResult Hit;
		if (GetHitResultUnderCursor(ECC_Visibility, false, Hit))
		{
			GhostTurnBased->SetRelocationHover(Hit.ImpactPoint);
		}
	}

	// Item hand-over: the ring follows the cursor / the hovered squad mate.
	if (USquadTransferSubsystem* Transfer = GetWorld()->GetSubsystem<USquadTransferSubsystem>(); Transfer && Transfer->IsTransferring())
	{
		FHitResult Hit;
		if (GetHitResultUnderCursor(ECC_Visibility, false, Hit))
		{
			Transfer->UpdatePreview(Hit.ImpactPoint, Hit.GetActor());
		}
	}

	// Grenade aim: rings and arc follow the cursor.
	if (UGrenadeSubsystem* Grenades = GetWorld()->GetSubsystem<UGrenadeSubsystem>(); Grenades && Grenades->IsAiming())
	{
		FVector Point;
		if (GetGrenadeAimPoint(Point))
		{
			Grenades->UpdateAim(Point);
		}
	}

	// Object placement: the ghost follows the cursor.
	if (URelocationSubsystem* Relocation = GetPlacingRelocation())
	{
		FVector Point;
		if (GetPlacementPoint(Point))
		{
			Relocation->UpdatePreview(Point);
		}
	}
}

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
		Messages->PostMessage(LOCTEXT("GrenadeSpeaker", "Граната"), LOCTEXT("GrenadeCancelled", "Бросок отменён."));
	}
	return true;
}

void ACodexTacticsPlayerController::GuardKey()
{
	if (const UTurnBasedCombatSubsystem* TurnBased = GetActiveTurnBased(); TurnBased && TurnBased->IsBusy())
	{
		return;
	}
	if (USquadSubsystem* Squad = GetSquad(); Squad && Squad->GetLeader())
	{
		Squad->ToggleGuard(Squad->GetLeader());
	}
}

void ACodexTacticsPlayerController::QuickSaveKey()
{
	if (USaveGameSubsystem* Saves = GetWorld()->GetSubsystem<USaveGameSubsystem>())
	{
		Saves->QuickSave();
	}
}

void ACodexTacticsPlayerController::UseSquadItem(EPersonalItem Item)
{
	USquadSubsystem* Squad = GetSquad();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>();
	if (!Leader || !Messages)
	{
		return;
	}
	if (Leader->UsePersonalItem(Item))
	{
		Messages->PostMessage(Leader->DisplayName, FText::Format(LOCTEXT("ItemUsed", "Использован(а) {0} (+HP / согрев)! (Осталось: {1} шт.)"),
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
		Messages->PostMessage(Leader->DisplayName, FText::Format(LOCTEXT("ItemMissing", "У {0} нет {1} в личном инвентаре!"),
			Leader->DisplayName, PersonalItemRules::GetMissingName(Item)));
	}
	else
	{
		Messages->PostMessage(Leader->DisplayName, LOCTEXT("ItemFull", "Здоровье и тепло бойца уже 100%!"));
	}
}

void ACodexTacticsPlayerController::StartPlacementForType(EDeployableType Type)
{
	USquadSubsystem* Squad = GetSquad();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	URelocationSubsystem* Relocation = GetWorld()->GetSubsystem<URelocationSubsystem>();
	UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>();
	if (!Leader || !Relocation || !Messages)
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
			Messages->PostMessage(Leader->DisplayName, FText::Format(LOCTEXT("NoneInSquad", "⚠️ У отряда нет в наличии: {0}!"), Name));
			return;
		}
		Carrier->AddDeployable(Type, -1);
		Leader->AddDeployable(Type, 1);
		Messages->PostMessage(Leader->DisplayName, FText::Format(LOCTEXT("HandsOver", "🛠️ {0} передает {1} бойцу {2} для установки!"),
			Carrier->DisplayName, Name, Leader->DisplayName));
	}
	Relocation->StartDeployPlacement(Type, Leader);
}

void ACodexTacticsPlayerController::GrenadeKey()
{
	USquadSubsystem* Squad = GetSquad();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	UGrenadeSubsystem* Grenades = GetWorld()->GetSubsystem<UGrenadeSubsystem>();
	UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>();
	if (!Leader || !Grenades || !Messages)
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
			Messages->PostMessage(Leader->DisplayName, FText::Format(LOCTEXT("WeaponBack", "🔫 Оружие: {0} [{1}] (Урон: {2})"),
				Leader->CurrentWeapon ? Leader->CurrentWeapon->WeaponName : FText::GetEmpty(), FText::FromString(Ammo),
				Leader->CurrentWeapon ? FMath::FloorToInt(Leader->CurrentWeapon->BaseDamage) : 0));
		}
		return;
	}
	if (Leader->GrenadesCount <= 0)
	{
		Messages->PostMessage(Leader->DisplayName, LOCTEXT("NoGrenade", "🧨 У бойца нет гранат!"));
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
	Messages->PostMessage(Leader->DisplayName, FText::Format(LOCTEXT("GrenadeTaken", "🧨 Выбрана граната [{0} шт.] (Урон: {1}, Радиус: {2}m)"),
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
	USquadSubsystem* Squad = GetSquad();
	URelocationSubsystem* Relocation = GetWorld()->GetSubsystem<URelocationSubsystem>();
	// Turn-based combat: the active operative sets it up (Godot current_leader follows the selected unit).
	const UTurnBasedCombatSubsystem* TurnBased = GetActiveTurnBased();
	AOperativeCharacter* Worker = TurnBased && TurnBased->GetActiveUnit() ? TurnBased->GetActiveUnit() : (Squad ? Squad->GetLeader() : nullptr);
	if (Relocation && Worker)
	{
		Relocation->HandleDeployKey(Worker);
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
	else if (AOperativeCharacter* Leader = GetSquad() ? GetSquad()->GetLeader() : nullptr)
	{
		// Godot main.gd KEY_R outside placement / the grid fight: reload.
		Leader->StartReload();
		if (Leader->bIsReloading)
		{
			if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
			{
				Messages->PostMessage(Leader->DisplayName, FText::Format(LOCTEXT("Reloading", "🔄 Перезаряжаю {0}..."),
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
	if (!Leader || Leader->AvailableWeapons.IsEmpty() || GetActiveTurnBased() || IsDialogueOpen())
	{
		return;
	}
	const int32 Current = Leader->AvailableWeapons.IndexOfByKey(Leader->CurrentWeapon);
	const UWeaponDataAsset* Next = Leader->AvailableWeapons[(Current + 1) % Leader->AvailableWeapons.Num()];
	if (!Next || !Leader->SwitchToWeaponById(Next->WeaponId))
	{
		return;
	}
	UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>();
	UGrenadeSubsystem* Grenades = GetWorld()->GetSubsystem<UGrenadeSubsystem>();
	if (Messages)
	{
		const FString Ammo = Leader->UsesAmmo() ? FString::Printf(TEXT("%d / %d"), Leader->CurrentClip, Leader->ReserveAmmo) : TEXT("∞");
		Messages->PostMessage(Leader->DisplayName, FText::Format(LOCTEXT("WeaponCycle", "🔫 Оружие: {0} [{1}] (Урон: {2})"),
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
			Messages->PostMessage(Leader->DisplayName, LOCTEXT("GrenadesOut", "🧨 Гранаты закончились!"));
		}
	}
	else if (Grenades && Grenades->IsAiming())
	{
		Grenades->CancelAim(false);
	}
}

void ACodexTacticsPlayerController::SpacePressed()
{
	// Godot: any key skips the pre-combat cutscene.
	if (UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>(); Flow && Flow->GetPhase() == ECodexGamePhase::Cutscene)
	{
		Flow->FinishCutscene();
		return;
	}
	if (IsDialogueOpen())
	{
		DialogueNext(); // Godot: Space advances the dialogue instead of pausing
		return;
	}
	SpaceInput.Press();
}

void ACodexTacticsPlayerController::SpaceReleased()
{
	SetSquadCeaseFire(false);
	if (HoldSphere)
	{
		HoldSphere->HideSphere();
	}
	if (SpaceInput.Release() == ESpaceInputAction::Tap)
	{
		HandleSpaceTap();
	}
}

void ACodexTacticsPlayerController::HandleSpaceTap()
{
	UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	if (!Flow || Flow->GetCombatMode() == ECodexCombatMode::TurnBased)
	{
		// Turn-based: a tap toggles the squad command bar (UI step).
		return;
	}
	const bool bWasPaused = Flow->GetCombatMode() == ECodexCombatMode::TacticalPause;
	const EGameFlowResult Result = Flow->ToggleTacticalPause();
	switch (Result)
	{
	case EGameFlowResult::Ok:
		if (bWasPaused)
		{
			if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
			{
				Messages->PostMessage(LOCTEXT("SquadSpeaker", "ОТРЯД"), LOCTEXT("OrdersAccepted", "▶️ Приказы приняты! Приступаем к выполнению задач!"));
			}
		}
		else
		{
			PostHeadquarters(FText::Format(LOCTEXT("PausePlanning",
				"⏱️ ПЛАНИРОВАНИЕ (Зарядов: {0}/{1}): Отдайте приказы бойцам. Они начнут выполнение после снятия паузы [ПРОБЕЛ]!"),
				Flow->GetPauseCharges(), Flow->GetConfig().TacticalPauseMaxCharges));
		}
		break;
	case EGameFlowResult::PauseOnCooldown:
		PostHeadquarters(FText::Format(LOCTEXT("PauseCooldown", "⏳ Тактическая пауза перезаряжается! Осталось: {0}с"),
			FText::AsNumber(Flow->GetPauseCooldownRemaining(), &FNumberFormattingOptions().SetMinimumFractionalDigits(1).SetMaximumFractionalDigits(1))));
		break;
	case EGameFlowResult::NoPauseCharges:
		PostHeadquarters(LOCTEXT("PauseNoCharges", "⏳ Все 3 заряда тактической паузы исчерпаны в этой волне! Кулдаун: 20с."));
		break;
	default:
		// Outside a wave the tap opens the command bar (UI step).
		break;
	}
}

void ACodexTacticsPlayerController::HandleSpaceHold()
{
	UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	USquadSubsystem* Squad = GetSquad();
	if (!Flow || !Squad || !Squad->GetLeader())
	{
		return;
	}
	if (Flow->GetCombatMode() == ECodexCombatMode::TurnBased)
	{
		if (Flow->ExitTurnBased() == EGameFlowResult::Ok)
		{
			PostHeadquarters(LOCTEXT("TurnBasedExit",
				"🛡️ Пошаговый бой завершен. Включена тактическая пауза (20с) для подготовки отряда [ПРОБЕЛ]."));
		}
		return;
	}

	// User decision 2026-09-30: only on flat ground — no fight started up on a platform or down in a pit.
	if (Flow->GetCombatMode() != ECodexCombatMode::TurnBased)
	{
		TArray<float> Feet;
		for (const AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (Member->HealthComponent && Member->HealthComponent->IsAlive())
			{
				Feet.Add(Member->GetActorLocation().Z - Member->GetSimpleCollisionHalfHeight());
			}
		}
		float Ground = 0.f;
		const TArray<float> Samples = CombatQueries::SampleGroundHeights(GetWorld(), Squad->GetLeader()->GetActorLocation(), 1050.f);
		if (!CombatQueries::IsSquadOnFlatGround(Samples, Feet, 50.f, Ground))
		{
			PostHeadquarters(LOCTEXT("TurnBasedNotFlat",
				"⚠️ Пошаговый бой можно начать только на ровной поверхности — не на возвышенности и не в низине!"));
			return;
		}
	}
	const bool bEnemiesNear = CombatQueries::HasEnemiesWithin(GetWorld(), Squad->GetLeader()->GetActorLocation(),
		Flow->GetConfig().TurnBasedEncounterRadius);
	const EGameFlowResult Result = Flow->RequestEnterTurnBased(bEnemiesNear);
	if (Result == EGameFlowResult::NoEnemiesInRange)
	{
		PostHeadquarters(LOCTEXT("TurnBasedNoEnemies", "⚠️ В радиусе 15м нет живых врагов для пошагового боя!"));
	}
	else if (Result == EGameFlowResult::TurnBasedLimitReached)
	{
		PostHeadquarters(FText::Format(LOCTEXT("TurnBasedLimit", "⚠️ Лимит пошагового боя на эту волну исчерпан ({0}/{0})!"),
			Flow->GetConfig().TurnBasedUsesPerWave));
	}
}

void ACodexTacticsPlayerController::PostHeadquarters(const FText& Text) const
{
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(LOCTEXT("HQSpeaker", "ШТАБ"), Text);
	}
}

bool ACodexTacticsPlayerController::IsDialogueOpen() const
{
	const UDialogueSubsystem* Dialogue = GetWorld()->GetSubsystem<UDialogueSubsystem>();
	return Dialogue && Dialogue->IsDialogueOpen();
}

void ACodexTacticsPlayerController::DialogueNext()
{
	if (UDialogueSubsystem* Dialogue = GetWorld()->GetSubsystem<UDialogueSubsystem>())
	{
		Dialogue->AdvanceLine();
	}
}

void ACodexTacticsPlayerController::EnterPressed()
{
	if (IsDialogueOpen())
	{
		DialogueNext();
	}
	else if (UTurnBasedCombatSubsystem* TurnBased = GetActiveTurnBased())
	{
		TurnBased->PassSquadTurn();
	}
}

void ACodexTacticsPlayerController::ProfilePressed()
{
	if (ACodexTacticsHUD* Hud = GetHUD<ACodexTacticsHUD>())
	{
		Hud->ToggleProfileDialog();
	}
}

void ACodexTacticsPlayerController::TabPressed()
{
	if (UTurnBasedCombatSubsystem* TurnBased = GetActiveTurnBased())
	{
		TurnBased->EndCurrentUnitTurn();
	}
}

UTurnBasedCombatSubsystem* ACodexTacticsPlayerController::GetActiveTurnBased() const
{
	UTurnBasedCombatSubsystem* TurnBased = GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>();
	return TurnBased && TurnBased->IsActive() ? TurnBased : nullptr;
}

void ACodexTacticsPlayerController::DialogueSkip()
{
	ACodexTacticsHUD* Hud = Cast<ACodexTacticsHUD>(GetHUD());
	// Pause menu windows first (they are open while the world is paused).
	if (Hud && ((Hud->GetPauseMenu() && Hud->GetPauseMenu()->IsOpen()) || (Hud->GetSaveLoadDialog() && Hud->GetSaveLoadDialog()->IsOpen())))
	{
		Hud->HandleEscape();
		return;
	}
	if (USquadTransferSubsystem* Transfer = GetWorld()->GetSubsystem<USquadTransferSubsystem>(); Transfer && Transfer->IsTransferring())
	{
		Transfer->CancelTransferMode();
		return;
	}
	if (CancelGrenadeAim())
	{
		return;
	}
	// Godot: Esc cancels the turn-based object relocation first, then the attack mode.
	if (UTurnBasedCombatSubsystem* TurnBased = GetActiveTurnBased(); TurnBased && TurnBased->IsRelocating())
	{
		TurnBased->CancelRelocate();
		return;
	}
	if (UTurnBasedCombatSubsystem* TurnBased = GetActiveTurnBased(); TurnBased && TurnBased->IsAttackMode())
	{
		TurnBased->ExitAttackMode(TEXT("🟢 Прицеливание отменено (возврат в режим перемещения)."));
		return;
	}
	if (UDialogueSubsystem* Dialogue = GetWorld()->GetSubsystem<UDialogueSubsystem>(); Dialogue && Dialogue->IsDialogueOpen())
	{
		Dialogue->SkipDialogue();
		return;
	}
	// Godot: other windows close, otherwise the pause menu opens.
	if (Hud)
	{
		Hud->HandleEscape();
	}
}

void ACodexTacticsPlayerController::OnClick()
{
	if (IsDialogueOpen())
	{
		return; // the dialogue panel handles its own clicks
	}
	// Item hand-over: LMB on a squad mate (Godot _handle_transfer_click).
	if (USquadTransferSubsystem* Transfer = GetWorld()->GetSubsystem<USquadTransferSubsystem>(); Transfer && Transfer->IsTransferring())
	{
		FHitResult Hit;
		if (GetHitResultUnderCursor(ECC_Visibility, false, Hit))
		{
			Transfer->HandleClick(Hit.ImpactPoint, Hit.GetActor());
		}
		return;
	}
	// Grenade aim: LMB throws (Godot _handle_grenade_throw_click).
	if (UGrenadeSubsystem* Grenades = GetWorld()->GetSubsystem<UGrenadeSubsystem>(); Grenades && Grenades->IsAiming())
	{
		FVector Point;
		if (GetGrenadeAimPoint(Point))
		{
			Grenades->ThrowAtCursor(Point);
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
			if (GetPlacementPoint(Point) && TurnBased->HandleDeployPlacement(Relocation->GetPlacingType(), Point, Relocation->GetPlacingYaw()))
			{
				Relocation->CancelPlacement();
			}
			return;
		}
	}
	// Placement mode: LMB sets the new spot of the object being moved.
	if (URelocationSubsystem* Relocation = GetPlacingRelocation())
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
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(LOCTEXT("SquadSender", "ОТРЯД"), Selected.Num() == 1
			? FText::Format(LOCTEXT("UnitSelected", "👤 Выбран боец: {0}"), Selected[0]->DisplayName)
			: FText::Format(LOCTEXT("GroupSelected", "👥 Выбрана группа: {0} бойцов"), Selected.Num()));
	}
	return Selected.Num();
}

void ACodexTacticsPlayerController::OrderGroupMove(const FVector& Destination, bool bSprint, bool bPlan)
{
	USquadSubsystem* Squad = GetSquad();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (!Leader)
	{
		return;
	}
	// The leader first, then the others in selection order (Godot sorted_group).
	TArray<AOperativeCharacter*> Group = Squad->HasMultiSelection() ? Squad->GetSelectedGroup() : TArray<AOperativeCharacter*>{ Leader };
	Group.Remove(Leader);
	Group.Insert(Leader, 0);
	FVector Average = FVector::ZeroVector;
	for (const AOperativeCharacter* Member : Group)
	{
		Average += Member->GetActorLocation();
	}
	Average /= Group.Num();
	const FVector CameraForward = PlayerCameraManager ? PlayerCameraManager->GetCameraRotation().Vector() : FVector::ForwardVector;
	const TArray<FVector> Targets = SquadFormation::ComputeGroupTargets(Destination, Average, CameraForward, Group.Num());

	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>();
	for (int32 Index = 0; Index < Group.Num(); ++Index)
	{
		if (bPlan)
		{
			const FVector Planned = Squad->PlanMove(Group[Index], Targets[Index], bSprint, Flow ? Flow->GetConfig().PauseOrderRadius : 1200.f);
			if (UCombatFeedbackSubsystem* Feedback = GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>())
			{
				Feedback->SpawnWaypointMarker(Planned);
			}
		}
		else
		{
			Group[Index]->OrderMoveTo(Targets[Index], bSprint);
			// Sprint 06-B: where each operative was sent (a fading ping; the new order replaces the old pings).
			if (UCombatFeedbackSubsystem* Feedback = GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>())
			{
				Feedback->SpawnMovePing(Targets[Index], Index == 0);
			}
		}
	}
	if (Messages && Group.Num() > 1)
	{
		const FText Line = bPlan
			? FText::Format(bSprint ? LOCTEXT("GroupPlanSprint", "🏃 [ПЛАН] Запланирован групповой рывок ({0} бойцов)!")
				: LOCTEXT("GroupPlanMove", "📋 [ПЛАН] Запланировано групповое перемещение ({0} бойцов)!"), Group.Num())
			: FText::Format(LOCTEXT("GroupMove", "🏃 Группа ({0} бойцов) выдвигается на позиции!"), Group.Num());
		Messages->PostMessage(LOCTEXT("SquadSender", "ОТРЯД"), Line);
	}
}

void ACodexTacticsPlayerController::HandleWorldHit(const FHitResult& Hit)
{
	USquadSubsystem* Squad = GetSquad();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (!Leader)
	{
		return;
	}

	// Turn-based combat: the grid handles every click (select, attack, walk).
	if (UTurnBasedCombatSubsystem* TurnBased = GetActiveTurnBased())
	{
		TurnBased->HandleWorldClick(Hit.ImpactPoint, Hit.GetActor(), IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift));
		return;
	}

	// Action bar «ПЕР»: the clicked object is picked up for relocation.
	if (bRelocateSelectMode)
	{
		if (AInteractableActor* Object = Cast<AInteractableActor>(Hit.GetActor()))
		{
			bRelocateSelectMode = false;
			if (URelocationSubsystem* Relocation = GetWorld()->GetSubsystem<URelocationSubsystem>())
			{
				Relocation->StartRelocate(Object, Leader);
			}
		}
		return;
	}

	// Ctrl + click: targeted fire only, never a move or a selection.
	if (IsInputKeyDown(EKeys::LeftControl) || IsInputKeyDown(EKeys::RightControl))
	{
		const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
		if (!Flow || Flow->GetCombatMode() != ECodexCombatMode::TurnBased)
		{
			IssueTargetedShot(Hit.GetActor());
		}
		return;
	}

	// Godot main.gd plain-click rules during a fight (before the usual walk-up):
	const UGameFlowSubsystem* ClickFlow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	const bool bWave = ClickFlow && ClickFlow->GetPhase() == ECodexGamePhase::WaveCombat;
	const bool bClickPause = ClickFlow && ClickFlow->GetCombatMode() == ECodexCombatMode::TacticalPause;
	const bool bPreparation = ClickFlow && ClickFlow->GetPhase() == ECodexGamePhase::Preparation;
	UGameMessageSubsystem* ClickMessages = GetWorld()->GetSubsystem<UGameMessageSubsystem>();
	// 0.5: a live enemy becomes the priority target (no Ctrl needed while a wave is on).
	if (AActor* HitActor = Hit.GetActor(); bWave && HitActor && HitActor->ActorHasTag(FName(TEXT("Enemy"))))
	{
		const UHealthComponent* EnemyHealth = HitActor->FindComponentByClass<UHealthComponent>();
		if (!EnemyHealth || EnemyHealth->IsAlive())
		{
			Leader->AssignPriorityTarget(HitActor);
			if (ClickMessages)
			{
				const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(HitActor);
				ClickMessages->PostMessage(Leader->DisplayName, FText::FromString(FString::Printf(TEXT("🎯 Назначена приоритетная цель: %s!"),
					Enemy ? *Enemy->GetEnemyDisplayName() : TEXT("Враг"))));
			}
			return;
		}
	}
	// 0: set-up items and movable objects can't be moved mid-fight; in the pause / preparation a set-up item opens its
	// menu at once and a movable object is picked up for relocation right away.
	if (AInteractableActor* Object = Cast<AInteractableActor>(Hit.GetActor()))
	{
		const bool bDeployable = Object->IsA<ADeployableActor>();
		// Godot is_zone_solo: the leader alone in a camera zone may move / take objects mid-wave.
		const bool bZoneSolo = Leader && Leader->bInCameraZone;
		if ((bDeployable || Object->bCanBeRelocated) && bWave && !bClickPause && !bZoneSolo)
		{
			if (ClickMessages)
			{
				ClickMessages->PostMessage(LOCTEXT("HQ", "ШТАБ"), LOCTEXT("NoMoveInFight", "⚠️ Во время боя менять расположение объектов нельзя! Используйте тактическую паузу [ПРОБЕЛ]."));
			}
			if (UInteractionSubsystem* ClickInteractions = GetWorld()->GetSubsystem<UInteractionSubsystem>())
			{
				ClickInteractions->CancelInteraction();
			}
			return;
		}
		if (bDeployable && (bPreparation || bClickPause))
		{
			if (UInteractionSubsystem* ClickInteractions = GetWorld()->GetSubsystem<UInteractionSubsystem>())
			{
				ClickInteractions->OpenMenuNow(Object);
			}
			return;
		}
		if (!bDeployable && Object->bCanBeRelocated && (bClickPause || (bWave && bZoneSolo)))
		{
			if (URelocationSubsystem* Relocation = GetWorld()->GetSubsystem<URelocationSubsystem>())
			{
				Relocation->StartRelocate(Object, Leader);
			}
			return;
		}
	}

	// Godot "is_unrecruited": a click on the recruit rescues / talks to him (or walks the leader up to him).
	if (URecruitSubsystem* Recruits = GetWorld()->GetSubsystem<URecruitSubsystem>(); Recruits && Recruits->IsRecruit(Hit.GetActor()))
	{
		if (UInteractionSubsystem* Interactions = GetWorld()->GetSubsystem<UInteractionSubsystem>())
		{
			Interactions->CancelInteraction();
		}
		Recruits->HandleRecruitClicked(false);
		return;
	}

	// 1. Direct hit check or owner check on operative
	AOperativeCharacter* SelectedMember = nullptr;
	if (AOperativeCharacter* HitOperative = Cast<AOperativeCharacter>(Hit.GetActor()))
	{
		if (Squad->GetMembers().Contains(HitOperative))
		{
			SelectedMember = HitOperative;
		}
	}
	else if (Hit.GetActor() && Hit.GetActor()->GetOwner())
	{
		if (AOperativeCharacter* OwnerOperative = Cast<AOperativeCharacter>(Hit.GetActor()->GetOwner()))
		{
			if (Squad->GetMembers().Contains(OwnerOperative))
			{
				SelectedMember = OwnerOperative;
			}
		}
	}

	// 2. Proximity check around cursor impact point (not with a group selected: a click next to one of them is the
	// group's move order — only a click on an operative's body picks him).
	if (!SelectedMember && !Squad->HasMultiSelection())
	{
		float ClosestDist = SelectRadius;
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			const float Dist = FVector::Dist2D(Member->GetActorLocation(), Hit.ImpactPoint);
			if (Dist <= ClosestDist)
			{
				ClosestDist = Dist;
				SelectedMember = Member;
			}
		}
	}

	UInteractionSubsystem* Interactions = GetWorld()->GetSubsystem<UInteractionSubsystem>();
	if (SelectedMember)
	{
		if (Interactions)
		{
			Interactions->CancelInteraction();
		}
		Squad->SetLeader(SelectedMember);
		if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
		{
			Messages->PostMessage(LOCTEXT("SquadSpeaker", "ОТРЯД"),
				FText::Format(LOCTEXT("UnitSelected", "👤 Выбран боец: {0}"), SelectedMember->DisplayName));
		}
		LastClickTime = -1.0;
		return;
	}

	FVector2D MousePosition;
	GetMousePosition(MousePosition.X, MousePosition.Y);
	const double Now = FApp::GetCurrentTime(); // real time, but follows a fixed-step (bot) run
	const bool bDoubleClick = LastClickTime >= 0.0
		&& Now - LastClickTime <= DoubleClickSeconds
		&& FVector2D::Distance(MousePosition, LastClickPosition) <= DoubleClickPixels;
	LastClickTime = bDoubleClick ? -1.0 : Now;
	LastClickPosition = MousePosition;

	// Clicking an object sends the leader to it (running on a double click); its action menu opens on arrival.
	if (AInteractableActor* Interactable = Cast<AInteractableActor>(Hit.GetActor()))
	{
		if (Interactions)
		{
			Interactions->RequestInteraction(Interactable, bDoubleClick);
		}
		return;
	}
	if (Interactions)
	{
		Interactions->CancelInteraction();
	}

	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	const ECodexCombatMode Mode = Flow ? Flow->GetCombatMode() : ECodexCombatMode::None;
	if (Mode == ECodexCombatMode::TurnBased)
	{
		// Grid combat handles clicks itself (turn-based step).
		return;
	}
	// Godot Shift + click on the ground: turn the leader and fix the observation sector.
	if (IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift))
	{
		Leader->SetFacingPoint(Hit.ImpactPoint);
		UFloatingTextSubsystem::SpawnAboveOperative(Leader, TEXT("👁️ СЕКТОР ОБЗОРА"), FLinearColor(0.2f, 0.9f, 1.f));
		if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
		{
			Messages->PostMessage(Leader->DisplayName, FText::Format(LOCTEXT("Sector", "👁️ [{0}]: Сектор наблюдения зафиксирован!"), Leader->DisplayName));
		}
		return;
	}
	// Godot: during an active wave the squad moves only through the tactical pause.
	if (Flow && Flow->GetPhase() == ECodexGamePhase::WaveCombat && Mode == ECodexCombatMode::RealTime)
	{
		if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
		{
			Messages->PostMessage(LOCTEXT("HQ", "ШТАБ"), LOCTEXT("MoveOnlyInPause", "Перемещение во время боя возможно только в режиме тактической паузы [ПРОБЕЛ]!"));
		}
		return;
	}
	// Godot double click: sprint, or why not.
	if (bDoubleClick)
	{
		if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
		{
			if (!Leader->CanSprint() && Leader->ColdLevel >= Leader->MovementConfig.MaxColdToSprint)
			{
				Messages->PostMessage(Leader->DisplayName, FText::FromString(FString::Printf(TEXT("🥶 %s замерз(ла) (%d%% холода) и не может бежать! Иду шагом."),
					*Leader->DisplayName.ToString(), FMath::FloorToInt(Leader->ColdLevel))));
			}
			else if (!Leader->CanSprint() && Leader->IsWounded() && Leader->HealthComponent)
			{
				Messages->PostMessage(Leader->DisplayName, FText::FromString(FString::Printf(TEXT("🩹 %s тяжело ранен(а) (%d/%d HP) и не может бежать! Иду шагом."),
					*Leader->DisplayName.ToString(), FMath::FloorToInt(Leader->HealthComponent->GetCurrentHealth()), FMath::FloorToInt(Leader->HealthComponent->GetMaxHealth()))));
			}
			else if (Leader->CanSprint())
			{
				Messages->PostMessage(Leader->DisplayName, LOCTEXT("SprintOrder", "🏃 Бегом к позиции!"));
			}
		}
	}
	OrderGroupMove(Hit.ImpactPoint, bDoubleClick, Mode == ECodexCombatMode::TacticalPause);
}

void ACodexTacticsPlayerController::SetEntireSquadStance(EOperativeStance Stance)
{
	USquadSubsystem* Squad = GetSquad();
	UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>();
	if (!Squad)
	{
		return;
	}
	// Godot _set_entire_squad_stance: nobody lies down while the squad moves.
	if (Stance == EOperativeStance::Prone)
	{
		for (const AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (Member->IsMoving() || Member->GetVelocity().SizeSquared2D() > 100.f)
			{
				if (Messages)
				{
					Messages->PostMessage(LOCTEXT("HQ", "ШТАБ"), LOCTEXT("SquadProneMoving", "⚠️ Нельзя перевести отряд в положение лёжа во время движения! Сначала полностью остановитесь."));
				}
				return;
			}
		}
	}
	const TCHAR* Name = Stance == EOperativeStance::Prone ? TEXT("ЛЁЖА") : (Stance == EOperativeStance::Crouching ? TEXT("ПРИСЕВ") : TEXT("СТОЯ"));
	for (AOperativeCharacter* Member : Squad->GetMembers())
	{
		Member->SetStance(Stance);
		UFloatingTextSubsystem::SpawnAboveOperative(Member, FString::Printf(TEXT("👥 ОТРЯД: %s"), Name), FLinearColor(0.3f, 0.95f, 1.f));
	}
	if (Messages)
	{
		Messages->PostMessage(LOCTEXT("SquadSpeaker", "ОТРЯД"), FText::FromString(FString::Printf(TEXT("📢 [ПРИКАЗ ОТРЯДУ]: Все бойцы переходят в положение %s!"), Name)));
	}
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
			if (Hud && Hud->GetProfileDialog() && Hud->GetProfileDialog()->IsOpen())
			{
				Hud->OpenProfileDialog(Squad->GetLeader());
			}
			if (AOperativeCharacter* NewLeader = Squad->GetLeader())
			{
				if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
				{
					Messages->PostMessage(LOCTEXT("SquadSpeaker", "ОТРЯД"),
						FText::Format(LOCTEXT("UnitSelected", "👤 Выбран боец: {0}"), NewLeader->DisplayName));
				}
			}
		}
	}
}

void ACodexTacticsPlayerController::ApplyStance(EOperativeStance Stance)
{
	if (UTurnBasedCombatSubsystem* TurnBased = GetActiveTurnBased())
	{
		TurnBased->SetActiveUnitStance(Stance); // 1 AP, active operative only
		return;
	}
	USquadSubsystem* Squad = GetSquad();
	if (!Squad)
	{
		return;
	}
	if (IsInputKeyDown(EKeys::LeftAlt) || IsInputKeyDown(EKeys::RightAlt))
	{
		SetEntireSquadStance(Stance); // Godot Alt + Z / C / V
		return;
	}
	AOperativeCharacter* Leader = Squad->GetLeader();
	if (!Leader)
	{
		return;
	}

	// Godot check: cannot go prone while moving!
	if (Stance == EOperativeStance::Prone && (Leader->IsMoving() || Leader->GetVelocity().SizeSquared2D() > 10.f))
	{
		if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
		{
			Messages->PostMessage(Leader->DisplayName,
				LOCTEXT("CannotProneWhileMoving", "⚠️ Нельзя лечь во время движения! Сначала полностью остановитесь."));
		}
		return;
	}

	// Deviation (user decision 2026-10-01; Godot _sync_squad_stances made the others follow): only the selected
	// operative changes his stance — the whole squad only with Alt + Z / C / V (SetEntireSquadStance).
	Leader->SetStance(Stance);

	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		FText StanceName;
		switch (Stance)
		{
		case EOperativeStance::Standing: StanceName = LOCTEXT("StanceStanding", "СТОЯ"); break;
		case EOperativeStance::Crouching: StanceName = LOCTEXT("StanceCrouching", "ПРИСЕВ"); break;
		case EOperativeStance::Prone: StanceName = LOCTEXT("StanceProne", "ЛЁЖА"); break;
		default: break;
		}

		Messages->PostMessage(Leader->DisplayName,
			FText::Format(LOCTEXT("SoloStanceFmt", "Стойка бойца {0}: {1}"), Leader->DisplayName, StanceName));
	}
}

void ACodexTacticsPlayerController::ToggleSoloMode()
{
	if (USquadSubsystem* Squad = GetSquad())
	{
		Squad->ToggleSoloMode();
	}
}

ATacticalCameraPawn* ACodexTacticsPlayerController::GetCameraPawn() const
{
	return GetPawn<ATacticalCameraPawn>();
}

void ACodexTacticsPlayerController::OnMouseWheelUp()
{
	if (UTurnBasedCombatSubsystem* TurnBased = GetActiveTurnBased(); TurnBased && TurnBased->IsRelocating())
	{
		TurnBased->RotateRelocation(+1); // Godot: the wheel rotates the barricade being relocated
		return;
	}
	if (URelocationSubsystem* Relocation = GetPlacingRelocation())
	{
		Relocation->RotatePreview(+1); // Godot: the wheel rotates the object being placed
		return;
	}
	if (ATacticalCameraPawn* CameraPawn = GetCameraPawn())
	{
		CameraPawn->AddZoomNotches(-1.f);
	}
}

void ACodexTacticsPlayerController::OnMouseWheelDown()
{
	if (UTurnBasedCombatSubsystem* TurnBased = GetActiveTurnBased(); TurnBased && TurnBased->IsRelocating())
	{
		TurnBased->RotateRelocation(-1);
		return;
	}
	if (URelocationSubsystem* Relocation = GetPlacingRelocation())
	{
		Relocation->RotatePreview(-1);
		return;
	}
	if (ATacticalCameraPawn* CameraPawn = GetCameraPawn())
	{
		CameraPawn->AddZoomNotches(+1.f);
	}
}

void ACodexTacticsPlayerController::CameraRotateLeft()
{
	// Godot: Q turns the object being relocated / placed by -45° instead of the camera.
	if (UTurnBasedCombatSubsystem* TurnBased = GetActiveTurnBased(); TurnBased && TurnBased->IsRelocating())
	{
		TurnBased->RotateRelocation(-1);
		return;
	}
	if (URelocationSubsystem* Relocation = GetPlacingRelocation(); Relocation && Relocation->IsPlacingDeployable() && GetActiveTurnBased())
	{
		Relocation->RotatePreview(-1);
		return;
	}
	if (ATacticalCameraPawn* CameraPawn = GetCameraPawn())
	{
		CameraPawn->RotateStep(+1);
	}
}

void ACodexTacticsPlayerController::CameraRotateRight()
{
	// Godot: E (and R) turn the object being relocated / placed by +45°.
	if (UTurnBasedCombatSubsystem* TurnBased = GetActiveTurnBased(); TurnBased && TurnBased->IsRelocating())
	{
		TurnBased->RotateRelocation(+1);
		return;
	}
	if (URelocationSubsystem* Relocation = GetPlacingRelocation(); Relocation && Relocation->IsPlacingDeployable() && GetActiveTurnBased())
	{
		Relocation->RotatePreview(+1);
		return;
	}
	if (ATacticalCameraPawn* CameraPawn = GetCameraPawn())
	{
		CameraPawn->RotateStep(-1);
	}
}

void ACodexTacticsPlayerController::CameraDragRotateStart()
{
	// Godot: RMB drops a selection box being dragged.
	bLmbDown = false;
	bBoxSelecting = false;
	// Godot: RMB cancels the hand-over, the grenade aim, object placement (and the turn-based relocation).
	if (USquadTransferSubsystem* Transfer = GetWorld()->GetSubsystem<USquadTransferSubsystem>(); Transfer && Transfer->IsTransferring())
	{
		Transfer->CancelTransferMode();
		return;
	}
	if (CancelGrenadeAim())
	{
		return;
	}
	if (URelocationSubsystem* Relocation = GetPlacingRelocation())
	{
		Relocation->CancelPlacement();
		return;
	}
	// Sprint 06-E: RMB aborts a running relocation / deploy walk of the leader or the selected group.
	if (URelocationSubsystem* Relocation = GetWorld()->GetSubsystem<URelocationSubsystem>())
	{
		bool bCancelled = false;
		if (USquadSubsystem* Squad = GetSquad())
		{
			TArray<AOperativeCharacter*> Workers = Squad->HasMultiSelection() ? Squad->GetSelectedGroup() : TArray<AOperativeCharacter*>();
			Workers.AddUnique(Squad->GetLeader());
			for (AOperativeCharacter* Worker : Workers)
			{
				bCancelled |= Relocation->CancelActiveTask(Worker);
			}
		}
		if (bCancelled)
		{
			return;
		}
	}
	if (UTurnBasedCombatSubsystem* TurnBased = GetActiveTurnBased(); TurnBased && TurnBased->IsRelocating())
	{
		TurnBased->CancelRelocate();
		return;
	}
	if (UTurnBasedCombatSubsystem* TurnBased = GetActiveTurnBased(); TurnBased && TurnBased->IsAttackMode())
	{
		TurnBased->ExitAttackMode(TEXT("🟢 Прицеливание отменено (возврат в режим перемещения)."));
		return;
	}
	if (ATacticalCameraPawn* CameraPawn = GetCameraPawn())
	{
		CameraPawn->SetDragRotating(true);
	}
}

void ACodexTacticsPlayerController::CameraDragRotateStop()
{
	if (ATacticalCameraPawn* CameraPawn = GetCameraPawn())
	{
		CameraPawn->SetDragRotating(false);
	}
}

void ACodexTacticsPlayerController::CameraDragPanStart()
{
	if (ATacticalCameraPawn* CameraPawn = GetCameraPawn())
	{
		CameraPawn->SetDragPanning(true);
	}
}

void ACodexTacticsPlayerController::CameraDragPanStop()
{
	if (ATacticalCameraPawn* CameraPawn = GetCameraPawn())
	{
		CameraPawn->SetDragPanning(false);
	}
}

USquadSubsystem* ACodexTacticsPlayerController::GetSquad() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
}


void ACodexTacticsPlayerController::IssueTargetedShot(AActor* HitActor)
{
	USquadSubsystem* Squad = GetSquad();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>();
	if (!Leader || !Messages)
	{
		return;
	}
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	const bool bPaused = Flow && Flow->GetCombatMode() == ECodexCombatMode::TacticalPause;
	const ETargetedShotKind Kind = AOperativeCharacter::ClassifyShotTarget(HitActor);
	UCombatFeedbackSubsystem* Feedback = GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>();
	if (Kind != ETargetedShotKind::None && Feedback)
	{
		// Godot _highlight_target_feedback (not on destroyed crates) + a plan marker in the pause.
		const ALootCrateActor* Crate = Cast<ALootCrateActor>(HitActor);
		if (!Crate || !Crate->IsDestroyed())
		{
			Feedback->HighlightTarget(HitActor);
		}
		if (bPaused)
		{
			FVector Origin;
			FVector Extent;
			HitActor->GetActorBounds(true, Origin, Extent);
			Feedback->SpawnWaypointMarker(FVector(Origin.X, Origin.Y, Origin.Z - Extent.Z));
		}
	}
	if (bPaused && Kind != ETargetedShotKind::None)
	{
		Leader->PlanTargetedShot(HitActor);
	}

	switch (Kind)
	{
	case ETargetedShotKind::Enemy:
	{
		const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(HitActor);
		const FText EnemyName = Enemy ? FText::FromString(Enemy->GetEnemyDisplayName()) : LOCTEXT("Enemy", "Враг");
		if (bPaused)
		{
			Messages->PostMessage(Leader->DisplayName, FText::Format(LOCTEXT("PlanEnemy", "📋 [ПЛАН] Назначен прицельный огонь по: {0}!"), EnemyName));
		}
		else
		{
			Leader->SetManualPriorityTarget(HitActor);
			Messages->PostMessage(Leader->DisplayName, FText::Format(LOCTEXT("PriorityEnemy", "🎯 Назначена приоритетная цель: {0}!"), EnemyName));
		}
		break;
	}
	case ETargetedShotKind::Barrel:
		if (bPaused)
		{
			Messages->PostMessage(Leader->DisplayName, LOCTEXT("PlanBarrel", "📋 [ПЛАН] Запланирован выстрел по горючей бочке! [ПРОБЕЛ — огонь]"));
		}
		else if (Leader->ShootAtObject(HitActor))
		{
			Messages->PostMessage(Leader->DisplayName, LOCTEXT("ShotBarrel", "💥 Прицельный выстрел по горючей бочке!"));
		}
		break;
	case ETargetedShotKind::Mine:
		if (bPaused)
		{
			Messages->PostMessage(Leader->DisplayName, LOCTEXT("PlanMine", "📋 [ПЛАН] Запланирован прицельный выстрел по мине! [ПРОБЕЛ — огонь]"));
		}
		else
		{
			Leader->ShootAtObject(HitActor);
		}
		break;
	case ETargetedShotKind::Crate:
		if (bPaused)
		{
			Messages->PostMessage(Leader->DisplayName, LOCTEXT("PlanCrate", "📋 [ПЛАН] Запланирован выстрел по ящику снабжения! [ПРОБЕЛ — огонь]"));
		}
		else
		{
			Leader->ShootAtObject(HitActor);
		}
		break;
	case ETargetedShotKind::TrappedObject:
		if (bPaused)
		{
			Messages->PostMessage(Leader->DisplayName, LOCTEXT("PlanTrapped", "📋 [ПЛАН] Запланирован дистанционный подрыв растяжки! [ПРОБЕЛ — огонь]"));
		}
		else
		{
			Leader->ShootAtObject(HitActor);
		}
		break;
	default:
		Messages->PostMessage(Leader->DisplayName, LOCTEXT("TargetHint", "Укажите врага, бочку, мину или ящик для прицельной стрельбы [Ctrl+Клик]!"));
		break;
	}
}

void ACodexTacticsPlayerController::RestartMission()
{
	if (UMissionSubsystem* Mission = GetWorld()->GetSubsystem<UMissionSubsystem>())
	{
		Mission->RestartMission(/*bQuick*/ true);
	}
}

void ACodexTacticsPlayerController::CycleLeaderStance()
{
	USquadSubsystem* Squad = GetSquad();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (!Leader || IsDialogueOpen())
	{
		return;
	}
	EOperativeStance Next = static_cast<EOperativeStance>((static_cast<int32>(Leader->GetStance()) + 1) % 3);
	if (Next == EOperativeStance::Prone && (Leader->IsMoving() || Leader->GetVelocity().SizeSquared2D() > 10.f))
	{
		Next = EOperativeStance::Standing; // Godot: prone is skipped while moving
	}
	// Godot _cycle_leader_stance, without the squad sync (user decision 2026-10-01): the selected operative only.
	Leader->SetStance(Next);
}

void ACodexTacticsPlayerController::ToggleRelocateSelectMode()
{
	URelocationSubsystem* Relocation = GetWorld()->GetSubsystem<URelocationSubsystem>();
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>();
	if (Relocation && !Relocation->CanRelocateNow())
	{
		PostHeadquarters(LOCTEXT("RelocateCombat", "⚠️ Во время боя менять расположение объектов нельзя! Используйте тактическую паузу [ПРОБЕЛ]."));
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
			Messages->PostMessage(LOCTEXT("Engineering", "Инженерия"), LOCTEXT("RelocateCancelled", "Режим перемещения объектов отменен."));
		}
		return;
	}
	bRelocateSelectMode = true;
	if (Messages)
	{
		Messages->PostMessage(LOCTEXT("Engineering", "Инженерия"),
			LOCTEXT("RelocatePick", "📦 [ПЕРЕНОС] 1️⃣ Кликните на объект в сцене (бочка, баррикада, ящик, турель, мина), который хотите переместить."));
	}
}

#undef LOCTEXT_NAMESPACE

void ACodexTacticsPlayerController::SetSquadCeaseFire(bool bCease)
{
	if (bCeaseFireSet == bCease)
	{
		return;
	}
	bCeaseFireSet = bCease;
	if (USquadSubsystem* Squad = GetSquad())
	{
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->bTacticalCeaseFire = bCease;
		}
	}
}
