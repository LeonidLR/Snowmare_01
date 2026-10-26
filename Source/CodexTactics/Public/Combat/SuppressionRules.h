#pragma once

#include "CoreMinimal.h"

/**
 * The fire a round came from: picks its suppression weight (suppression.json "weight.*"). UE-only (game designer 2026-10-26).
 */
enum class ESuppressionWeapon : uint8
{
	/** A rifle / pistol / shotgun round fired one at a time (the operatives' normal fire). */
	SemiAuto,
	/** An assault rifle on full-auto (the SUPPRESS order). */
	FullAuto,
	/** A turret / vehicle machine gun. */
	MachineGun,
	/** A marksman / sniper round (few, heavy). */
	Sniper,
	/** A spitter's / cryo drone's acid. */
	Acid,
	/** The APC's autocannon shell (explosive: a wider near-miss radius). */
	Cannon
};

/** How one round met one unit. */
enum class ESuppressionImpact : uint8
{
	None,
	/** It passed within the near-miss radius of him. */
	NearMiss,
	/** It struck the cover he hides behind (within the cover-strike radius of the impact). */
	CoverStrike,
	/** It hit him. */
	Hit
};

/** A unit's suppression tier (with hysteresis between the enter / exit thresholds). */
enum class ESuppressionTier : uint8
{
	None,
	/** Enemy: does not fire, hugs his cover. Operative: blind fire with a large spread, no peeking. */
	Suppressed,
	/** Very intense: nobody fires at all (both sides). */
	Pinned
};

/** Why a SUPPRESS order ended (none = it goes on). */
enum class ESuppressOrderStop : uint8
{
	None,
	/** The ordered enemy died. */
	TargetDown,
	/** Magazine + reserve at or below the order's stop threshold. */
	AmmoLow,
	/** A weapon without ammo (the knife). */
	NoWeapon
};

/**
 * Every suppression tunable. Content/Data/Combat/suppression.json is the master copy (flat "params" object, the keys are named on
 * each field); the defaults here mirror it (test CodexTactics.Combat.Suppression.TuningFileMirrorsDefaults). docs/design/SUPPRESSION.md.
 */
struct CODEXTACTICS_API FSuppressionConfig
{
	// --- Level and tiers ---
	/** "tier.suppressed_enter": level at which a unit becomes Suppressed. */
	float SuppressedEnter = 0.5f;
	/** "tier.suppressed_exit": a Suppressed unit is free again below this. */
	float SuppressedExit = 0.4f;
	/** "tier.pinned_enter": level at which a unit becomes Pinned. */
	float PinnedEnter = 0.85f;
	/** "tier.pinned_exit": a Pinned unit drops back to Suppressed below this. */
	float PinnedExit = 0.7f;
	/** "decay.per_second": the level always bleeds off at this rate (one rifleman's normal fire stays below it). */
	float DecayPerSecond = 0.1f;
	/** "decay.quiet_seconds": no round near him for this long = quiet. */
	float QuietSeconds = 1.5f;
	/** "decay.quiet_per_second": extra decay while quiet. */
	float QuietDecayPerSecond = 0.3f;

	// --- What a round does ---
	/** "round.near_miss_radius_cm": a round passing this close to a unit (his capsule centre) counts as a near miss. */
	float NearMissRadiusCm = 200.f;
	/** "round.near_miss_edge_factor": a near miss at the radius edge counts this share of one at point blank. */
	float NearMissEdgeFactor = 0.5f;
	/** "round.cover_strike_radius_cm": a round striking cover this close to a unit counts as a strike on his cover. */
	float CoverStrikeRadiusCm = 250.f;
	/** "round.cover_strike_factor": a strike on his cover x the weapon weight. */
	float CoverStrikeFactor = 0.9f;
	/** "round.hit_factor": a hit x the weapon weight. */
	float HitFactor = 1.5f;
	/** "round.explosive_radius_cm": the cannon shell's near-miss radius. */
	float ExplosiveRadiusCm = 450.f;

	// --- Weight of one round by weapon ---
	/** "weight.semi_auto" */
	float WeightSemiAuto = 0.04f;
	/** "weight.full_auto" */
	float WeightFullAuto = 0.07f;
	/** "weight.machine_gun" */
	float WeightMachineGun = 0.09f;
	/** "weight.sniper" */
	float WeightSniper = 0.3f;
	/** "weight.acid" */
	float WeightAcid = 0.1f;
	/** "weight.cannon" */
	float WeightCannon = 0.45f;
	/** "weight.deliberate_multiplier": an enemy round fired under a deliberate "suppress target" intent (the enemy-AI hook). */
	float DeliberateMultiplier = 1.5f;

