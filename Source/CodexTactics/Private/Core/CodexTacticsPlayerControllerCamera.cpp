// ACodexTacticsPlayerController: camera keys - zoom wheel, rotate keys, RMB drag rotate, MMB drag pan, interior view.
// Split out of CodexTacticsPlayerController.cpp verbatim (cleanup 2026-10-15; map: docs/port/REFACTOR_CONTROLLER_2026-10.md).
// Godot reference: Scenes/movements/main.gd (_input and the click / key handlers it dispatches to).

#include "Core/CodexTacticsPlayerController.h"
#include "Core/CodexTacticsPlayerControllerInternal.h"
#include "Camera/TacticalCameraPawn.h"
#include "Camera/InteriorExitSubsystem.h"
#include "Characters/SquadTransferSubsystem.h"
#include "Core/PlayerCommands.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "Interactables/RelocationSubsystem.h"
#include "UI/GameMessageSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "CodexTacticsPlayerController"

void ACodexTacticsPlayerController::InteriorViewKey()
{
	// Never locked (a camera command): the player must always be able to see his way out.
	UInteriorExitSubsystem* Exits = GetWorld()->GetSubsystem<UInteriorExitSubsystem>();
	bool bOutside = false;
	if (!Exits || !Exits->ToggleInteriorView(bOutside))
	{
		return; // not inside a building with an interior camera
	}
	ControllerCommands::Report(this, PlayerCommands::CameraInteriorView);
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(LOCTEXT("SquadSpeaker", "SQUAD"), bOutside
			? LOCTEXT("InteriorViewOutside", "Outside view - [M] back to the interior camera.")
			: LOCTEXT("InteriorViewInside", "Interior camera - [M] outside view."));
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
		ControllerCommands::Report(this, PlayerCommands::CameraZoom);
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
		ControllerCommands::Report(this, PlayerCommands::CameraZoom);
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
		ControllerCommands::Report(this, PlayerCommands::CameraRotateKeys);
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
		ControllerCommands::Report(this, PlayerCommands::CameraRotateKeys);
	}
}

void ACodexTacticsPlayerController::CameraDragRotateStart()
{
	// Sprint 10: Shift + RMB assigns a defense line instead of the camera drag.
	if (IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift))
	{
		AssignDefenseUnderCursor();
		return;
	}
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
		TurnBased->ExitAttackMode(TEXT("🟢 Aiming cancelled (back to move mode)."));
		return;
	}
	if (ATacticalCameraPawn* CameraPawn = GetCameraPawn())
	{
		CameraPawn->SetDragRotating(true);
		ControllerCommands::Report(this, PlayerCommands::CameraRotateMouse);
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
		ControllerCommands::Report(this, PlayerCommands::CameraPanDrag);
	}
}

void ACodexTacticsPlayerController::CameraDragPanStop()
{
	if (ATacticalCameraPawn* CameraPawn = GetCameraPawn())
	{
		CameraPawn->SetDragPanning(false);
	}
}

#undef LOCTEXT_NAMESPACE
