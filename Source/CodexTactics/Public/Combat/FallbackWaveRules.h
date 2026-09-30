#pragma once

#include "CoreMinimal.h"

class UGodotBalanceAsset;
struct FRandomStream;

/** Enemy counts of a wave that has no level definition. */
struct FFallbackWaveCounts
{
	int32 Hounds = 0;
	int32 Spitters = 0;
	int32 Brutes = 0;

	int32 Total() const { return Hounds + Spitters + Brutes; }
};

/**
 * The balance-driven wave Godot spawns when the level JSON has no entry for the wave.
 * Godot reference: Scenes/movements/main.gd _start_next_wave (fallback branch).
 */
namespace FallbackWaveRules
{
	/**
	 * total = clamp(base_wave_enemy_count + (wave - 1) * enemy_count_per_wave_growth, min_enemies_per_wave,
	 * max_enemies_per_wave); the minimum hounds / spitters (/ brutes from brute_start_wave) come first, every remaining
	 * enemy is a brute (r < 0.20, from brute_start_wave, at most 3), a hound (r < 0.65) or a spitter.
	 * Config may be null (Godot fallback numbers).
	 */
	CODEXTACTICS_API FFallbackWaveCounts Compute(const UGodotBalanceAsset* Config, int32 WaveIndex, FRandomStream& Random);
}
