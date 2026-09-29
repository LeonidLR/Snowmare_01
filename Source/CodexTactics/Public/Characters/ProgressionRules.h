#pragma once

#include "CoreMinimal.h"
#include "Data/CombatTypes.h"
#include "ProgressionRules.generated.h"

class UGodotBalanceAsset;

/** Stat a free point can go into (Godot player.gd stat names "HP", "LUCK", "ACCURACY", "FORTITUDE"). */
UENUM(BlueprintType)
enum class EProgressStat : uint8
{
	Health,
	Luck,
	Accuracy,
	Fortitude
};

/**
 * Pure experience / level / stat point rules.
 * Godot reference: Scenes/movements/player.gd get_next_level_exp, add_exp, _on_level_up, can_/increase_/decrease_stat;
 * enemy_base.gd / enemy_cutter.gd kill rewards.
 */
namespace ProgressionRules
{
	constexpr int32 MaxLevel = 10;
	constexpr int32 PointsPerLevel = 3;
	/** Godot HEALTH_CAP. */
	constexpr float HealthCap = 200.f;

	/** EXP for the next level: level x 250 (2500 shown at the level cap). */
	CODEXTACTICS_API int32 NextLevelExp(int32 Level);

	/** Adds EXP (nothing at the level cap), carrying the overflow through each level-up; returns the levels gained. */
	CODEXTACTICS_API int32 AddExp(int32& Level, int32& CurrentExp, int32 Amount);

	/** One point buys +5 max health or +1 of the other stats. */
	CODEXTACTICS_API float StatStep(EProgressStat Stat);

	/** Upper bound: health 200, luck 60, accuracy 100, fortitude 50. */
	CODEXTACTICS_API float StatCap(EProgressStat Stat);

	/** A free point is left and the step stays within the cap. */
	CODEXTACTICS_API bool CanIncrease(EProgressStat Stat, float Value, int32 UnspentPoints);

	/** The step back stays at or above the value the operative started the mission with. */
	CODEXTACTICS_API bool CanDecrease(EProgressStat Stat, float Value, float InitialBase);

	/**
	 * EXP every squad member gets for a kill: game_balance_config exp_reward_<type> (hound, spitter, brute, frostbitten),
	 * the cutter always 16, anything else 9. Without a config the game_balance_config.gd defaults apply.
	 */
	CODEXTACTICS_API int32 KillReward(EEnemyArchetype Archetype, const UGodotBalanceAsset* Config);

	/** EXP every squad member gets for a cleared wave: game_balance_config exp_reward_wave_complete (default 40). */
	CODEXTACTICS_API int32 WaveClearReward(const UGodotBalanceAsset* Config);

	/** Profile line «Срез: -N%»: fortitude x 1.5, clamped to [0, 50]. */
	CODEXTACTICS_API int32 FortitudeCutPercent(float Fortitude);
}
