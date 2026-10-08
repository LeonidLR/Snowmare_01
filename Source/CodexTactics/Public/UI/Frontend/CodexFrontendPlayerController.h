#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "CodexFrontendPlayerController.generated.h"

class ACodexMenuCameraAnchor;

/**
 * Player controller of the menu map: mouse cursor, the primary layout with the title screen (or the main menu after
 * QUIT TO MAIN MENU), and the menu camera that blends between ACodexMenuCameraAnchor actors.
 * No Godot reference (2026-10-08 user decision: professional frontend).
 */
UCLASS()
class CODEXTACTICS_API ACodexFrontendPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ACodexFrontendPlayerController();

	/** Blends the view to the anchor with this id (no-op when missing or already there); bInstant cuts. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Frontend")
	bool FocusCameraAnchor(FName AnchorId, bool bInstant = false);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Frontend")
	FName GetCurrentAnchorId() const { return CurrentAnchorId; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Frontend")
	ACodexMenuCameraAnchor* FindAnchor(FName AnchorId) const;

	/** Title screen («PRESS ANY KEY») on the Menu layer. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Frontend")
	void ShowTitle();

	/** Main menu on the Menu layer (removes the title). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Frontend")
	void ShowMainMenu();

protected:
	virtual void BeginPlay() override;

private:
	FName CurrentAnchorId;
};

/**
 * Game mode of the menu map (set in L_MainMenu's World Settings): no pawn, the frontend controller, no HUD.
 */
UCLASS()
class CODEXTACTICS_API ACodexFrontendGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACodexFrontendGameMode();
};
