#pragma once

#include "CoreMinimal.h"

class UGodotBalanceAsset;

/**
 * Panic tuning (Godot Scripts/components/panic_component.gd exports + game_balance_config panic_* / <role>_*). The values
 * are tuned in DA_GameBalanceConfig (user decision 2026-10-01: at most one operative panics at a time, the core squad
 * resists much harder than a recruit).
 */
struct CODEXTACTICS_API FPanicConfig
{
	/** Health fraction below which wounds feed the stress. */
	float HpThreshold = 0.35f;
	/** Cold fraction (0..1) above which the cold feeds the stress. */
	float ColdThreshold = 0.7f;
	int32 LowAmmoThreshold = 5;
	/** Enemies closer than this feed the stress, m. */
	float MonsterThreatDistance = 8.f;
	/** Stress points calmed per second without threats. */
	float StressRecoveryRate = 5.f;
	/** Personal multiplier of every stress gain (<role>_stress_gain_multiplier). */
	float StressGainMultiplier = 1.f;
	/** Personal multiplier of the calm-down rate (<role>_panic_recovery_mult). */
	float StressRecoveryMultiplier = 1.f;
	/** Operatives panicking at the same time across the squad. */
	int32 MaxPanickedMembers = 2;
	/** Flight speed, m/s (0 = walk speed x SpeedMultiplier). */
	float FleeSpeed = 6.f;
	float SpeedMultiplier = 1.25f;
	/** Farthest the operative runs from the leader / squad centre, m. */
	float MaxFleeRadius = 15.f;
	/** First phase: run this far (or FleeMaxTime) before cowering, m / s. */
	float FleeDistance = 6.5f;
	float FleeMaxTime = 2.2f;
	float MinDuration = 6.f;
	float MaxDuration = 12.f;
	/** The panic ends early once the nearest enemy is farther than this, m. */
	float RecoverDistanceFromEnemies = 15.f;
	/** Stress calmed per second next to an active heat source. */
	float HeatSourceCalmRate = 25.f;
	/** Seconds of uninterrupted warmth that break a panic. */
	float HeatSourcePanicBreakTime = 2.5f;
	/** Weight of the pull towards the nearest heat source while fleeing. */
	float FleeTowardsHeatBias = 0.5f;
};

/** Pure panic rules (Godot panic_component.gd). */
namespace PanicRules
{
	/**
	 * Godot apply_balance_config + apply_soldier_config: the general panic_* keys, then the soldier's own <Prefix>_* keys
	 * (commander / engineer / medic / susanin). Empty Prefix = general values only.
	 */
	CODEXTACTICS_API FPanicConfig ConfigFromBalance(const UGodotBalanceAsset* Balance, const FString& Prefix);

	/**
	 * Godot _process_stress: stress gained per second from wounds (20 x severity below HpThreshold), cold (25 x severity
	 * above ColdThreshold), ammo (10 x shortage at or below LowAmmoThreshold) and enemies within MonsterThreatDistance
	 * (15 x closeness), cut by fortitude (2.5 % a point, at most 65 %) and scaled by StressGainMultiplier.
	 * NearestEnemyDistance < 0 = no enemy.
	 */
	CODEXTACTICS_API float GetStressGrowth(const FPanicConfig& Config, float HealthFraction, float ColdFraction, int32 Ammo,
		float NearestEnemyDistance, float Fortitude);

	/** One stress step: grow by Growth when it is above 0.5 / s, otherwise calm down; clamped to [0, 100]. */
	CODEXTACTICS_API float StepStress(const FPanicConfig& Config, float Stress, float Growth, float DeltaSeconds);

	/** Godot on_damage_taken: damage x 1.5 cut by fortitude (2 % a point, at most 60 %), within [6, 35] x StressGainMultiplier. */
	CODEXTACTICS_API float GetDamageStress(const FPanicConfig& Config, float Damage, float Fortitude);

	/** Godot on_low_ammo: 12 cut by fortitude (2 % a point, at most 50 %), x StressGainMultiplier. */
	CODEXTACTICS_API float GetLowAmmoStress(const FPanicConfig& Config, float Fortitude);

	/** Result of reaching 100 stress (Godot _attempt_trigger_panic). */
	enum class ETriggerVerdict : uint8
	{
		Panic,
		/** The squad's panic limit is reached: the operative holds on at 95 stress. */
		HoldOnLimit,
		/** The leader holds on at 90 while subordinates panic. */
		HoldOnLeader,
	};

	CODEXTACTICS_API ETriggerVerdict GetTriggerVerdict(const FPanicConfig& Config, int32 PanickedNow, bool bIsLeader);
}
