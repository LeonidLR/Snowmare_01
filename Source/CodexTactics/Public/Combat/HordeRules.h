#pragma once

#include "CoreMinimal.h"
#include "Combat/CombatPressureRules.h"
#include "Combat/FirefightRules.h"
#include "Data/CombatTypes.h"
#include "GameFlow/GameFlowTypes.h"

class FJsonObject;

/** One archetype of the horde mix with its share (weights need not sum to 1). */
struct CODEXTACTICS_API FHordeMixEntry
{
	EEnemyArchetype Type = EEnemyArchetype::FrostHound;
	float Weight = 1.f;
	/**
	 * The entry's variant Blueprint (task #34's mechanism, horde #12 2026-11-09: "MILITARY_ZOMBIE" = Frostbitten + BP_Enemy_MilitaryZombie via
	 * ACodexTacticsGameMode::EnemyVariantClasses); NAME_None = the archetype's own Blueprint.
	 */
	FName Variant;
};

/**
 * The horde (UE-only). Since the game designer's decision 2026-11-09 it comes ONLY after N firefights in a row on the level (FirefightRules,
 * UFirefightSubsystem); the old trigger after 240 s of real-time fight (user request 2026-10-06: "trigger_seconds", "repeats",
 * "repeat_seconds", "count_increase_per_repeat", Codex.Horde.TriggerSeconds / RepeatSeconds) is gone - those keys are ignored if a file
 * still has them. Defaults: Content/Data/AI/horde.json (read at game start, like enemy_perception.json); per level: the level JSON
 * "horde_enabled" (false switches it off), "horde_after_firefights" and an optional "horde" object with any of the same keys (overrides the
 * file for that level). Distances in the JSON are in meters, times in seconds; here in cm / s. Console: Codex.Horde.Enabled /
 * Codex.Horde.AfterFirefights / Codex.Horde.FirefightWarningSeconds (-1 = data).
 */
struct CODEXTACTICS_API FHordeConfig
{
	/** "enabled": the horde exists at all (the level's "horde_enabled" must agree). */
	bool bEnabled = true;
	/** "count": enemies of a horde. */
	int32 Count = 8;
	/**
	 * "composition": { "FROST_HOUND": 5, "FROSTBITTEN": 3 } — weights per level-JSON enemy type or variant key ("MILITARY_ZOMBIE"); the
	 * Spitter is refused (task #34 / horde #12: the Military Zombie replaced it outside the stage waves).
	 */
	TArray<FHordeMixEntry> Composition = { { EEnemyArchetype::FrostHound, 5.f }, { EEnemyArchetype::Frostbitten, 3.f } };
	/** "min_distance_m" / "max_distance_m": spawn band around the squad's centre. */
	float MinDistanceCm = 2500.f;
	float MaxDistanceCm = 4500.f;
	/** "cluster_radius_m": the members gather within this radius of the spawn point. */
	float ClusterRadiusCm = 400.f;
	/** "prefer_out_of_sight": a point no living operative sees wins over a visible one. */
	bool bPreferOutOfSight = true;
	/** "spawn_samples": random candidate points tried around the squad. */
	int32 SpawnSamples = 32;
	/** "apply_wave_modifiers": the current wave's hp / damage / speed multipliers (the level's difficulty) apply. */
	bool bApplyWaveModifiers = true;
	/** "warning_seconds": how long the HUD shows "HORDE!" and the direction arrow. */
	float WarningSeconds = 6.f;
	/** "warning_sound": optional sound asset path played once (empty: none — the project has no audio cue yet). */
	FString WarningSound;
	/**
	 * Horde #12: the horde comes after this many firefights IN A ROW on the level (the level JSON root key "horde_after_firefights", default
	 * 10 - game designer 2026-11-09, FirefightRules::DefaultAfterFirefights; min 1; console Codex.Horde.AfterFirefights).
	 */
	int32 AfterFirefights = FirefightRules::DefaultAfterFirefights;
	/** Horde #12: the firefight count and slip-past tuning ("firefight_*" keys of horde.json / the level "horde" override). */
	FFirefightConfig Firefight;
};

