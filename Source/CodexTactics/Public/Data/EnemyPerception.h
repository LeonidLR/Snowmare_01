#pragma once

#include "CoreMinimal.h"
#include "AI/PerceptionRules.h"
#include "AI/StealthLureRules.h"

class FJsonObject;

/**
 * Jev AI coach knobs (Scripts/Tools/jev_ai_coach.py --stealth; ai_tuning.json / -dpcvars=) on top of the data file:
 * Codex.Perception.SightRangeScale / FovScale / ProneVisibilityScale / HearingScale / SmellScale / TimeToDetectScale
 * (multipliers, 1 keeps the data) and Codex.Patrol.SearchSeconds / SearchRadius (-1 keeps the data) / SearchSpeedScale.
 */
struct CODEXTACTICS_API FPerceptionTuning
{
	float SightRangeScale = 1.f;
	float FovScale = 1.f;
	float ProneVisibilityScale = 1.f;
	float HearingScale = 1.f;
	float SmellScale = 1.f;
	float TimeToDetectScale = 1.f;
	/** Seconds / cm; < 0 keeps the data value. */
	float SearchSeconds = -1.f;
	float SearchRadiusCm = -1.f;
	float SearchSpeedScale = 1.f;
	/** Footstep hearing x this per wall in between (Codex.Perception.HearingOcclusion); < 0 keeps the data value. */
	float HearingOcclusionPerWall = -1.f;
};

/**
 * Enemy perception table (user request 2026-10-06, UE-only): Content/Data/AI/enemy_perception.json, read by the game
 * mode at StartPlay like squad_roe.json. Layout:
 *   { "search": { "duration_seconds": 60, "sweep_radius_m": 12, "speed_multiplier": 1.6, "perception_multiplier": 1.25,
 *                 "look_around_seconds": 2, "arrive_m": 1.5, "leg_timeout_seconds": 10 },
 *     "archetypes": { "FROST_HOUND": { "sight_range_m": 15, "sight_half_angle_deg": 70, "visibility_standing": 1,
 *       "visibility_crouching": 0.75, "visibility_prone": 0.4, "proximity_m": 3, "time_to_detect_seconds": 0.6,
 *       "suspicion_decay_per_second": 0.5, "hear_walk_m": 7, "hear_run_m": 14, "hear_crouch_walk_m": 3.5, "hear_crawl_m": 1.5,
 *       "hear_gunshot_m": 45, "hear_explosion_m": 60, "smell_radius_m": 6, "hearing_occlusion_per_wall": 0.5,
 *       "smell_occlusion_per_wall": 0.3, "smell_detect_m": 2, "sniff_seconds": 2.5, "scent_lost_seconds": 1,
 *       "scent_follow_max_seconds": 20, "scent_follow_speed_fraction": 0.8, "scent_search_seconds": 20, "scent_sweep_radius_m": 3 },
 *       "MARKSMAN": {...}, ... } }   (hound scent, game designer 2026-10-18: ScentRules; all scent keys optional)
 *     "lure": { "whistle_radius_m": 12, "throw_radius_m": 8, "throw_range_m": 15, "whistle_cooldown_seconds": 10,
 *               "throw_cooldown_seconds": 6, "investigate_seconds": 60, "investigate_sweep_radius_m": 4,
 *               "approach_speed_fraction": 0.75 } }   (stealth lure, user decisions 2026-10-18; FStealthLureParams)
 * (footstep radii lowered 2026-10-06 after the user's playtest: run ~10-12 m, walk ~5-6 m, hounds a bit more;
 * hearing_occlusion_per_wall optional, default 0.5 — each wall between the ear and the feet halves the footstep radius).
 * Archetype keys as in the level JSON (LevelJsonRules::ParseEnemyType). A missing key keeps the built-in default
 * (PerceptionRules::GetArchetypeDefaults); smell is ignored for every archetype but the frost hound. Kept out of
 * GameBalanceConfig for the same reason as the ROE: that header is generated from Godot and these values are UE-only.
 */
namespace EnemyPerception
{
	/** Content/Data/AI/enemy_perception.json. */
	CODEXTACTICS_API FString GetDefaultPath();

	/** Perception of Archetype in force (built-in defaults until ApplyFile / Set; the Codex.Perception.* knobs applied). */
	CODEXTACTICS_API FEnemyPerceptionParams Get(EEnemyArchetype Archetype);

	/** The Codex.Perception.* / Codex.Patrol.* console variables now. */
	CODEXTACTICS_API FPerceptionTuning GetTuning();

	/** Params with the coach knobs applied (pure: scales, FOV clamped to 180 deg). */
	CODEXTACTICS_API FEnemyPerceptionParams ApplyTuning(const FEnemyPerceptionParams& Params, const FPerceptionTuning& Tuning);
	CODEXTACTICS_API FPatrolSearchParams ApplyTuning(const FPatrolSearchParams& Search, const FPerceptionTuning& Tuning);
	CODEXTACTICS_API void Set(EEnemyArchetype Archetype, const FEnemyPerceptionParams& Params);

	/** Patrol search parameters in force (Codex.Patrol.* knobs applied; the level JSON search time is resolved by
	 * ULevelEncounterSubsystem, a Codex.Patrol.SearchSeconds >= 0 wins over it). */
	CODEXTACTICS_API FPatrolSearchParams GetSearch();
	CODEXTACTICS_API void SetSearch(const FPatrolSearchParams& Search);

	/**
	 * Stealth lure tuning in force (the "lure" section; Codex.Perception.HearingScale scales both radii like every hearing radius,
	 * Codex.Lure.InvestigateSeconds >= 0 overrides the investigation time).
	 */
	CODEXTACTICS_API FStealthLureParams GetLure();
	CODEXTACTICS_API void SetLure(const FStealthLureParams& Lure);
	CODEXTACTICS_API FStealthLureParams LureFromJson(const FJsonObject& Json, const FStealthLureParams& Base = FStealthLureParams());

	/** Back to the built-in defaults (tests). */
	CODEXTACTICS_API void ResetToDefaults();

	/** One archetype's entry over Base (meters / seconds / degrees as in the file). */
	CODEXTACTICS_API FEnemyPerceptionParams ParamsFromJson(const FJsonObject& Json, const FEnemyPerceptionParams& Base);
	CODEXTACTICS_API FPatrolSearchParams SearchFromJson(const FJsonObject& Json, const FPatrolSearchParams& Base = FPatrolSearchParams());

	/** Applies a whole file text; false when it is not valid JSON (nothing changed). */
	CODEXTACTICS_API bool ApplyJson(const FString& Text);

	/** ApplyJson on the file; false (defaults kept) when it is missing or unreadable. */
	CODEXTACTICS_API bool ApplyFile(const FString& Path);
}
