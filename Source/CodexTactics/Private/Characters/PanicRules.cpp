#include "Characters/PanicRules.h"

#include "Data/GodotBalanceAsset.h"

namespace PanicRules
{
	FPanicConfig ConfigFromBalance(const UGodotBalanceAsset* Balance, const FString& Prefix)
	{
		FPanicConfig Config;
		if (!Balance)
		{
			return Config;
		}
		auto Number = [Balance](const TCHAR* Key, float Fallback) { return Balance->GetNumber(FName(Key), Fallback); };
		// Godot apply_balance_config: the general panic_* keys.
		Config.HpThreshold = Number(TEXT("panic_hp_threshold"), Config.HpThreshold);
		Config.ColdThreshold = Number(TEXT("panic_cold_threshold"), Config.ColdThreshold);
		Config.LowAmmoThreshold = FMath::RoundToInt(Number(TEXT("panic_low_ammo_threshold"), static_cast<float>(Config.LowAmmoThreshold)));
		Config.MonsterThreatDistance = Number(TEXT("panic_monster_threat_distance"), Config.MonsterThreatDistance);
		Config.StressRecoveryRate = Number(TEXT("panic_stress_recovery_rate"), Config.StressRecoveryRate);
		Config.MaxPanickedMembers = FMath::RoundToInt(Number(TEXT("panic_max_panicked_members"), static_cast<float>(Config.MaxPanickedMembers)));
		Config.SpeedMultiplier = Number(TEXT("panic_speed_multiplier"), Config.SpeedMultiplier);
		Config.FleeSpeed = Number(TEXT("panic_flee_speed"), Config.FleeSpeed);
		Config.MaxFleeRadius = Number(TEXT("panic_max_flee_radius"), Config.MaxFleeRadius);
		Config.FleeDistance = Number(TEXT("panic_flee_distance"), Config.FleeDistance);
		Config.FleeMaxTime = Number(TEXT("panic_flee_max_time"), Config.FleeMaxTime);
		Config.MinDuration = Number(TEXT("panic_min_duration"), Config.MinDuration);
		Config.MaxDuration = Number(TEXT("panic_max_duration"), Config.MaxDuration);
		Config.RecoverDistanceFromEnemies = Number(TEXT("panic_recover_distance_from_enemies"), Config.RecoverDistanceFromEnemies);
		Config.HeatSourceCalmRate = Number(TEXT("panic_heat_source_calm_rate"), Config.HeatSourceCalmRate);
		Config.HeatSourcePanicBreakTime = Number(TEXT("panic_heat_source_panic_break_time"), Config.HeatSourcePanicBreakTime);
		Config.FleeTowardsHeatBias = Number(TEXT("panic_flee_towards_heat_bias"), Config.FleeTowardsHeatBias);
		if (Prefix.IsEmpty())
		{
			return Config;
		}
		// Godot apply_soldier_config: the soldier's own keys (both spellings of the thresholds).
		auto Own = [&Number, &Prefix](const TCHAR* Key, float Fallback)
		{
			return Number(*FString::Printf(TEXT("%s_%s"), *Prefix, Key), Fallback);
		};
		Config.StressGainMultiplier = Own(TEXT("stress_gain_multiplier"), Config.StressGainMultiplier);
		Config.StressRecoveryRate = Own(TEXT("panic_recovery_rate"), Config.StressRecoveryRate);
		Config.StressRecoveryMultiplier = Own(TEXT("panic_recovery_mult"), Config.StressRecoveryMultiplier);
		Config.HpThreshold = Own(TEXT("panic_hp_threshold"), Own(TEXT("hp_panic_threshold"), Config.HpThreshold));
		Config.ColdThreshold = Own(TEXT("panic_cold_threshold"), Config.ColdThreshold);
		Config.LowAmmoThreshold = FMath::RoundToInt(Own(TEXT("panic_low_ammo_threshold"), static_cast<float>(Config.LowAmmoThreshold)));
		Config.MonsterThreatDistance = Own(TEXT("panic_monster_threat_distance"), Own(TEXT("monster_threat_distance"), Config.MonsterThreatDistance));
		Config.FleeSpeed = Own(TEXT("panic_flee_speed"), Config.FleeSpeed);
		Config.FleeDistance = Own(TEXT("panic_flee_distance"), Config.FleeDistance);
		Config.MinDuration = Own(TEXT("panic_min_duration"), Config.MinDuration);
		Config.MaxDuration = Own(TEXT("panic_max_duration"), Config.MaxDuration);
		return Config;
	}

	float GetStressGrowth(const FPanicConfig& Config, float HealthFraction, float ColdFraction, int32 Ammo, float NearestEnemyDistance,
		float Fortitude)
	{
		float Stress = 0.f;
		if (Config.HpThreshold > 0.f && HealthFraction < Config.HpThreshold)
		{
			Stress += 20.f * (Config.HpThreshold - HealthFraction) / Config.HpThreshold;
		}
		if (Config.ColdThreshold < 1.f && ColdFraction > Config.ColdThreshold)
		{
			Stress += 25.f * (ColdFraction - Config.ColdThreshold) / (1.f - Config.ColdThreshold);
		}
		if (Ammo <= Config.LowAmmoThreshold)
		{
			Stress += 10.f * (1.f - static_cast<float>(Ammo) / FMath::Max(1.f, static_cast<float>(Config.LowAmmoThreshold)));
		}
		if (NearestEnemyDistance > 0.f && NearestEnemyDistance < Config.MonsterThreatDistance)
		{
			Stress += 15.f * (1.f - NearestEnemyDistance / Config.MonsterThreatDistance);
		}
		const float FortitudeCut = FMath::Clamp(Fortitude * 0.025f, 0.f, 0.65f);
		return Stress * (1.f - FortitudeCut) * Config.StressGainMultiplier;
	}

	float StepStress(const FPanicConfig& Config, float Stress, float Growth, float DeltaSeconds)
	{
		if (Growth > 0.5f)
		{
			return FMath::Min(100.f, Stress + Growth * DeltaSeconds);
		}
		return FMath::Max(0.f, Stress - Config.StressRecoveryRate * Config.StressRecoveryMultiplier * DeltaSeconds);
	}

	float GetDamageStress(const FPanicConfig& Config, float Damage, float Fortitude)
	{
		const float FortitudeCut = FMath::Clamp(Fortitude * 0.02f, 0.f, 0.6f);
		return FMath::Clamp(Damage * 1.5f * (1.f - FortitudeCut) * Config.StressGainMultiplier,
			6.f * Config.StressGainMultiplier, 35.f * Config.StressGainMultiplier);
	}

	float GetLowAmmoStress(const FPanicConfig& Config, float Fortitude)
	{
		const float FortitudeCut = FMath::Clamp(Fortitude * 0.02f, 0.f, 0.5f);
		return 12.f * (1.f - FortitudeCut) * Config.StressGainMultiplier;
	}

	ETriggerVerdict GetTriggerVerdict(const FPanicConfig& Config, int32 PanickedNow, bool bIsLeader)
	{
		if (PanickedNow >= Config.MaxPanickedMembers)
		{
			return ETriggerVerdict::HoldOnLimit;
		}
		if (bIsLeader && PanickedNow > 0)
		{
			return ETriggerVerdict::HoldOnLeader;
		}
		return ETriggerVerdict::Panic;
	}
}