/** A candidate spawn point measured by the subsystem (navmesh, path, squad sight). */
struct CODEXTACTICS_API FHordeSpawnCandidate
{
	FVector Location = FVector::ZeroVector;
	/** 2D distance to the squad's centre, cm. */
	float DistanceCm = 0.f;
	/** On the navmesh with a full path to the squad. */
	bool bReachable = false;
	/** Some living operative sees the point. */
	bool bVisibleToSquad = false;
};

/** Pure horde rules (tests CodexTactics.Combat.Horde.*). */
namespace HordeRules
{
	/**
	 * The firefight horde's warning countdown runs now: in exploration or a wave fight in full real time - not in the tactical pause, not in
	 * turn-based combat, not while dialogues / cutscenes hold the world AI (UWorldAIPauseSubsystem), not in a cutscene / preparation /
	 * post-combat phase.
	 */
	CODEXTACTICS_API bool IsCountdownRunning(ECodexGamePhase Phase, ECodexCombatMode Mode, bool bWorldAIPaused);

	/** The distance lies in [MinDistanceCm, MaxDistanceCm]. */
	CODEXTACTICS_API bool IsInDistanceBand(float DistanceCm, const FHordeConfig& Config);

	/**
	 * The spawn point: a reachable candidate in the distance band, one out of the squad's sight first (when
	 * bPreferOutOfSight), the farthest-from-the-band-edges first among equals. INDEX_NONE when none qualifies.
	 */
	CODEXTACTICS_API int32 PickSpawnPoint(const TArray<FHordeSpawnCandidate>& Candidates, const FHordeConfig& Config);

	/** Enemies of a horde: Count, at least 1. */
	CODEXTACTICS_API int32 GetHordeSize(const FHordeConfig& Config);

	/** The archetypes of one horde of Total enemies by the composition weights (largest remainder, stable order). */
	CODEXTACTICS_API TArray<EEnemyArchetype> BuildComposition(const FHordeConfig& Config, int32 Total);

	/** BuildComposition with each member's variant (the composition entry's; NAME_None = the archetype's own Blueprint). */
	CODEXTACTICS_API TArray<FPressureEnemyType> BuildMembers(const FHordeConfig& Config, int32 Total);

	/** A type the horde may spawn: every archetype but the Spitter (it stays in the game for the stage waves only, horde #12). */
	CODEXTACTICS_API bool IsAllowedHordeType(EEnemyArchetype Type);

	/** A "composition" key: a level-JSON enemy type or a variant key ("MILITARY_ZOMBIE"); false for unknown / refused ones. */
	CODEXTACTICS_API bool ParseCompositionKey(const FString& Key, FHordeMixEntry& OutEntry);

	/** Spawn offset of member Index of Count within the cluster radius (the first at the centre, then a sunflower). */
	CODEXTACTICS_API FVector ClusterOffset(int32 Index, int32 Count, float ClusterRadiusCm);

	/**
	 * Applies the keys present in Json over Config (meters -> cm); unknown archetypes in "composition" are skipped; the removed timer keys
	 * ("trigger_seconds", "repeats", "repeat_seconds", "count_increase_per_repeat") are ignored.
	 */
	CODEXTACTICS_API void ApplyJson(const FJsonObject& Json, FHordeConfig& Config);

	/** ApplyJson from a JSON text; false (Config unchanged) when it is not a JSON object. */
	CODEXTACTICS_API bool ApplyJsonText(const FString& Text, FHordeConfig& Config);

	/** Content/Data/AI/horde.json. */
	CODEXTACTICS_API FString GetDefaultPath();

	/** The defaults file over the built-in values (built-in when it is missing / invalid). */
	CODEXTACTICS_API FHordeConfig LoadDefaults();

	/**
	 * The config in force for a level: Defaults, then the level's "horde" object (LevelOverrideJson, may be empty), off
	 * when the level says "horde_enabled": false.
	 */
	CODEXTACTICS_API FHordeConfig ResolveForLevel(const FHordeConfig& Defaults, bool bLevelHordeEnabled, const FString& LevelOverrideJson,
		int32 LevelAfterFirefights = FirefightRules::DefaultAfterFirefights);
}
