// ACodexTacticsPlayerController: construction, BeginPlay / EndPlay (keys 1-4 Slate pre-processor), input actions and
// bindings, PlayerTick, and the small queries every input area uses (modifiers, HQ line, command lock, squad, turn-based).
// Split out of CodexTacticsPlayerController.cpp verbatim (cleanup 2026-10-15; map: docs/port/REFACTOR_CONTROLLER_2026-10.md).
// Godot reference: Scenes/movements/main.gd (_input and the click / key handlers it dispatches to).

#include "Core/CodexTacticsPlayerController.h"
#include "Widgets/SWindow.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/IInputProcessor.h"
#include "Camera/InteriorExitArrowActor.h"
#include "Camera/InteriorExitSubsystem.h"
#include "Combat/HoldSphereActor.h"
#include "Combat/CombatTimeModeRules.h"
#include "CodexTactics.h"
#include "Combat/GrenadeSubsystem.h"
#include "Characters/SquadTransferSubsystem.h"
#include "UI/DialogueRules.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "Tutorial/TutorialSubsystem.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Interactables/InteractableActor.h"
#include "Interactables/RelocationSubsystem.h"
#include "Interactables/TripwireActor.h"
#include "Misc/App.h"
#include "UI/GameMessageSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "InputAction.h"
#include "InputMappingContext.h"

#define LOCTEXT_NAMESPACE "CodexTacticsPlayerController"

ACodexTacticsPlayerController::ACodexTacticsPlayerController()
{
	// Top-down tactical game: cursor is always visible.
	bShowMouseCursor = true;
}

namespace SquadKeys
{
	/** Slate pre-processor: keys 1-4 reach the squad selection whatever widget holds the keyboard focus. */
	class FSquadKeyProcessor : public IInputProcessor
	{
	public:
		explicit FSquadKeyProcessor(ACodexTacticsPlayerController* InController) : Controller(InController) {}

		virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

		virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
		{
			ACodexTacticsPlayerController* PC = Controller.Get();
			if (!PC || InKeyEvent.IsRepeat() || InKeyEvent.IsControlDown() || InKeyEvent.IsAltDown())
			{
				return false;
			}
			const FKey Key = InKeyEvent.GetKey();
			if (Key != EKeys::One && Key != EKeys::Two && Key != EKeys::Three && Key != EKeys::Four)
			{
				return false;
			}
			// Only for the game's window, never while typing (a save name) or with the game paused (menu).
			const UGameViewportClient* Viewport = PC->GetWorld() ? PC->GetWorld()->GetGameViewport() : nullptr;
			const TSharedPtr<SWindow> GameWindow = Viewport ? Viewport->GetWindow() : nullptr;
			if (!GameWindow.IsValid() || SlateApp.GetActiveTopLevelWindow() != GameWindow || PC->IsPaused())
			{
				return false;
			}
			const TSharedPtr<SWidget> Focused = SlateApp.GetKeyboardFocusedWidget();
			const FName FocusType = Focused.IsValid() ? Focused->GetType() : NAME_None;
			if (FocusType == TEXT("SEditableText") || FocusType == TEXT("SMultiLineEditableText"))
			{
				return false;
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Select key %s (keyboard focus: %s)"), *Key.ToString(), *FocusType.ToString());
			return PC->HandleSquadNumberKey(Key);
		}

		virtual const TCHAR* GetDebugName() const override { return TEXT("CodexSquadKeys"); }

	private:
		TWeakObjectPtr<ACodexTacticsPlayerController> Controller;
	};
}

void ACodexTacticsPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (SquadKeyProcessor.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(SquadKeyProcessor);
	}
	SquadKeyProcessor.Reset();
	Super::EndPlay(EndPlayReason);
}

void ACodexTacticsPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// Keys 1-4 before any widget (the Enhanced Input binding stays for simulated input: the smokes).
	if (IsLocalController() && FSlateApplication::IsInitialized())
	{
		SquadKeyProcessor = MakeShared<SquadKeys::FSquadKeyProcessor>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(SquadKeyProcessor);
	}

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
		MakeAction(TEXT("IA_UseChocolate"), EKeys::L),
		MakeAction(TEXT("IA_UseHeatPack"), EKeys::U) }; // Sprint 15
	// Stealth lure (user decisions 2026-10-18): 5 whistle, 6 stone throw (every letter is taken; 5 / 6 sit next to the squad keys).
	StealthWhistleAction = MakeAction(TEXT("IA_StealthWhistle"), EKeys::Five);
	StealthThrowAction = MakeAction(TEXT("IA_StealthThrow"), EKeys::Six);
	QuickSaveAction = MakeAction(TEXT("IA_QuickSave"), EKeys::F5);
	QuickLoadAction = MakeAction(TEXT("IA_QuickLoad"), EKeys::F9);
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
	// Ctrl + T: Commander Mode on / off (Sprint 07-A); plain T stays the guard toggle (GuardKey skips it with Ctrl).
	InputComponent->BindKey(FInputChord(EKeys::T, false, true, false, false), IE_Pressed, this, &ACodexTacticsPlayerController::ToggleAutonomyKey);
	// Plain X (the chord without Ctrl): Godot switch_weapon cycle.
	InputComponent->BindKey(FInputChord(EKeys::X), IE_Pressed, this, &ACodexTacticsPlayerController::CycleWeaponKey);
	InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &ACodexTacticsPlayerController::EnterPressed);
	// Enter released: a short tap advances the dialogue line (a hold skips the conversation in PlayerTick).
	InputComponent->BindKey(EKeys::Enter, IE_Released, this, &ACodexTacticsPlayerController::EnterReleased);
	InputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &ACodexTacticsPlayerController::TabPressed);
	InputComponent->BindKey(EKeys::P, IE_Pressed, this, &ACodexTacticsPlayerController::ProfilePressed);
	// Sprint 12: N = cover fire mode (corner lean / blind fire) of the selected operative(s).
	InputComponent->BindKey(EKeys::N, IE_Pressed, this, &ACodexTacticsPlayerController::ToggleCoverFireModeKey);
	// APC P1 (user decision 2026-10-12): Y = board / get out (the selection), Alt + Y = the whole squad.
	InputComponent->BindKey(EKeys::Y, IE_Pressed, this, &ACodexTacticsPlayerController::VehicleKeyPressed);
	// Fire posture direct-select keys (user decision 2026-10-06): , Passive, . Defensive, / Aggressive.
	InputComponent->BindKey(EKeys::Comma, IE_Pressed, this, &ACodexTacticsPlayerController::PosturePassiveKey);
	InputComponent->BindKey(EKeys::Period, IE_Pressed, this, &ACodexTacticsPlayerController::PostureDefensiveKey);
	InputComponent->BindKey(EKeys::Slash, IE_Pressed, this, &ACodexTacticsPlayerController::PostureAggressiveKey);
	// Esc also closes the pause menu, so it runs while the world is paused.
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ACodexTacticsPlayerController::DialogueSkip).bExecuteWhenPaused = true;
	// F1: the Controls reference opens / closes at any time (it pauses the world itself, so it runs while paused).
	InputComponent->BindKey(EKeys::F1, IE_Pressed, this, &ACodexTacticsPlayerController::ControlsKey).bExecuteWhenPaused = true;
	// Demo 01 (2026-10-13): I = the inventory drawer (a tutorial lock target), O = the enemies' vision cones (Commandos style).
	InputComponent->BindKey(EKeys::I, IE_Pressed, this, &ACodexTacticsPlayerController::InventoryKey);
	InputComponent->BindKey(EKeys::O, IE_Pressed, this, &ACodexTacticsPlayerController::VisionConesKey);
	// Shift + O (2026-10-20): the operatives' own cones (what the squad sees); plain O stays the enemies'.
	InputComponent->BindKey(FInputChord(EKeys::O, true, false, false, false), IE_Pressed, this, &ACodexTacticsPlayerController::SquadVisionConesKey);
	// Rev 5g: M = interior camera <-> outside view inside a building (a free key; F1 row Camera.InteriorView).
	InputComponent->BindKey(EKeys::M, IE_Pressed, this, &ACodexTacticsPlayerController::InteriorViewKey);
	// Minimap (2026-10-18): F2 = the top-right panel MAP <-> MESSAGES, F3 = the full-screen map in any mode (Tab outside
	// turn-based combat, TabPressed).
	InputComponent->BindKey(EKeys::F2, IE_Pressed, this, &ACodexTacticsPlayerController::MapPanelKey);
	InputComponent->BindKey(EKeys::F3, IE_Pressed, this, &ACodexTacticsPlayerController::FullMapKey);

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
	Input->BindAction(ItemActions[4], ETriggerEvent::Started, this, &ACodexTacticsPlayerController::UseHeatPackKey);
	Input->BindAction(StealthWhistleAction, ETriggerEvent::Started, this, &ACodexTacticsPlayerController::StealthWhistleKey);
	Input->BindAction(StealthThrowAction, ETriggerEvent::Started, this, &ACodexTacticsPlayerController::StealthThrowKey);
	Input->BindAction(QuickSaveAction, ETriggerEvent::Started, this, &ACodexTacticsPlayerController::QuickSaveKey);
	Input->BindAction(QuickLoadAction, ETriggerEvent::Started, this, &ACodexTacticsPlayerController::QuickLoadKey);
}

void ACodexTacticsPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	// Enter held on a conversation: skip it all once the hold threshold is reached (real time, also in the tactical pause).
	if (bDialogueKeyDown && !bDialogueSkipFired)
	{
		const EDialogueKeyAction Action = DialogueRules::ResolveDialogueKey(static_cast<float>(FPlatformTime::Seconds() - DialogueKeyDownTime), false, false);
		if (Action == EDialogueKeyAction::SkipAll)
		{
			bDialogueSkipFired = true;
			ApplyDialogueKey(true);
		}
	}

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
	// Entry and exit thresholds may differ (FGameFlowConfig::TurnBasedHoldDuration / TurnBasedExitHoldDuration).
	const float HoldDuration = Flow ? FCombatTimeModeRules::GetHoldSeconds(Flow->GetCombatMode(), Flow->GetConfig()) : 1.5f;
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

	// Sprint 12: RMB / Esc drop the cover preview.
	if (HasCoverPreview() && (WasInputKeyJustPressed(EKeys::RightMouseButton) || WasInputKeyJustPressed(EKeys::Escape)))
	{
		HideCoverPreview();
	}

	// Rev 5c manual corner peek: hovering the end of the leader's (turn-based: the active unit's) cover - high, or low since rev 5e -
	// previews the peek.
	{
		UTurnBasedCombatSubsystem* PeekTurnBased = GetActiveTurnBased();
		USquadSubsystem* PeekSquad = GetSquad();
		AOperativeCharacter* PeekOperative = PeekTurnBased ? PeekTurnBased->GetActiveUnit() : (PeekSquad ? PeekSquad->GetLeader() : nullptr);
		// The minimap is UI: nothing in the world is hovered through it.
		const bool bOverMinimap = IsCursorOverMinimap();
		FHitResult PeekHit;
		const bool bPeekHit = PeekOperative && PeekOperative->IsInPeekableCover() && !IsDialogueOpen() && !bRelocateSelectMode && !bOverMinimap
			&& GetHitResultUnderCursor(ECC_Visibility, false, PeekHit);
		UpdateCornerPeekHover(PeekOperative, bPeekHit ? &PeekHit : nullptr);
		// 2026-10-20: from a wall edge, another cover under the cursor previews the cover switch (ghost + dash line; real time only).
		if (!bFreezeCoverHoverForTesting)
		{
			UpdateCoverSwitchHover(PeekTurnBased ? nullptr : PeekOperative, bPeekHit && !PeekTurnBased ? &PeekHit : nullptr);
			// 2026-10-20: round his own cover object (another face, the ground next to it) previews the way along its wall (ghost + path).
			UpdateCoverWrapHover(PeekTurnBased ? nullptr : PeekOperative, bPeekHit && !PeekTurnBased ? &PeekHit : nullptr);
		}
		// Rev 5e: a spotted tripwire (or the ground right next to it) under the cursor: the hand cursor - a click opens its menu.
		FHitResult TrapHit;
		const bool bCursorHit = !bCornerPeekCursor && !IsDialogueOpen() && !bOverMinimap && GetHitResultUnderCursor(ECC_Visibility, false, TrapHit);
		// Rev 5g: an EXIT arrow under the cursor lights up and shows the hand too.
		AInteriorExitArrowActor* HoveredExit = bCursorHit ? Cast<AInteriorExitArrowActor>(TrapHit.GetActor()) : nullptr;
		if (UInteriorExitSubsystem* Exits = GetWorld()->GetSubsystem<UInteriorExitSubsystem>())
		{
			Exits->SetHoveredArrow(HoveredExit);
		}
		const bool bTrap = bCursorHit && (HoveredExit || Cast<ATripwireActor>(TrapHit.GetActor())
			|| (!Cast<APawn>(TrapHit.GetActor()) && !Cast<AInteractableActor>(TrapHit.GetActor())
				&& ATripwireActor::FindClickableWireNear(GetWorld(), TrapHit.ImpactPoint)));
		if (bTrap != bTrapHoverCursor)
		{
			bTrapHoverCursor = bTrap;
			if (bTrap || !bCornerPeekCursor)
			{
				CurrentMouseCursor = bTrap ? EMouseCursor::Hand : DefaultMouseCursor.GetValue();
			}
		}
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

bool ACodexTacticsPlayerController::IsAttackModifierDown() const
{
	return bSimulatedAttackModifier || IsInputKeyDown(EKeys::LeftControl) || IsInputKeyDown(EKeys::RightControl);
}

bool ACodexTacticsPlayerController::IsSquadWideModifierDown() const
{
	return IsInputKeyDown(EKeys::LeftAlt) || IsInputKeyDown(EKeys::RightAlt);
}

void ACodexTacticsPlayerController::PostHeadquarters(const FText& Text) const
{
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(LOCTEXT("HQSpeaker", "HQ"), Text);
	}
}

bool ACodexTacticsPlayerController::RefuseIfLocked(FName CommandId) const
{
	return UTutorialSubsystem::CheckCommandLocked(this, CommandId);
}

UTurnBasedCombatSubsystem* ACodexTacticsPlayerController::GetActiveTurnBased() const
{
	UTurnBasedCombatSubsystem* TurnBased = GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>();
	return TurnBased && TurnBased->IsActive() ? TurnBased : nullptr;
}

USquadSubsystem* ACodexTacticsPlayerController::GetSquad() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
}

#undef LOCTEXT_NAMESPACE
