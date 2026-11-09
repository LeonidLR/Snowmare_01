#pragma once

#include "CoreMinimal.h"
#include "Combat/CombatPressureRules.h"
#include "Combat/FirefightRules.h"
#include "Data/CombatTypes.h"
#include "Data/WaveConfigTypes.h"
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

	/**
	 * Game designer play-test 2026-11-09 ("the horde must appear far away - about 300 m - and in a different place every time"): the
	 * FIREFIGHT horde's spawn distance from the squad's centre, "firefight_spawn_distance_m" (300) +- "firefight_spawn_spread_m" (40) = the
	 * band 260-340 m. On a map with no reachable out-of-sight point that far the farthest ones are taken (HordeRules::PickFarSpawnPoint).
	 * The dev command CodexTactics.Horde.Release keeps the near band MinDistanceCm..MaxDistanceCm.
	 */
	float FirefightSpawnDistanceCm = 30000.f;
	float FirefightSpawnSpreadCm = 4000.f;
	/** "firefight_bearing_change_deg": the next firefight horde comes from a bearing at least this far from the previous one's (90). */
	float FirefightBearingChangeDeg = 90.f;
	/**
	 * "firefight_bearing_relax_share": the new-bearing rule gives way when the points off the previous bearing reach less than this share of
	 * the farthest out-of-sight distance. 0 (default, the designer's "a different place every time"): only when there is no point off it at
	 * all - on the Depot's southern start the second horde then comes from ~80 m on a new side, not from 166 m on the old one; 0.5 would
	 * prefer the distance there.
	 */
	float FirefightBearingRelaxShare = 0.f;
	/** "firefight_spawn_bearings": directions sampled round the squad for the far spawn (16; 4..64). */
	int32 FirefightSpawnBearings = 16;
	/**
	 * "firefight_fallback_share": no point in the band (a small map) - a random one among the points at least this share of the farthest
	 * reachable out-of-sight distance (0.85; 1 = exactly the farthest).
	 */
	float FirefightFallbackShare = 0.85f;
	/**
	 * "waves_feed_horde" (game designer 2026-11-09): on a level that does not play its "waves" as scripted waves (an ambush-start level such
	 * as the Depot) the waves feed the firefight hordes in order - horde 1 = wave 1, 2 = wave 2, ..., then the last wave repeats (inactive
	 * waves are skipped by the level parser); false = always the "count" / "composition" of this file / the level's "horde" block.
	 */
	bool bWavesFeedHorde = true;
};

/** One member of a horde plan: its type (+ variant) and the wave spawn entry it came from (custom_stats; INDEX_NONE = none). */
struct CODEXTACTICS_API FHordeMember
{
	FPressureEnemyType Enemy;
	int32 EntryIndex = INDEX_NONE;
};

/** What one horde brings (HordeRules::PlanFromWave / PlanFromConfig). */
struct CODEXTACTICS_API FHordePlan
{
	/** The members in spawn order. */
	TArray<FHordeMember> Members;
	/** The wave's spawn entries (their custom_stats apply to their members); empty for the "horde" block / horde.json. */
	TArray<FEnemySpawnEntry> Entries;
	/** The most members alive at once (the wave's max_simultaneous_enemies; the rest come as the living fall); 0 = all at once. */
	int32 MaxAlive = 0;
	/** The wave's hp / damage / speed multipliers apply (always for a wave-fed horde). */
	bool bHasModifiers = false;
	FWaveModifiers Modifiers;
	/** The level wave (1-based among the active waves) this horde comes from; 0 = the "horde" block / horde.json. */
	int32 SourceWave = 0;
	/** Spitter entries mapped to the Military Zombie (the Spitter is refused in the horde, game designer 2026-11-09). */
	int32 SpitterSwaps = 0;

	/** "4 FROSTBITTEN, 30 FROST_HOUND" (logs, smokes). */
	FString Describe() const;
};

/** A far (firefight) spawn candidate measured by the subsystem. */
struct CODEXTACTICS_API FHordeFarCandidate
{
	FVector Location = FVector::ZeroVector;
	/** 2D distance to the squad's centre, cm. */
	float DistanceCm = 0.f;
	/** Bearing from the squad's centre, degrees (HordeRules::BearingDeg). */
	float BearingDeg = 0.f;
	/** On the navmesh with a full path to the squad. */
	bool bReachable = false;
	/** Some living operative sees the point (a clear line within his sight range). */
	bool bVisibleToSquad = false;
};

