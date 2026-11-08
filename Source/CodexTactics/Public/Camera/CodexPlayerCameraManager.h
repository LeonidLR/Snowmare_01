#pragma once

#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "CodexPlayerCameraManager.generated.h"

/**
 * The player's camera manager (bug #52 on L_Gen_Depot_1, 2026-11-08): after the engine computed this frame's view - whichever view target
 * (the tactical camera pawn, a camera zone's fixed ACameraActor) or a blend between two of them - the final view location is pushed out of
 * every visible character's capsule (ATacticalCameraPawn::PushOutOfCharacters), so no view the player sees is ever inside a character. The
 * tactical pawn keeps its own 7 m floor and operative clearance; a fixed camera has no focus to measure a floor from, it is only kept out of
 * the capsules. Console Codex.Camera.OperativeClearance 0 = the engine's view untouched (A/B).
 * Godot reference: none (UE-only).
 */
UCLASS()
class CODEXTACTICS_API ACodexPlayerCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()

protected:
	virtual void DoUpdateCamera(float DeltaTime) override;
};
