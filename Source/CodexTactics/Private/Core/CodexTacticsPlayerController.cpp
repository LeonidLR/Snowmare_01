#include "Core/CodexTacticsPlayerController.h"
#include "Camera/TacticalCameraPawn.h"
#include "CodexTactics.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Combat/GrenadeSubsystem.h"
#include "Data/WeaponDataAsset.h"
#include "Combat/EncounterQueries.h"
#include "Core/MissionSubsystem.h"
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
		MakeAction(TEXT("IA_SelectMember3"), EKeys::Three) };
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
	InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &ACodexTacticsPlayerController::EnterPressed);
	InputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &ACodexTacticsPlayerController::TabPressed);
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ACodexTacticsPlayerController::DialogueSkip);

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(InputComponent);
	if (!Input)
	{
		return;
	}
	Input->BindAction(ClickAction, ETriggerEvent::Started, this, &ACodexTacticsPlayerController::OnClick);
	Input->BindAction(SelectActions[0], ETriggerEvent::Started, this, &ACodexTacticsPlayerController::SelectMember1);
	Input->BindAction(SelectActions[1], ETriggerEvent::Started, this, &ACodexTacticsPlayerController::SelectMember2);
	Input->BindAction(SelectActions[2], ETriggerEvent::Started, this, &ACodexTacticsPlayerController::SelectMember3);
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
}

void ACodexTacticsPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	const float HoldDuration = Flow ? Flow->GetConfig().TurnBasedHoldDuration : 1.5f;
	// Space hold is measured in real time: the tactical pause slows the world down.
	if (SpaceInput.Tick(static_cast<float>(FApp::GetDeltaTime()), HoldDuration) == ESpaceInputAction::Hold)
	{
		HandleSpaceHold();
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
	if (const UTurnBasedCombatSubsystem* TurnBased = GetActiveTurnBased(); TurnBased && TurnBased->IsUnitMoving())
	{
		return;
	}
	if (USquadSubsystem* Squad = GetSquad(); Squad && Squad->GetLeader())
	{
		Squad->ToggleGuard(Squad->GetLeader());
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
	if (CancelGrenadeAim())
	{
		return;
	}
	// Godot: Esc cancels the turn-based object relocation first.
	if (UTurnBasedCombatSubsystem* TurnBased = GetActiveTurnBased(); TurnBased && TurnBased->IsRelocating())
	{
		TurnBased->CancelRelocate();
		return;
	}
	if (UDialogueSubsystem* Dialogue = GetWorld()->GetSubsystem<UDialogueSubsystem>())
	{
		Dialogue->SkipDialogue();
	}
}

void ACodexTacticsPlayerController::OnClick()
{
	if (IsDialogueOpen())
	{
		return; // the dialogue panel handles its own clicks
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

	USquadSubsystem* Squad = GetSquad();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	FHitResult Hit;
	if (!Leader || !GetHitResultUnderCursor(ECC_Visibility, false, Hit))
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

	// 2. Proximity check around cursor impact point
	if (!SelectedMember)
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
	const double Now = FPlatformTime::Seconds();
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
	if (Mode == ECodexCombatMode::TacticalPause)
	{
		const FVector Planned = Squad->PlanMove(Leader, Hit.ImpactPoint, bDoubleClick, Flow->GetConfig().PauseOrderRadius);
		if (UCombatFeedbackSubsystem* Feedback = GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>())
		{
			Feedback->SpawnWaypointMarker(Planned);
		}
		UE_LOG(LogCodexTactics, Log, TEXT("Planned move for %s to (%.0f, %.0f)%s"), *Leader->DisplayName.ToString(),
			Planned.X, Planned.Y, bDoubleClick ? TEXT(" sprint") : TEXT(""));
		return;
	}
	Leader->OrderMoveTo(Hit.ImpactPoint, bDoubleClick);
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
		if (Squad->SetLeaderByIndex(RosterIndex))
		{
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

	const bool bSolo = Squad->IsSoloMode();
	if (bSolo)
	{
		Leader->SetStance(Stance);
	}
	else
	{
		Squad->SetSquadStance(Stance);
	}

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

		if (bSolo)
		{
			Messages->PostMessage(Leader->DisplayName,
				FText::Format(LOCTEXT("SoloStanceFmt", "Стойка бойца {0}: {1}"), Leader->DisplayName, StanceName));
		}
		else
		{
			Messages->PostMessage(Leader->DisplayName,
				FText::Format(LOCTEXT("SquadStanceFmt", "Стойка отряда: {0}"), StanceName));
		}
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
	// Godot: RMB cancels the grenade aim, object placement (and the turn-based relocation).
	if (CancelGrenadeAim())
	{
		return;
	}
	if (URelocationSubsystem* Relocation = GetPlacingRelocation())
	{
		Relocation->CancelPlacement();
		return;
	}
	if (UTurnBasedCombatSubsystem* TurnBased = GetActiveTurnBased(); TurnBased && TurnBased->IsRelocating())
	{
		TurnBased->CancelRelocate();
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
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	const bool bPreparation = Flow && Flow->GetPhase() == ECodexGamePhase::Preparation;
	if (bPreparation || Squad->IsSoloMode())
	{
		Leader->SetStance(Next);
	}
	else
	{
		Squad->SetSquadStance(Next);
	}
}

void ACodexTacticsPlayerController::ToggleRelocateSelectMode()
{
	URelocationSubsystem* Relocation = GetWorld()->GetSubsystem<URelocationSubsystem>();
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>();
	if (Flow && !RelocationRules::CanRelocateNow(Flow->GetPhase(), Flow->GetCombatMode()))
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
