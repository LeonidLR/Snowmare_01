#pragma once

#include "CoreMinimal.h"

class UGodotBalanceAsset;

/** Rage tuning (Godot Scripts/components/rage_component.gd exports + game_balance_config rage_* / <role>_rage_*). */
struct CODEXTACTICS_API FRageConfig
{
	int32 RequiredCrits = 2;
	/** Crits from the same enemy count within this window, s. */
	float CritMemoryWindow = 25.f;
	float HighHealthThreshold = 0.65f;
	int32 MinAmmo = 6;
	float BaseChance = 0.8f;
	/** Chance lost per minute of combat time. */
	float CombatDecayRate = 0.1f;
	float MinChance = 0.15f;
	float Duration = 8.5f;
	float ChaoticSwitchTime = 0.55f;
	float FireRateMultiplier = 0.45f;
	float DamageMultiplier = 1.3f;
	bool bInfiniteAmmo = true;
};

/** Pure rage rules (Godot rage_component.gd). */
namespace RageRules
{
	/**
	 * Godot: setup() applies the general rage_* keys, then main.gd re-applies the soldier's own <Prefix>rage_* keys
	 * (apply_soldier_config). Empty Prefix = general values only (a recruit spawned mid-mission).
	 */
	CODEXTACTICS_API FRageConfig ConfigFromBalance(const UGodotBalanceAsset* Balance, const FString& Prefix);

	/** Rage chance after CombatSeconds: base - minutes * decay, clamped to [min, 1]. */
	CODEXTACTICS_API float GetChance(const FRageConfig& Config, float CombatSeconds);

	/** Godot _check_rage_conditions_and_trigger: healthy enough, enough rounds in the clip, and the roll <= chance. */
	CODEXTACTICS_API bool CanEnterRage(const FRageConfig& Config, float HealthFraction, int32 Clip, float Roll, float Chance);
}
