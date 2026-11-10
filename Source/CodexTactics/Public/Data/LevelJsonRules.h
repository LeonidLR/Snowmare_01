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
	/** "start_mode" (2026-11-09): "solo" -> false, "formation" -> true (case-insensitive); false on anything else. */
	CODEXTACTICS_API bool ParseStartMode(const FString& Name, bool& bOutFormation);

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
	 * The level JSON "pressure" object (combat lab, game designer 2026-11-01, docs/design/COMBAT_LAB.md): {"enabled": true, "interval_sec": 120,
	 * "first_event_sec": 15, "group_min": 1, "group_max": 3, "max_alive": 4, "patrol_share": 0.6, "seed": 1101, "types": {"HOUND": 1, ...},
	 * "wall_clearance_cm": 250, "min_squad_distance_cm": 2500, "max_squad_distance_cm": 5000, "patrol": {"points", "radius_cm", "wait_sec"},
	 * "ambush": {"min_squad_distance_cm", "max_squad_distance_cm", "watch_half_angle_deg", "watch_range_cm", "strike_range_cm",
	 * "spots": {"wall_standoff_cm", "end_inset_cm", "min_wall_length_cm", "joined_gap_cm", "doorway_min_cm", "doorway_max_cm", "authored_bonus",
	 * "doorway_bonus", "merge_cm"}, "points": [[x, y], ...], "coordinated": {"share", "bodies_min", "bodies_max", "max_alive",
	 * "min_squad_distance_cm", "max_squad_distance_cm", "anchor_ahead_cm", "flank_offset_cm", "front_offset_cm", "rear_offset_cm",
	 * "min_separation_cm", "trigger_range_cm", "assault_min_sec", "assault_max_sec", "block_distance_cm", "min_ranged"},
	 * "relocate": {"enabled", "after_sec", "behind_angle_deg", "far_cm", "min_ahead_cos"}} (designer answers 2026-11-01),
	 * "recycle": {"distance_cm", "after_sec"}}. Absent keys keep the FCombatPressureConfig defaults, values are clamped to sane ranges;
	 * "enabled" defaults to true. False + OutError on an unknown type name, a type the pressure never spawns (BRUTE, CRYO_DRONE) or an
	 * ambush point that is not [x, y].
	 */
	CODEXTACTICS_API bool ParsePressureSettings(const FJsonObject& Pressure, FCombatPressureConfig& OutConfig, FString* OutError = nullptr);

	/**
	 * Parses a level JSON text; false + OutError on invalid JSON or an unknown enemy type. A spawn entry's "environment_lock": true
	 * sets FEnemySpawnEntry::bEnvironmentLocked (the weather / time-of-day roster leaves it as written).
	 */
	CODEXTACTICS_API bool ParseLevel(const FString& Json, const FString& FallbackId, FLevelCombatConfig& OutConfig, FString& OutError);

	/** Loads and parses GetLevelsDirectory() / FileName. */
	CODEXTACTICS_API bool LoadLevel(const FString& FileName, FLevelCombatConfig& OutConfig, FString& OutError);
}
