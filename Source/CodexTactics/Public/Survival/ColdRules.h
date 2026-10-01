#pragma once

#include "CoreMinimal.h"
#include "Characters/OperativeMovementRules.h"
#include "ColdRules.generated.h"

/** Cold severity tiers. Godot player.gd `cold_status`: Норма / Озноб / Замерзание / Переохлаждение / Обморожение. */
UENUM(BlueprintType)
enum class EColdTier : uint8
{
	/** cold < 40 % */
	Normal,
	/** 40..70 % */
	Chills,
	/** 70..90 % */
	Freezing,
	/** 90..100 % */
	Hypothermia,
	/** 100 %: frostbite, the operative collapses prone */
	Frostbite
};

/**
 * Cold survival tuning. Defaults = values the Godot build runs with:
 * balance.tres cold_accumulation_rate 1.0, misfire 60 % / 0.30 / 0.45 s, weapon freeze 90 %, aim penalty 0.30;
 * game_balance_config.gd cold_warmth_recovery_rate 8.0, stance multipliers 1 / 0.85 / 0.6;
 * player.gd FREEZE_DAMAGE_PER_SEC 0.66, HEALTH_WARMTH_REGEN_RATE 4, wind x2 at y >= 2.4 m, fortitude cut 0.008 (max 0.4).
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FColdConfig
{
	GENERATED_BODY()

	/** Cold gain in the open, %/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold")
	float AccumulationRate = 1.f;

	/** Cold loss near heat, in a closed room or during preparation, %/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold")
	float WarmthRecoveryRate = 8.f;

	/**
	 * Cold loss while sprinting in the cold, %/s (the run warms the body up; fortitude speeds it up like the warmth
	 * recovery). Not in Godot — user decision 2026-10-01. Sprinting itself stops at MaxColdToSprint (60 %) or with a heavy
	 * wound, so the run only helps before the operative is chilled.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold", meta = (ClampMin = "0"))
	float SprintWarmupRate = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold")
	float StanceMultiplierStanding = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold")
	float StanceMultiplierCrouching = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold")
	float StanceMultiplierProne = 0.6f;

	/** Arctic wind multiplier on elevated ground. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold")
	float ElevatedWindMultiplier = 2.f;

	/**
	 * Feet height (world Z) from which the ground counts as elevated, cm. Godot checks the body origin
	 * y >= 2.4 m with the origin 1 m above the floor, i.e. feet 1.4 m above the base floor.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold")
	float ElevatedHeight = 140.f;

	/** Cold gain cut per fortitude point (Godot 0.008, capped by MaxFortitudeCut). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold")
	float FortitudeCutPerPoint = 0.008f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold")
	float MaxFortitudeCut = 0.4f;

	/** Warm-up boost per fortitude point (Godot 1 + fortitude * 0.01). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold")
	float FortitudeWarmupPerPoint = 0.01f;

	/** Health loss at 100 % cold, HP/s (halved in combat). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold")
	float FreezeDamagePerSecond = 0.66f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold")
	float CombatFreezeDamageMultiplier = 0.5f;

	/** Health regeneration while warm and cold <= RegenMaxCold, HP/s (Godot boost 1 + fortitude * 0.015). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold")
	float WarmHealthRegenPerSecond = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold")
	float RegenMaxCold = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	float MisfireThreshold = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	float MisfireMaxChance = 0.3f;

	/** Pause after a misfire before the next shot, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	float MisfireDelay = 0.45f;

	/** Weapon freezes solid from this cold level unless near a heat source. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	float WeaponFreezeThreshold = 90.f;

	/** A frozen weapon thaws this far below the freeze threshold (hysteresis). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	float WeaponThawHysteresis = 5.f;

	/** Maximum hit-chance penalty from shivering hands (from 50 % to 100 % cold). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	float AimPenaltyMax = 0.3f;
};

/** Environment of one cold step. */
struct CODEXTACTICS_API FColdEnvironment
{
	/** Near an active heat source (burning barrel, running generator). */
	bool bWarm = false;
	/** Preparation phase: no cold accumulates (Godot is_prep). */
	bool bPreparation = false;
	/** Zone multiplier: 0 closed bunker, 0.5 shelter, 1 open, 2.5 blizzard. */
	float ZoneMultiplier = 1.f;
	bool bElevated = false;
	/** Actually running in a sprint (not just the sprint order while standing). */
	bool bSprinting = false;
};

/** Pure cold formulas. Godot reference: Scenes/movements/player.gd `_process_cold_system`, `_shoot_at_target`. */
namespace ColdRules
{
	CODEXTACTICS_API float GetStanceMultiplier(const FColdConfig& Config, EOperativeStance Stance);

	/** Cold level after DeltaSeconds, clamped to [0, 100]. */
	CODEXTACTICS_API float StepCold(const FColdConfig& Config, float Cold, float DeltaSeconds, const FColdEnvironment& Environment,
		EOperativeStance Stance, float Fortitude);

	CODEXTACTICS_API EColdTier GetTier(float Cold);

	/** Movement speed multiplier of a tier (1 / 0.7 / 0.45 / 0.25). */
	CODEXTACTICS_API float GetSpeedMultiplier(EColdTier Tier);

	/** Action points per turn of a tier (5 / 4 / 3 / 2). */
	CODEXTACTICS_API int32 GetMaxActionPoints(EColdTier Tier);

	/** Misfire probability for a shot (0 near heat or below the threshold). */
	CODEXTACTICS_API float GetMisfireChance(const FColdConfig& Config, float Cold, bool bNearHeat);

	/** Hit-chance penalty from cold (0 up to 50 %). */
	CODEXTACTICS_API float GetAimPenalty(const FColdConfig& Config, float Cold);

	/** New frozen-weapon state with hysteresis; heat always thaws. */
	CODEXTACTICS_API bool UpdateWeaponFrozen(const FColdConfig& Config, bool bFrozen, float Cold, bool bNearHeat);
}
