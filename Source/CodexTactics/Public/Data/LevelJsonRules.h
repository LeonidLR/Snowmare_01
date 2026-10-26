#pragma once

#include "CoreMinimal.h"
#include "Data/WaveConfigTypes.h"

class FJsonObject;

/**
 * Level / wave data read at runtime from Content/Data/LevelJson/<file>.json (user decision 2026-10-02: Unreal is the
 * reference; the JSON is edited with the Wave Editor in Tools/TrainingGround and migrated from the Godot archive
 * data/configs/levels). Same contract as Godot main.gd _load_active_level_config / data/schemas/level_config.schema.json
 * and the former Scripts/Editor/import_levels.py: inactive waves ("is_active": false) are skipped, custom_stats keep
 * Godot units (m, m/s, s), squad_loadout falls back to the .get() defaults.
 */
namespace LevelJsonRules
{
	/** Folder of the level JSON files (Content/Data/LevelJson, staged with the game). */
	CODEXTACTICS_API FString GetLevelsDirectory();

	/** Map package/short name without the PIE prefix ("UEDPIE_0_L_Demo_A" / "/Game/Maps/L_Demo_A" -> "L_Demo_A"). */
	CODEXTACTICS_API FString NormalizeMapName(const FString& MapName);

	/**
	 * Which level JSON a map uses (pure rule, UE-only, 2026-10-12): the command-line override (-LevelJson=) wins; else
	 * "<MapName>.json" when that file exists (bMapFileExists); else the game mode's DefaultFile. Empty result = no JSON
	 * (the LevelConfig asset fallback).
	 */
	CODEXTACTICS_API FString ChooseLevelJsonFile(const FString& MapName, bool bMapFileExists, const FString& DefaultFile, const FString& CommandLineOverride);

	/** Same as ChooseLevelJsonFile, checking the level folder for "<MapName>.json" itself. */
	CODEXTACTICS_API FString ResolveLevelJsonFile(const FString& MapName, const FString& DefaultFile, const FString& CommandLineOverride);

	/** "HOUND" / "FROST_HOUND", "SPITTER", "BRUTE", "FROSTBITTEN", "CUTTER", "CRYO_DRONE", "MARKSMAN" (any case). */
	CODEXTACTICS_API bool ParseEnemyType(const FString& Name, EEnemyArchetype& OutType);

	/**
	 * The level JSON "demo" object (UE-only, Demo 01 C10): {"panic": false, "vision_cones": true, "tutorial": true,
	 * "tutorial_steps": "demo_01_steps.json", "hud": "demo"}; absent keys keep the normal game's behaviour.
	 */
	CODEXTACTICS_API FDemoModeSettings ParseDemoSettings(const FJsonObject& Demo);

	/**
	 * The level JSON "environment" object (UE-only, 2026-10-22): {"locked": true, "start_hour": 23, "weather": "Snowfall", "seed": 7,
	 * "visuals": false, "snow_cover": 0.6} ("visuals" phase 3, default true: false keeps the map's authored lighting, no weather presenter;
	 * "snow_cover" phase 4: the start snow cover 0..1 of a locked / fresh level, clamped);
	 * absent keys keep the procedural default, an unknown weather name is ignored (bHasWeather false) and reported in OutError.
	 * Section 17: "climate": "arctic" (default) | "temperate" (parsed first; a temperate level also takes the weather names Sunny / SunnyDay /
	 * Rain / Storm), "wetness" / "puddles" 0..1 (temperate start values); an unknown climate is reported and stays arctic.
	 */
	CODEXTACTICS_API FEnvironmentLevelSettings ParseEnvironmentSettings(const FJsonObject& Environment, FString* OutError = nullptr);

	/**
	 * Parses a level JSON text; false + OutError on invalid JSON or an unknown enemy type. A spawn entry's "environment_lock": true
	 * sets FEnemySpawnEntry::bEnvironmentLocked (the weather / time-of-day roster leaves it as written).
	 */
	CODEXTACTICS_API bool ParseLevel(const FString& Json, const FString& FallbackId, FLevelCombatConfig& OutConfig, FString& OutError);

	/** Loads and parses GetLevelsDirectory() / FileName. */
	CODEXTACTICS_API bool LoadLevel(const FString& FileName, FLevelCombatConfig& OutConfig, FString& OutError);
}
