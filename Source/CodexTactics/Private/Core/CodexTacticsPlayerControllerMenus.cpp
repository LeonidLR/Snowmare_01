// ACodexTacticsPlayerController: dialogue keys (Space / Enter / Esc) and UI keys - profile, Tab, controls,
// inventory, vision cones, quick save / load, restart.
// Split out of CodexTacticsPlayerController.cpp verbatim (cleanup 2026-10-15; map: docs/port/REFACTOR_CONTROLLER_2026-10.md).
// Godot reference: Scenes/movements/main.gd (_input and the click / key handlers it dispatches to).

#include "Core/CodexTacticsPlayerController.h"
#include "Core/CodexTacticsPlayerControllerInternal.h"
#include "Combat/HordeIntroSubsystem.h"
#include "Characters/SquadTransferSubsystem.h"
#include "Core/MissionSubsystem.h"
#include "Core/PlayerCommands.h"
#include "Core/SaveGameSubsystem.h"
#include "UI/CodexTacticsHUD.h"
#include "Combat/VisionConeComponent.h"
#include "UI/DialogueRules.h"
#include "UI/DialogueSubsystem.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "Tutorial/TutorialSubsystem.h"
#include "UI/GameMessageSubsystem.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "UI/Frontend/CodexUISubsystem.h"

#define LOCTEXT_NAMESPACE "CodexTacticsPlayerController"

void ACodexTacticsPlayerController::QuickSaveKey()
{
	if (USaveGameSubsystem* Saves = GetWorld()->GetSubsystem<USaveGameSubsystem>())
	{
		if (Saves->QuickSave())
		{
			ControllerCommands::Report(this, PlayerCommands::QuickSave);
		}
	}
}

