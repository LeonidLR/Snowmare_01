#include "Core/CodexTacticsPlayerController.h"
#include "Camera/TacticalCameraPawn.h"
#include "CodexTactics.h"
#include "Combat/EncounterQueries.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Interactables/InteractableActor.h"
#include "Interactables/InteractionSubsystem.h"
#include "Misc/App.h"
#include "UI/GameMessageSubsystem.h"

#define LOCTEXT_NAMESPACE "CodexTacticsPlayerController"
#include "InputActionValue.h"
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
		MakeAction(TEXT("IA_StanceProne"), EKeys::X) };

	CameraRotateLeftAction = MakeAction(TEXT("IA_CameraRotateLeft"), EKeys::Q);
	MappingContext->MapKey(CameraRotateLeftAction, EKeys::Left);
	CameraRotateRightAction = MakeAction(TEXT("IA_CameraRotateRight"), EKeys::E);
	MappingContext->MapKey(CameraRotateRightAction, EKeys::Right);
	CameraDragRotateAction = MakeAction(TEXT("IA_CameraDragRotate"), EKeys::RightMouseButton);
	CameraDragPanAction = MakeAction(TEXT("IA_CameraDragPan"), EKeys::MiddleMouseButton);
	SpaceAction = MakeAction(TEXT("IA_Space"), EKeys::SpaceBar);
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

	// Direct input bindings for mouse wheel (fail-safe for Slate cursor mode)
	InputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &ACodexTacticsPlayerController::OnMouseWheelUp);
	InputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &ACodexTacticsPlayerController::OnMouseWheelDown);

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

	Input->BindAction(CameraRotateLeftAction, ETriggerEvent::Started, this, &ACodexTacticsPlayerController::CameraRotateLeft);
	Input->BindAction(CameraRotateRightAction, ETriggerEvent::Started, this, &ACodexTacticsPlayerController::CameraRotateRight);
	Input->BindAction(CameraDragRotateAction, ETriggerEvent::Started, this, &ACodexTacticsPlayerController::CameraDragRotateStart);
	Input->BindAction(CameraDragRotateAction, ETriggerEvent::Completed, this, &ACodexTacticsPlayerController::CameraDragRotateStop);
	Input->BindAction(CameraDragPanAction, ETriggerEvent::Started, this, &ACodexTacticsPlayerController::CameraDragPanStart);
	Input->BindAction(CameraDragPanAction, ETriggerEvent::Completed, this, &ACodexTacticsPlayerController::CameraDragPanStop);
	Input->BindAction(SpaceAction, ETriggerEvent::Started, this, &ACodexTacticsPlayerController::SpacePressed);
	Input->BindAction(SpaceAction, ETriggerEvent::Completed, this, &ACodexTacticsPlayerController::SpaceReleased);
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
}

void ACodexTacticsPlayerController::SpacePressed()
{
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

void ACodexTacticsPlayerController::OnClick()
{
	USquadSubsystem* Squad = GetSquad();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	FHitResult Hit;
	if (!Leader || !GetHitResultUnderCursor(ECC_Visibility, false, Hit))
	{
		return;
	}

	// Clicking on (or right next to) a squad member selects it.
	for (AOperativeCharacter* Member : Squad->GetMembers())
	{
		if (FVector::Dist2D(Member->GetActorLocation(), Hit.ImpactPoint) <= SelectRadius)
		{
			Squad->SetLeader(Member);
			LastClickTime = -1.0;
			return;
		}
	}

	UInteractionSubsystem* Interactions = GetWorld()->GetSubsystem<UInteractionSubsystem>();
	// Clicking a quest object sends the leader to it; the interaction runs on arrival.
	if (AInteractableActor* Interactable = Cast<AInteractableActor>(Hit.GetActor()))
	{
		if (Interactions)
		{
			Interactions->RequestInteraction(Interactable);
		}
		LastClickTime = -1.0;
		return;
	}
	if (Interactions)
	{
		Interactions->CancelInteraction();
	}

	FVector2D MousePosition;
	GetMousePosition(MousePosition.X, MousePosition.Y);
	const double Now = FPlatformTime::Seconds();
	const bool bDoubleClick = LastClickTime >= 0.0
		&& Now - LastClickTime <= DoubleClickSeconds
		&& FVector2D::Distance(MousePosition, LastClickPosition) <= DoubleClickPixels;
	LastClickTime = bDoubleClick ? -1.0 : Now;
	LastClickPosition = MousePosition;

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
		UE_LOG(LogCodexTactics, Log, TEXT("Planned move for %s to (%.0f, %.0f)%s"), *Leader->DisplayName.ToString(),
			Planned.X, Planned.Y, bDoubleClick ? TEXT(" sprint") : TEXT(""));
		return;
	}
	Leader->OrderMoveTo(Hit.ImpactPoint, bDoubleClick);
}

void ACodexTacticsPlayerController::SelectMember(int32 RosterIndex)
{
	if (USquadSubsystem* Squad = GetSquad())
	{
		Squad->SetLeaderByIndex(RosterIndex);
	}
}

void ACodexTacticsPlayerController::ApplyStance(EOperativeStance Stance)
{
	USquadSubsystem* Squad = GetSquad();
	if (!Squad)
	{
		return;
	}
	if (IsInputKeyDown(EKeys::LeftAlt) || IsInputKeyDown(EKeys::RightAlt))
	{
		Squad->SetSquadStance(Stance);
	}
	else if (AOperativeCharacter* Leader = Squad->GetLeader())
	{
		Leader->SetStance(Stance);
	}
}

ATacticalCameraPawn* ACodexTacticsPlayerController::GetCameraPawn() const
{
	return GetPawn<ATacticalCameraPawn>();
}

void ACodexTacticsPlayerController::OnMouseWheelUp()
{
	if (ATacticalCameraPawn* CameraPawn = GetCameraPawn())
	{
		CameraPawn->AddZoomNotches(-1.f);
	}
}

void ACodexTacticsPlayerController::OnMouseWheelDown()
{
	if (ATacticalCameraPawn* CameraPawn = GetCameraPawn())
	{
		CameraPawn->AddZoomNotches(+1.f);
	}
}

void ACodexTacticsPlayerController::CameraRotateLeft()
{
	if (ATacticalCameraPawn* CameraPawn = GetCameraPawn())
	{
		CameraPawn->RotateStep(+1);
	}
}

void ACodexTacticsPlayerController::CameraRotateRight()
{
	if (ATacticalCameraPawn* CameraPawn = GetCameraPawn())
	{
		CameraPawn->RotateStep(-1);
	}
}

void ACodexTacticsPlayerController::CameraDragRotateStart()
{
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

#undef LOCTEXT_NAMESPACE
