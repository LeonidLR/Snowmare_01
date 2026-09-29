#pragma once

#include "CoreMinimal.h"
#include "GameFlow/GameFlowTypes.h"

struct FLevelCombatConfig;
class UGodotBalanceAsset;

/**
 * Pure rules that feed the mission flow from the level data (imported Godot level JSON, DA_Level_*) and the balance.
 * Godot reference: Scenes/movements/main.gd _load_active_level_config (prep_phase_duration, wave_rest_duration,
 * max_waves = active waves) and the _ready fallback (preparation_phase_duration, max_campaign_waves from
 * game_balance_config.tres when the level has no such fields).
 */
namespace LevelFlowRules
{
	/**
	 * Returns Base with the level's preparation / rest durations and wave count applied. Without a level (nullptr),
	 * Godot falls back to the balance: preparation_phase_duration and max_campaign_waves (default 10); the rest keeps
	 * main.gd's default 20 s. Either source may be missing; missing values keep Base.
	 */
	CODEXTACTICS_API FGameFlowConfig ApplyLevel(const FGameFlowConfig& Base, const FLevelCombatConfig* Level, const UGodotBalanceAsset* Balance);
}
