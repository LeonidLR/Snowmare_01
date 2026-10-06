#pragma once

#include "CoreMinimal.h"
#include "Data/CombatTypes.h"
#include "GameFlow/GameFlowTypes.h"

class FJsonObject;

/** One archetype of the horde mix with its share (weights need not sum to 1). */
struct CODEXTACTICS_API FHordeMixEntry
{
	EEnemyArchetype Type = EEnemyArchetype::FrostHound;
	float Weight = 1.f;
};

/**
 * Horde after a long real-time fight (user request 2026-10-06, UE-only). Defaults: Content/Data/AI/horde.json (read at
 * game start, like enemy_perception.json); per level: the level JSON "horde_enabled" (false switches it off) and an
 * optional "horde" object with any of the same keys (overrides the file for that level). Distances in the JSON are in
 * meters, times in seconds; here in cm / s. Console: Codex.Horde.TriggerSeconds / Codex.Horde.Enabled (-1 = data).
 */
struct CODEXTACTICS_API FHordeConfig
{
	/** "enabled": the horde exists at all (the level's "horde_enabled" must agree). */
	bool bEnabled = true;
	/** "trigger_seconds": real-time fight seconds (pause / turn-based / dialogue excluded) before the first horde. */
	float TriggerSeconds = 240.f;
	/** "repeats": more hordes after the first, every RepeatSeconds of further fight time. */
	bool bRepeats = false;
	/** "repeat_seconds". */
	float RepeatSeconds = 120.f;
	/** "count": enemies of the first horde. */
	int32 Count = 8;
	/** "count_increase_per_repeat": extra enemies for each further horde of the same fight. */
	int32 CountIncreasePerRepeat = 0;
	/** "composition": { "FROST_HOUND": 5, "FROSTBITTEN": 3 } — weights per level-JSON enemy type. */
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
	/** "warning_seconds": how long the HUD shows «ОРДА!» and the direction arrow. */
	float WarningSeconds = 6.f;
	/** "warning_sound": optional sound asset path played once (empty: none — the project has no audio cue yet). */
	FString WarningSound;
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

/**
 * Fight clock of the horde: counts only running real-time fight time and fires at TriggerSeconds, then (option) every
 * RepeatSeconds. Reset at the start of every fight (a wave, an ambush fight).
 */
struct CODEXTACTICS_API FHordeTimer
{
	/** Running real-time fight seconds counted so far. */
	float CombatSeconds = 0.f;
	/** Hordes released in this fight. */
	int32 HordesReleased = 0;

	void Reset() { CombatSeconds = 0.f; HordesReleased = 0; }

	/** Fight seconds of the next horde (TNumericLimits<float>::Max() when none is left). */
	float GetNextTriggerSeconds(const FHordeConfig& Config) const;

	/**
	 * Adds DeltaSeconds when bCounting; true when a horde is due now (HordesReleased is then incremented — one per call,
	 * a long frame never releases two at once).
	 */
	bool Advance(float DeltaSeconds, bool bCounting, const FHordeConfig& Config);
};

/** Pure horde rules (tests CodexTactics.Combat.Horde.*). */
namespace HordeRules
{
	/**
	 * Does the fight clock run now: a wave fight in full real time, not in the tactical pause, not in turn-based combat,
	 * not while dialogues / cutscenes hold the world AI (UWorldAIPauseSubsystem).
	 */
	CODEXTACTICS_API bool IsCountingTime(ECodexGamePhase Phase, ECodexCombatMode Mode, bool bWorldAIPaused);

	/** The distance lies in [MinDistanceCm, MaxDistanceCm]. */
	CODEXTACTICS_API bool IsInDistanceBand(float DistanceCm, const FHordeConfig& Config);

	/**
	 * The spawn point: a reachable candidate in the distance band, one out of the squad's sight first (when
	 * bPreferOutOfSight), the farthest-from-the-band-edges first among equals. INDEX_NONE when none qualifies.
	 */
	CODEXTACTICS_API int32 PickSpawnPoint(const TArray<FHordeSpawnCandidate>& Candidates, const FHordeConfig& Config);

	/** Enemies of horde number HordeIndex (0 = the first): Count + CountIncreasePerRepeat x HordeIndex, at least 1. */
	CODEXTACTICS_API int32 GetHordeSize(const FHordeConfig& Config, int32 HordeIndex);

	/** The archetypes of one horde of Total enemies by the composition weights (largest remainder, stable order). */
	CODEXTACTICS_API TArray<EEnemyArchetype> BuildComposition(const FHordeConfig& Config, int32 Total);

	/** Spawn offset of member Index of Count within the cluster radius (the first at the centre, then a sunflower). */
	CODEXTACTICS_API FVector ClusterOffset(int32 Index, int32 Count, float ClusterRadiusCm);

	/** Applies the keys present in Json over Config (meters -> cm); unknown archetypes in "composition" are skipped. */
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
	CODEXTACTICS_API FHordeConfig ResolveForLevel(const FHordeConfig& Defaults, bool bLevelHordeEnabled, const FString& LevelOverrideJson);
}
