#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MissionSessionSubsystem.generated.h"

/** How a mission was started from the main menu (Godot GameState.last_selected_mode). */
UENUM(BlueprintType)
enum class EMissionStartMode : uint8
{
	/** Not chosen yet: the main menu is shown. */
	None,
	/** «Начать игру»: exploration, then combat after the gate. */
	Game,
	/** «Начать бой»: quest chain completed, squad behind the gate, straight to the combat cutscene / preparation. */
	Combat
	// Godot's third mode «Начать исследование» is not offered (user decision 2026-09-29).
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
};
