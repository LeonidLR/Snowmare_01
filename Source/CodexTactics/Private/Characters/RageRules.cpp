#include "Characters/RageRules.h"
#include "Data/GodotBalanceAsset.h"

FRageConfig RageRules::ConfigFromBalance(const UGodotBalanceAsset* Balance, const FString& Prefix)
{
	FRageConfig Config;
	if (!Balance)
	{
		return Config;
	}
	// General keys (Godot apply_balance_config).
	Config.RequiredCrits = Balance->GetInt(TEXT("rage_required_crits"), Config.RequiredCrits);
	Config.CritMemoryWindow = Balance->GetNumber(TEXT("rage_crit_memory_window"), Config.CritMemoryWindow);
	Config.HighHealthThreshold = Balance->GetNumber(TEXT("rage_high_health_threshold"), Config.HighHealthThreshold);
	Config.MinAmmo = Balance->GetInt(TEXT("rage_min_ammo_threshold"), Config.MinAmmo);
	Config.BaseChance = Balance->GetNumber(TEXT("rage_base_chance"), Config.BaseChance);
	Config.CombatDecayRate = Balance->GetNumber(TEXT("rage_combat_decay_rate"), Config.CombatDecayRate);
	Config.MinChance = Balance->GetNumber(TEXT("rage_min_chance"), Config.MinChance);
	Config.Duration = Balance->GetNumber(TEXT("rage_duration"), Config.Duration);
	Config.ChaoticSwitchTime = Balance->GetNumber(TEXT("rage_chaotic_target_switch_time"), Config.ChaoticSwitchTime);
	Config.FireRateMultiplier = Balance->GetNumber(TEXT("rage_fire_rate_multiplier"), Config.FireRateMultiplier);
	Config.DamageMultiplier = Balance->GetNumber(TEXT("rage_bullet_damage_multiplier"), Config.DamageMultiplier);
	Config.bInfiniteAmmo = Balance->GetNumber(TEXT("rage_infinite_ammo"), Config.bInfiniteAmmo ? 1.f : 0.f) > 0.5f; // bools are imported as 0 / 1
	if (Prefix.IsEmpty())
	{
		return Config;
	}
	// The soldier's own keys (Godot apply_soldier_config).
	auto Key = [&Prefix](const TCHAR* Name) { return FName(Prefix + TEXT("_") + Name); };
	Config.BaseChance = Balance->GetNumber(Key(TEXT("rage_base_chance")), Config.BaseChance);
	Config.RequiredCrits = Balance->GetInt(Key(TEXT("rage_required_crits")), Config.RequiredCrits);
	Config.HighHealthThreshold = Balance->GetNumber(Key(TEXT("rage_high_health_threshold")), Config.HighHealthThreshold);
	Config.MinAmmo = Balance->GetInt(Key(TEXT("rage_min_ammo_threshold")), Config.MinAmmo);
	Config.Duration = Balance->GetNumber(Key(TEXT("rage_duration")), Config.Duration);
	Config.DamageMultiplier = Balance->GetNumber(Key(TEXT("rage_damage_multiplier")), Config.DamageMultiplier);
	Config.FireRateMultiplier = Balance->GetNumber(Key(TEXT("rage_fire_rate_multiplier")), Config.FireRateMultiplier);
	return Config;
}

float RageRules::GetChance(const FRageConfig& Config, float CombatSeconds)
{
	return FMath::Clamp(Config.BaseChance - CombatSeconds / 60.f * Config.CombatDecayRate, Config.MinChance, 1.f);
}

bool RageRules::CanEnterRage(const FRageConfig& Config, float HealthFraction, int32 Clip, float Roll, float Chance)
{
	return HealthFraction >= Config.HighHealthThreshold && Clip >= Config.MinAmmo && Roll <= Chance;
}