	// --- Effects ---
	/** "effect.suppressed_hit_multiplier": a suppressed operative's hit chance (blind fire). */
	float SuppressedHitMultiplier = 0.35f;
	/** "effect.suppressed_spread_multiplier": a suppressed operative's spread cone / miss offset. */
	float SuppressedSpreadMultiplier = 3.f;

	// --- The SUPPRESS order ---
	/** "order.burst_rounds": rounds per full-auto burst. */
	float OrderBurstRounds = 6.f;
	/** "order.burst_pause_seconds": pause between two full-auto bursts. */
	float OrderBurstPauseSeconds = 0.6f;
	/** "order.hit_multiplier": a suppressing round that crosses an enemy hits by the weapon's chance x this. */
	float OrderHitMultiplier = 0.25f;
	/** "order.spread_standing_deg" / "order.spread_crouching_deg" / "order.spread_prone_deg": half-angle spray cone by stance. */
	float OrderSpreadStandingDeg = 3.5f;
	float OrderSpreadCrouchingDeg = 2.5f;
	float OrderSpreadProneDeg = 1.8f;
	/** "order.stop_at_rounds": the order ends once magazine + reserve are at or below this (he keeps the rest). */
	float OrderStopAtRounds = 10.f;
	/** "order.timeout_seconds": no round could leave for this long (turning, no line out of the cover): the order is dropped. */
	float OrderTimeoutSeconds = 6.f;
	/** "order.aim_height_cm": aim this far above an enemy's capsule centre (his chest / the top of his cover). */
	float OrderAimHeightCm = 30.f;
	/** "order.spot_scatter_cm": fire at a spot walks the rounds over a disc of this radius round it. */
	float OrderSpotScatterCm = 120.f;
};

/** One unit's suppression. */
struct CODEXTACTICS_API FSuppressionState
{
	/** 0..1. */
	float Level = 0.f;
	/** Seconds since the last round near him. */
	float QuietTime = 0.f;
	ESuppressionTier Tier = ESuppressionTier::None;
};

/** What a suppressed operative's fire looks like (SuppressionRules::OperativeFire). */
struct CODEXTACTICS_API FSuppressionFireEffect
{
	bool bMayFire = true;
	/** Blind fire: no aimed shot, no peek, hit chance x HitMultiplier. */
	bool bBlind = false;
	float HitMultiplier = 1.f;
	float SpreadMultiplier = 1.f;
};

/**
 * Pure rules of suppressive fire (game designer decisions 2026-10-26, UE-only - no Godot reference; spec docs/design/SUPPRESSION.md).
 * Symmetric: every round fired near a unit of the other side (near miss, a strike on his cover, a hit) adds pressure by its weapon; the
 * level decays (always a little, faster once quiet). Tiers: Suppressed (an enemy stops firing and hugs his cover; an operative fires
 * blind with a large spread and does not peek) and Pinned (no fire at all, both sides). Turn-based combat has none. Unit tests
 * CodexTactics.Combat.Suppression.*; the runtime is USuppressionSubsystem.
 */
namespace SuppressionRules
{
	CODEXTACTICS_API float GetWeaponWeight(const FSuppressionConfig& Config, ESuppressionWeapon Weapon);
	/** The near-miss radius of the weapon's rounds (the cannon: the explosive radius). */
	CODEXTACTICS_API float GetNearMissRadius(const FSuppressionConfig& Config, ESuppressionWeapon Weapon);

	/**
	 * How the round Start -> End met a unit at UnitLocation (its capsule centre): Hit when bHitThisUnit; else a strike on his cover when
	 * bStruckCover and the impact (End) lies within CoverStrikeRadiusCm of him, or a near miss when the segment passes within the weapon's
	 * near-miss radius (whichever is stronger at its distance); else None. OutDistanceCm = the distance that rated it.
	 */
	CODEXTACTICS_API ESuppressionImpact ClassifyImpact(const FSuppressionConfig& Config, ESuppressionWeapon Weapon, const FVector& Start,
		const FVector& End, bool bStruckCover, bool bHitThisUnit, const FVector& UnitLocation, float& OutDistanceCm);

	/**
	 * Pressure of one impact: weight x (hit: HitFactor; cover strike: CoverStrikeFactor; near miss: 1 at point blank falling linearly to
	 * NearMissEdgeFactor at the radius) x WeightScale (the weapon's own suppression_weight, a deliberate intent). None / outside: 0.
	 */
	CODEXTACTICS_API float ImpactPressure(const FSuppressionConfig& Config, ESuppressionWeapon Weapon, ESuppressionImpact Impact, float DistanceCm,
		float WeightScale = 1.f);

