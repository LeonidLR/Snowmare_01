#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraActor.h"
#include "CodexMenuCameraAnchor.generated.h"

/**
 * A camera position of the menu map (L_MainMenu). The menu camera blends to the anchor whose AnchorId matches the
 * selected menu entry (UCodexMenuButton::GetCameraAnchorId: Title, Continue, NewGame, LoadGame, Options, Credits, Quit).
 * Place / pilot it in the editor; the camera component's FOV and post process apply. Several entries may share one anchor
 * (set the button's CameraAnchorId). Blend time / curve: UCodexFrontendSettings, per anchor BlendTimeOverride.
 * No Godot reference (2026-10-08 user decision: professional frontend).
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API ACodexMenuCameraAnchor : public ACameraActor
{
	GENERATED_BODY()

public:
	/** Menu entry / screen this anchor frames. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Frontend")
	FName AnchorId = TEXT("Title");

	/** Blend time to this anchor in seconds; negative = the project default (Project Settings > Codex Frontend). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Frontend")
	float BlendTimeOverride = -1.f;
};
