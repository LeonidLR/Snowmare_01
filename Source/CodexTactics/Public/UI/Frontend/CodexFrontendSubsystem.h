#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CodexFrontendSubsystem.generated.h"

/**
 * Frontend flow across levels: NEW GAME / CONTINUE / LOAD from the menu map, QUIT TO MAIN MENU / QUIT GAME from the pause
 * menu. Hands the start request to the mission (UMissionSessionSubsystem: frontend start, pending save slot).
 * No Godot reference (2026-10-08 user decision: professional frontend).
 */
UCLASS()
class CODEXTACTICS_API UCodexFrontendSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UCodexFrontendSubsystem* Get(const UObject* WorldContext);

	/** NEW GAME: opens the campaign level at its start (UCodexFrontendSettings::NewGameLevel); its intro runs. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Frontend")
	void StartNewGame();

	/** CONTINUE: loads the newest save (false without one). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Frontend")
	bool ContinueLatest();

	/** Opens the slot's level and loads the slot there once it has started. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Frontend")
	bool LoadSlotFromFrontend(const FString& SlotName);

	/** QUIT TO MAIN MENU: opens the frontend level straight at the main menu (no title screen). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Frontend")
	void QuitToMainMenu();

	/** QUIT GAME. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Frontend")
	void QuitGame();

	/** True once after QuitToMainMenu: the frontend skips the title screen. */
	bool ConsumeSkipTitle();

	/** Is this world the frontend map (or run by the frontend game mode)? */
	static bool IsFrontendWorld(const UWorld* World);

	/** Save directory for every world of the session (smokes: Saved/SmokeSaves); empty = the save system's default. */
	UPROPERTY(BlueprintReadWrite, Category = "CodexTactics|Frontend")
	FString SaveDirectoryOverride;

	/** Smokes: QUIT GAME only logs instead of exiting. */
	bool bQuitDisabled = false;

	/** How many times QuitGame was requested (smokes). */
	int32 QuitRequests = 0;

private:
	void OpenLevel(const FSoftObjectPath& Level);

	bool bSkipTitleOnce = false;
};