	/** Adds pressure (level clamped to 1), resets the quiet time, re-resolves the tier. */
	CODEXTACTICS_API void AddPressure(const FSuppressionConfig& Config, FSuppressionState& State, float Amount);

	/** Time passes: quiet time grows, the level decays (DecayPerSecond, + QuietDecayPerSecond once quiet), the tier re-resolves. */
	CODEXTACTICS_API void Step(const FSuppressionConfig& Config, FSuppressionState& State, float DeltaSeconds);

	/** The tier of Level coming from Previous (enter / exit thresholds: hysteresis). */
	CODEXTACTICS_API ESuppressionTier ResolveTier(const FSuppressionConfig& Config, float Level, ESuppressionTier Previous);

	/** An enemy fires only while not suppressed (decision 2: he hugs his cover; decision 3: pinned = no fire). */
	CODEXTACTICS_API bool EnemyMayFire(ESuppressionTier Tier);

	/** An operative's fire under the tier: free; Suppressed = blind with the spread / hit multipliers; Pinned = no fire. */
	CODEXTACTICS_API FSuppressionFireEffect OperativeFire(const FSuppressionConfig& Config, ESuppressionTier Tier);

	/** He may expose himself (lean / peek / pop up / step off the wall) only when not suppressed. */
	CODEXTACTICS_API bool MayExpose(ESuppressionTier Tier);

	/** Full-auto when the weapon has a full-auto interval (> 0). */
	CODEXTACTICS_API bool IsFullAuto(float FullAutoIntervalSeconds);

	/**
	 * Delay before the next round of a SUPPRESS order: full-auto = the full-auto interval inside a burst of OrderBurstRounds, + the burst
	 * pause after its last round (InOutBurstCount counts the burst, reset after it); otherwise the weapon's own cadence (FireRate).
	 */
	CODEXTACTICS_API float NextOrderRoundDelay(const FSuppressionConfig& Config, float WeaponFireRate, float FullAutoIntervalSeconds,
		int32& InOutBurstCount);

	/** Spray cone half-angle of the order by stance index (0 standing, 1 crouching, 2 prone), degrees. */
	CODEXTACTICS_API float GetOrderSpreadDeg(const FSuppressionConfig& Config, int32 StanceIndex);

	/** Whether a SUPPRESS order goes on: the target died, ammo low (magazine + reserve <= OrderStopAtRounds), no ammo weapon. */
	CODEXTACTICS_API ESuppressOrderStop CheckOrder(const FSuppressionConfig& Config, bool bHasTarget, bool bTargetAlive, bool bUsesAmmo,
		int32 Clip, int32 Reserve);

	/** Ammo a SUPPRESS order can spend before it stops (magazine + reserve - OrderStopAtRounds, never below 0). */
	CODEXTACTICS_API int32 OrderAmmoBudget(const FSuppressionConfig& Config, int32 Clip, int32 Reserve);

	CODEXTACTICS_API const TCHAR* TierName(ESuppressionTier Tier);
	/** The HUD badge over a unit ("SUPPRESSED" / "PINNED"; empty for None). */
	CODEXTACTICS_API const TCHAR* TierBadge(ESuppressionTier Tier);
	CODEXTACTICS_API const TCHAR* StopName(ESuppressOrderStop Stop);

	// --- Tuning file ---

	/** Content/Data/Combat/suppression.json. */
	CODEXTACTICS_API FString GetDefaultPath();
	/** Every key of the "params" object, in the struct's order. */
	CODEXTACTICS_API TArray<FString> KnownKeys();
	/** The struct's value of Key; false when unknown. */
	CODEXTACTICS_API bool GetValue(const FSuppressionConfig& Config, const FString& Key, float& OutValue);
	/** Applies the "params" object of a suppression.json text over InOut; unknown keys go to OutUnknown. False: not JSON / no params. */
	CODEXTACTICS_API bool ApplyJson(const FString& JsonText, FSuppressionConfig& InOut, TArray<FString>* OutUnknown = nullptr);
	/** The values in force (the file on the first call; the defaults when it is missing). */
	CODEXTACTICS_API const FSuppressionConfig& GetConfig();
	CODEXTACTICS_API void SetConfig(const FSuppressionConfig& Config);
	/** Re-reads the file into GetConfig (game start); false (values kept) when missing / unreadable. */
	CODEXTACTICS_API bool ReloadConfig();
}
