#pragma once

#include "CoreMinimal.h"
#include "Data/WaveConfigTypes.h"

/**
 * Level / wave data read at runtime from Content/Data/LevelJson/<file>.json (user decision 2026-10-02: Unreal is the
 * reference; the JSON is edited with the Wave Editor in Tools/WaveEditor and migrated from the Godot archive
 * data/configs/levels). Same contract as Godot main.gd _load_active_level_config / data/schemas/level_config.schema.json
 * and the former Scripts/Editor/import_levels.py: inactive waves ("is_active": false) are skipped, custom_stats keep
 * Godot units (m, m/s, s), squad_loadout falls back to the .get() defaults.
 */
namespace LevelJsonRules
{
	/** Folder of the level JSON files (Content/Data/LevelJson, staged with the game). */
	CODEXTACTICS_API FString GetLevelsDirectory();

	/** "HOUND" / "FROST_HOUND", "SPITTER", "BRUTE", "FROSTBITTEN", "CUTTER", "CRYO_DRONE", "MARKSMAN" (any case). */
	CODEXTACTICS_API bool ParseEnemyType(const FString& Name, EEnemyArchetype& OutType);

	/** Parses a level JSON text; false + OutError on invalid JSON or an unknown enemy type. */
	CODEXTACTICS_API bool ParseLevel(const FString& Json, const FString& FallbackId, FLevelCombatConfig& OutConfig, FString& OutError);

	/** Loads and parses GetLevelsDirectory() / FileName. */
	CODEXTACTICS_API bool LoadLevel(const FString& FileName, FLevelCombatConfig& OutConfig, FString& OutError);
}
