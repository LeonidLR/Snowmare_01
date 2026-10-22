#pragma once

#include "CoreMinimal.h"
#include "Data/WaveConfigTypes.h"
#include "Environment/EnvironmentTypes.h"

/**
 * Phase 2: the enemy roster by time of day and weather (pure; docs/systems/environment-weather.md §4.2, roster v2 approved
 * 2026-10-22). UWaveSubsystem applies ResolveSpawns once when a wave starts (conditions sampled then; later weather changes never
 * touch it). Map-placed enemies and locked levels are never resolved; spawn entries with bEnvironmentLocked stay as written.
 */
namespace EnvironmentRosterRules
{
	/** Suitability of Type in the cell (0..4); false (OutScore untouched) when Type has no row or the row is short. */
	CODEXTACTICS_API bool GetSuitability(const FEnvironmentRosterConfig& Roster, EEnemyArchetype Type, EWeatherState Weather,
		ETimeOfDayPhase Phase, float& OutScore);

	/** Share of a pair's From that swaps in the cell: 1 below AllBelow, 0.5 below HalfBelow, else 0. */
	CODEXTACTICS_API float GetSwapShare(const FEnvironmentRosterConfig& Roster, float Score);

	/**
	 * The wave's spawn entries for these conditions: per entry, pairs first (moved = round-half-up(count x share), the rest stays;
	 * the replacement is a new entry right after it with the same lane / delays, count moved x Ratio and NO custom stats), then the
	 * bonus multiplier on BonusTypes entries (round half up). Entries of count 0 are dropped. Disabled roster -> unchanged.
	 * OutLog (optional) gets one readable line per change ("MARKSMAN 1.4 < 2.0: 1 -> 2 FROSTBITTEN").
	 */
	CODEXTACTICS_API TArray<FEnemySpawnEntry> ResolveSpawns(const FEnvironmentRosterConfig& Roster, const TArray<FEnemySpawnEntry>& Spawns,
		EWeatherState Weather, ETimeOfDayPhase Phase, TArray<FString>* OutLog = nullptr);
}