/** How the far spawn point was chosen. */
enum class EHordeFarPick : uint8
{
	/** No reachable candidate at all. */
	None,
	/** A point in the 260-340 m band. */
	InBand,
	/** The map has none that far: among the farthest reachable ones (logged). */
	Farthest,
};

/** HordeRules::PickFarSpawnPoint's answer. */
struct CODEXTACTICS_API FHordeFarPick
{
	int32 Index = INDEX_NONE;
	EHordeFarPick Kind = EHordeFarPick::None;
	/** No usable point off the previous bearing: the bearing rule was dropped. */
	bool bBearingRelaxed = false;
	/** No out-of-sight point: a visible one was taken. */
	bool bVisible = false;
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

	// ---- Waves feed the horde (game designer 2026-11-09) -----------------------------------------------------------------------------

	/**
	 * The level's waves feed the firefight horde: the config allows it (bWavesFeedHorde), the level has (active) waves, its fight starts by
	 * ambush (combat_start "ambush", or "auto" on a map with patrols / placed enemies: the "Start battle" flow never plays them) and no
	 * scripted level wave has been played on this level (a level that runs its waves as scripted fights keeps them for those - its firefight
	 * horde uses the "horde" block / horde.json).
	 */
	CODEXTACTICS_API bool ShouldWavesFeedHorde(const FHordeConfig& Config, int32 NumLevelWaves, bool bAmbushCombatStart, bool bScriptedWavesPlayed);

	/** The level wave (0-based among the active waves) of the level's HordeOrdinal-th firefight horde (1-based): min(N, waves) - 1, so the last wave repeats; INDEX_NONE without waves. */
	CODEXTACTICS_API int32 WaveForHorde(int32 HordeOrdinal, int32 NumLevelWaves);

	/**
	 * The horde of one level wave: every spawn entry's count in entry order (custom_stats kept), the SPITTER mapped to the MILITARY_ZOMBIE
	 * (the Spitter is refused in the horde - SpitterSwaps counts them), other refused types (Base) skipped; MaxAlive = max_simultaneous_enemies
	 * (>= 1); the wave's modifiers. spawn_delay_sec / initial_delay_sec are not used (like the scripted waves, user decision 2026-09-29).
	 */
	CODEXTACTICS_API FHordePlan PlanFromWave(const FWaveDefinition& Wave, int32 WaveNumber);

	/** The horde of the "horde" block / horde.json: Count members by the composition weights, all at once (MaxAlive 0), no modifiers. */
	CODEXTACTICS_API FHordePlan PlanFromConfig(const FHordeConfig& Config);

	/** Members of Plan that may still come when Alive of them live and Spawned of them came: min(cap - alive, left); no cap = all left. */
	CODEXTACTICS_API int32 MembersToSpawnNow(const FHordePlan& Plan, int32 Spawned, int32 Alive);

	// ---- The far (firefight) spawn point (game designer 2026-11-09: ~300 m, a different place every time) -------------------------------

	/** Bearing of To from From in the XY plane, degrees in [0, 360). */
	CODEXTACTICS_API float BearingDeg(const FVector& From, const FVector& To);

	/** The bearings differ by at least MinChangeDeg (the shorter way round). */
	CODEXTACTICS_API bool IsBearingFarEnough(float BearingA, float BearingB, float MinChangeDeg);

	/** The firefight band in cm: distance +- spread (min >= MinDistanceCm). */
	CODEXTACTICS_API float FarBandMinCm(const FHordeConfig& Config);
	CODEXTACTICS_API float FarBandMaxCm(const FHordeConfig& Config);

	/**
	 * Radii probed along each bearing, farthest first: the band's max, the distance, the band's min, then x0.8 steps down to MinDistanceCm
	 * (the first reachable one along a bearing is that bearing's candidate - on a small map the farthest it has).
	 */
	CODEXTACTICS_API TArray<float> FarProbeRadii(const FHordeConfig& Config);

	/**
	 * The firefight horde's spawn point among Candidates: only reachable ones at least MinDistanceCm away. Out of sight first
	 * (bPreferOutOfSight), then a bearing at least FirefightBearingChangeDeg off the previous horde's (when there was one) - the bearing rule
	 * is dropped when it leaves no point (or, with FirefightBearingRelaxShare > 0, only points under that share as far as without it). Within that set: a random point in the band (InBand), else a
	 * random one among those >= FirefightFallbackShare x the farthest distance (Farthest). Random picks use Random.
	 */
	CODEXTACTICS_API FHordeFarPick PickFarSpawnPoint(const TArray<FHordeFarCandidate>& Candidates, const FHordeConfig& Config, bool bHasPreviousBearing,
		float PreviousBearingDeg, FRandomStream& Random);
}
