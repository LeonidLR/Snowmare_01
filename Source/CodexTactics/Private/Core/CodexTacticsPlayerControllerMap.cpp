#include "Core/CodexTacticsPlayerController.h"
#include "Core/CodexTacticsPlayerControllerInternal.h"
#include "Camera/TacticalCameraPawn.h"
#include "Core/PlayerCommands.h"
#include "Engine/World.h"
#include "UI/MinimapSubsystem.h"

// ACodexTacticsPlayerController: the minimap keys and clicks (user decisions 2026-10-18, docs/design/MINIMAP.md; UE-only) -
// F2 panel switch, Tab / F3 full-screen map, Esc closes it, wheel = map zoom, LMB = camera there, RMB = move order there.
// The map state lives in UMinimapSubsystem; the RMB order goes through HandleWorldHit like a ground click.

void ACodexTacticsPlayerController::MapPanelKey()
{
	if (UMinimapSubsystem* Minimap = UMinimapSubsystem::Get(this))
	{
		Minimap->TogglePanelMode();
		ControllerCommands::Report(this, PlayerCommands::MapPanel);
	}
}

void ACodexTacticsPlayerController::FullMapKey()
{
	if (UMinimapSubsystem* Minimap = UMinimapSubsystem::Get(this); Minimap && Minimap->IsReady())
	{
		Minimap->ToggleFullMap();
		ControllerCommands::Report(this, PlayerCommands::MapFull);
	}
}

bool ACodexTacticsPlayerController::CloseFullMap()
{
	UMinimapSubsystem* Minimap = UMinimapSubsystem::Get(this);
	if (!Minimap || !Minimap->IsFullMapOpen())
	{
		return false;
	}
	Minimap->SetFullMapOpen(false);
	return true;
}

bool ACodexTacticsPlayerController::IsCursorOverMinimap() const
{
	const UMinimapSubsystem* Minimap = UMinimapSubsystem::Get(this);
	FVector2D Mouse;
	return Minimap && GetMousePosition(Mouse.X, Mouse.Y) && Minimap->IsScreenPointOverMap(Mouse);
}

bool ACodexTacticsPlayerController::HandleMapWheel(float Notches)
{
	UMinimapSubsystem* Minimap = UMinimapSubsystem::Get(this);
	FVector2D Mouse;
	if (!Minimap || !GetMousePosition(Mouse.X, Mouse.Y) || !Minimap->HandleWheel(Mouse, Notches))
	{
		return false;
	}
	ControllerCommands::Report(this, PlayerCommands::MapZoom);
	return true;
}

bool ACodexTacticsPlayerController::HandleMapClick(const FVector2D& Screen, bool bRightButton)
{
	UMinimapSubsystem* Minimap = UMinimapSubsystem::Get(this);
	if (!Minimap)
	{
		return false;
	}
	FVector World = FVector::ZeroVector;
	switch (Minimap->HandleClick(Screen, bRightButton, World))
	{
	case EMinimapClick::None:
		return false;
	case EMinimapClick::Header:
		ControllerCommands::Report(this, PlayerCommands::MapPanel);
		return true;
	case EMinimapClick::Camera:
		if (ATacticalCameraPawn* CameraPawn = GetCameraPawn())
		{
			CameraPawn->LookAtPoint(World);
			ControllerCommands::Report(this, PlayerCommands::MapCamera, nullptr, false, false, nullptr, World);
		}
		return true;
	case EMinimapClick::Move:
	{
		// A ground click at that spot without an actor: no object / wall / body under a map click (no menus, no cover ghost).
		FHitResult Hit;
		Hit.ImpactPoint = World;
		Hit.Location = World;
		Hit.ImpactNormal = FVector::UpVector;
		Hit.Normal = FVector::UpVector;
		Hit.bBlockingHit = true;
		bLmbDown = false;
		bBoxSelecting = false;
		HandleWorldHit(Hit);
		return true;
	}
	case EMinimapClick::Consumed:
	default:
		return true;
	}
}