void ACodexTacticsPlayerController::QuickLoadKey()
{
	if (USaveGameSubsystem* Saves = GetWorld()->GetSubsystem<USaveGameSubsystem>())
	{
		ControllerCommands::Report(this, PlayerCommands::QuickLoad);
		Saves->QuickLoad();
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

bool ACodexTacticsPlayerController::HasDialogueForKey() const
{
	if (IsDialogueOpen())
	{
		return true;
	}
	const UTutorialSubsystem* Tutorial = GetWorld()->GetSubsystem<UTutorialSubsystem>();
	return Tutorial && Tutorial->IsSpeechActive() && !GetActiveTurnBased();
}

void ACodexTacticsPlayerController::ApplyDialogueKey(bool bSkip)
{
	bool bDone = false;
	if (UDialogueSubsystem* Dialogue = GetWorld()->GetSubsystem<UDialogueSubsystem>(); Dialogue && Dialogue->IsDialogueOpen())
	{
		if (bSkip)
		{
			Dialogue->SkipDialogue();
		}
		else
		{
			Dialogue->AdvanceLine();
		}
		bDone = true;
	}
	else if (UTutorialSubsystem* Tutorial = GetWorld()->GetSubsystem<UTutorialSubsystem>())
	{
		bDone = bSkip ? Tutorial->SkipSpeech() : Tutorial->AdvanceSpeechLine();
	}
	if (bDone)
	{
		ControllerCommands::Report(this, bSkip ? PlayerCommands::DialogueSkip : PlayerCommands::DialogueNext);
	}
}

void ACodexTacticsPlayerController::EnterReleased()
{
	if (!bDialogueKeyDown)
	{
		return;
	}
	bDialogueKeyDown = false;
	const EDialogueKeyAction Action = DialogueRules::ResolveDialogueKey(static_cast<float>(FPlatformTime::Seconds() - DialogueKeyDownTime), true, bDialogueSkipFired);
	bDialogueSkipFired = false;
	if (Action != EDialogueKeyAction::None)
	{
		ApplyDialogueKey(Action == EDialogueKeyAction::SkipAll);
	}
}

void ACodexTacticsPlayerController::EnterPressed()
{
	if (HasDialogueForKey())
	{
		// The dialogue key (user request 2026-10-13): the release decides "next line", a hold skips (PlayerTick).
		bDialogueKeyDown = true;
		bDialogueSkipFired = false;
		DialogueKeyDownTime = FPlatformTime::Seconds();
	}
	else if (UTurnBasedCombatSubsystem* TurnBased = GetActiveTurnBased())
	{
		TurnBased->PassSquadTurn();
		ControllerCommands::Report(this, PlayerCommands::EndTurn);
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
		ControllerCommands::Report(this, PlayerCommands::NextUnit);
		return;
	}
	// Minimap (2026-10-18): outside turn-based combat Tab opens / closes the full-screen map (F3 works in every mode).
	FullMapKey();
}

void ACodexTacticsPlayerController::ControlsKey()
{
	if (UCodexUISubsystem* UI = UCodexUISubsystem::Get(this))
	{
		UI->ToggleControlsScreen();
	}
}

void ACodexTacticsPlayerController::InventoryKey()
{
	if (ACodexTacticsHUD* Hud = GetHUD<ACodexTacticsHUD>())
	{
		Hud->ToggleInventoryDrawer();
	}
}

void ACodexTacticsPlayerController::VisionConesKey()
{
	if (RefuseIfLocked(PlayerCommands::ToggleVisionCones))
	{
		return;
	}
	const bool bOn = !UVisionConeComponent::IsPlayerEnemyConesOn();
	UVisionConeComponent::SetPlayerEnemyCones(bOn);
	ControllerCommands::Report(this, PlayerCommands::ToggleVisionCones);
	GameplayEvents::Raise(this, GameplayEvents::VisionConesToggled);
	GameplayEvents::Raise(this, FName(*(GameplayEvents::VisionConesToggled.ToString() + (bOn ? TEXT(".On") : TEXT(".Off")))));
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(LOCTEXT("SquadSpeaker", "SQUAD"), bOn
			? LOCTEXT("VisionConesOn", "Vision cones ON: what the enemy can see is drawn on the ground.")
			: LOCTEXT("VisionConesOff", "Vision cones OFF."));
	}
}

void ACodexTacticsPlayerController::SquadVisionConesKey()
{
	// Locked with O (the tutorial teaches both on the A4 cones card), reported as its own command.
	if (RefuseIfLocked(PlayerCommands::ToggleVisionCones))
	{
		return;
	}
	const bool bOn = !UVisionConeComponent::IsPlayerSquadConesOn();
	UVisionConeComponent::SetPlayerSquadCones(bOn);
	ControllerCommands::Report(this, PlayerCommands::ToggleSquadVisionCones);
	GameplayEvents::Raise(this, GameplayEvents::SquadVisionConesToggled);
	GameplayEvents::Raise(this, FName(*(GameplayEvents::SquadVisionConesToggled.ToString() + (bOn ? TEXT(".On") : TEXT(".Off")))));
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(LOCTEXT("SquadSpeaker", "SQUAD"), bOn
			? LOCTEXT("SquadVisionConesOn", "Squad vision cones ON: what your operatives can see is drawn on the ground.")
			: LOCTEXT("SquadVisionConesOff", "Squad vision cones OFF."));
	}
}

void ACodexTacticsPlayerController::DialogueSkip()
{
	// The formation radial menu (hold B) first: Esc cancels it, the previous choice stays.
	if (CancelFormationMenu())
	{
		return;
	}
	ACodexTacticsHUD* Hud = Cast<ACodexTacticsHUD>(GetHUD());
	// Pause menu screens first (they are open while the world is paused).
	if (Hud && Hud->IsPauseMenuOpen())
	{
		Hud->HandleEscape();
		return;
	}
	// Minimap (2026-10-18): Esc closes the full-screen map first.
	if (CloseFullMap())
	{
		return;
	}
	// Sudden horde start (user decision 2026-10-11): Esc skips the camera flyover (the free tactical pause stays); before
	// the horde's trigger it opens the free pause at once.
	if (UHordeIntroSubsystem* HordeIntro = GetWorld()->GetSubsystem<UHordeIntroSubsystem>(); HordeIntro && HordeIntro->HandleSkipInput(true))
	{
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
		TurnBased->ExitAttackMode(TEXT("🟢 Aiming cancelled (back to move mode)."));
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

void ACodexTacticsPlayerController::RestartMission()
{
	if (UMissionSubsystem* Mission = GetWorld()->GetSubsystem<UMissionSubsystem>())
	{
		Mission->RestartMission(/*bQuick*/ true);
	}
}

#undef LOCTEXT_NAMESPACE
