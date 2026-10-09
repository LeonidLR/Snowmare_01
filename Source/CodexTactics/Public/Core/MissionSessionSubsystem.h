#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MissionSessionSubsystem.generated.h"

/** How a mission was started from the main menu (Godot GameState.last_selected_mode). */
UENUM(BlueprintType)
enum class EMissionStartMode : uint8
{
	/** Not started yet. */
	None,
	/** "Start Game": exploration, then combat after the gate. */
	Game,
	/** "Start Battle": quest chain completed, squad behind the gate, straight to the combat cutscene / preparation. */
	Combat
	// Godot's third mode "Start Exploration" is not offered (user decision 2026-09-29).
};

/**
 * State that survives a level reload: the last chosen start mode and the quick-restart flag (Ctrl + X starts the
 * same mode again without the menu). Godot reference: Scenes/movements/game_state.gd (last_selected_mode,
 * is_quick_restart).
 */
UCLASS()
class CODEXTACTICS_API UMissionSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Mission")
	EMissionStartMode LastMode = EMissionStartMode::None;

	/** Set by Ctrl + X before the reload; consumed by the next mission start. */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Mission")
	bool bQuickRestart = false;

	/**
	 * Set by the frontend (UCodexFrontendSubsystem NEW GAME / CONTINUE / LOAD) right before it opens the level; consumed
	 * by the next mission start: the intro briefing plays even in a headless check (unless a save is loaded).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Mission")
	bool bFrontendStart = false;

	/**
	 * Save slot to load once the next level has begun play (USaveGameSubsystem::LoadGameWithTravel — "Continue" / "Load
	 * Game" from a menu or a load that reopens the map): the mission starts in Game mode without the start menu and the
	 * intro, then the slot is applied. Cleared when consumed.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Save")
	FString PendingLoadSlot;

	/** Save directory of PendingLoadSlot (empty: Saved/SaveGames). */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Save")
	FString PendingLoadDirectory;

	bool HasPendingLoad() const { return !PendingLoadSlot.IsEmpty(); }
};
