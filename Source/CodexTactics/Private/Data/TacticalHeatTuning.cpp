#include "Data/TacticalHeatTuning.h"

#include "CodexTactics.h"
#include "Dom/JsonObject.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace TacticalHeatTuning
{
	static FTacticalHeatConfig Current;

	struct FFloatKey
	{
		const TCHAR* Key;
		float FTacticalHeatConfig::* Member;
	};

	struct FIntKey
	{
		const TCHAR* Key;
		int32 FTacticalHeatConfig::* Member;
	};

	static const FFloatKey FloatKeys[] = {
		{ TEXT("heat_pack_cold_reduction"), &FTacticalHeatConfig::HeatPackColdReduction },
		{ TEXT("heat_pack_buffer_seconds"), &FTacticalHeatConfig::HeatPackBufferSeconds },
		{ TEXT("heat_pack_accumulation_multiplier"), &FTacticalHeatConfig::HeatPackAccumulationMultiplier },
		{ TEXT("campfire_heat_radius"), &FTacticalHeatConfig::CampfireHeatRadius },
		{ TEXT("campfire_burn_seconds"), &FTacticalHeatConfig::CampfireBurnSeconds },
		{ TEXT("campfire_wind_burn_multiplier"), &FTacticalHeatConfig::CampfireWindBurnMultiplier },
		{ TEXT("campfire_wind_height"), &FTacticalHeatConfig::CampfireWindHeight },
		{ TEXT("campfire_noise_radius"), &FTacticalHeatConfig::CampfireNoiseRadius },
		{ TEXT("campfire_noise_interval_seconds"), &FTacticalHeatConfig::CampfireNoiseIntervalSeconds },
		{ TEXT("stove_heat_radius"), &FTacticalHeatConfig::StoveHeatRadius },
		{ TEXT("stove_gas_capacity_seconds"), &FTacticalHeatConfig::StoveGasCapacitySeconds },
		{ TEXT("stove_gas_seconds_per_round"), &FTacticalHeatConfig::StoveGasSecondsPerRound },
		{ TEXT("stove_pack_up_seconds"), &FTacticalHeatConfig::StovePackUpSeconds },
		{ TEXT("flare_heat_radius"), &FTacticalHeatConfig::FlareHeatRadius },
		{ TEXT("flare_burn_seconds"), &FTacticalHeatConfig::FlareBurnSeconds },
		{ TEXT("flare_throw_range"), &FTacticalHeatConfig::FlareThrowRange },
		{ TEXT("flare_at_feet_distance"), &FTacticalHeatConfig::FlareAtFeetDistance },
		{ TEXT("flare_reveal_radius"), &FTacticalHeatConfig::FlareRevealRadius },
		{ TEXT("puddle_core_radius"), &FTacticalHeatConfig::PuddleCoreRadius },
		{ TEXT("puddle_heat_radius"), &FTacticalHeatConfig::PuddleHeatRadius },
		{ TEXT("puddle_warmth_rate"), &FTacticalHeatConfig::PuddleWarmthRate },
		{ TEXT("puddle_lifetime_min_seconds"), &FTacticalHeatConfig::PuddleLifetimeMinSeconds },
		{ TEXT("puddle_lifetime_max_seconds"), &FTacticalHeatConfig::PuddleLifetimeMaxSeconds },
		{ TEXT("puddle_core_damage_per_second"), &FTacticalHeatConfig::PuddleCoreDamagePerSecond },
		{ TEXT("puddle_damage_interval_seconds"), &FTacticalHeatConfig::PuddleDamageIntervalSeconds },
		{ TEXT("puddle_burning_seconds"), &FTacticalHeatConfig::PuddleBurningSeconds },
		{ TEXT("puddle_burning_tick_damage"), &FTacticalHeatConfig::PuddleBurningTickDamage },
		{ TEXT("heat_tolerance"), &FTacticalHeatConfig::HeatTolerance },
		{ TEXT("exploration_warmth_multiplier"), &FTacticalHeatConfig::ExplorationWarmthMultiplier },
		{ TEXT("combat_warmth_multiplier"), &FTacticalHeatConfig::CombatWarmthMultiplier },
		{ TEXT("warm_idle_cold_threshold"), &FTacticalHeatConfig::WarmIdleColdThreshold },
		{ TEXT("warm_idle_cold_hysteresis"), &FTacticalHeatConfig::WarmIdleColdHysteresis },
		{ TEXT("warm_idle_delay_seconds"), &FTacticalHeatConfig::WarmIdleDelaySeconds },
		{ TEXT("warm_extra_sitter_cold"), &FTacticalHeatConfig::WarmExtraSitterCold },
		{ TEXT("campfire_seat_distance"), &FTacticalHeatConfig::CampfireSeatDistance },
		{ TEXT("campfire_stand_distance"), &FTacticalHeatConfig::CampfireStandDistance },
		{ TEXT("stove_seat_distance"), &FTacticalHeatConfig::StoveSeatDistance },
		{ TEXT("stove_stand_distance"), &FTacticalHeatConfig::StoveStandDistance },
		{ TEXT("barrel_stand_distance"), &FTacticalHeatConfig::BarrelStandDistance },
		{ TEXT("warm_spot_spacing"), &FTacticalHeatConfig::WarmSpotSpacing },
		{ TEXT("warm_spot_arrive_tolerance"), &FTacticalHeatConfig::WarmSpotArriveTolerance },
		{ TEXT("warm_spot_walk_speed"), &FTacticalHeatConfig::WarmSpotWalkSpeed },
		{ TEXT("warm_pull_max_distance"), &FTacticalHeatConfig::WarmPullMaxDistance } };

	static const FIntKey IntKeys[] = {
		{ TEXT("heat_pack_action_points"), &FTacticalHeatConfig::HeatPackActionPoints },
		{ TEXT("heat_pack_max_stack"), &FTacticalHeatConfig::HeatPackMaxStack },
		{ TEXT("campfire_turn_rounds"), &FTacticalHeatConfig::CampfireTurnRounds },
		{ TEXT("campfire_matches_cost"), &FTacticalHeatConfig::CampfireMatchesCost },
		{ TEXT("campfire_firewood_cost"), &FTacticalHeatConfig::CampfireFirewoodCost },
		{ TEXT("campfire_flame_fuel_cost"), &FTacticalHeatConfig::CampfireFlameFuelCost },
		{ TEXT("campfire_build_action_points"), &FTacticalHeatConfig::CampfireBuildActionPoints },
		{ TEXT("stove_deploy_action_points"), &FTacticalHeatConfig::StoveDeployActionPoints },
		{ TEXT("stove_max_carried"), &FTacticalHeatConfig::StoveMaxCarried },
		{ TEXT("flare_turn_rounds"), &FTacticalHeatConfig::FlareTurnRounds },
		{ TEXT("flare_max_carried"), &FTacticalHeatConfig::FlareMaxCarried },
		{ TEXT("puddle_turn_rounds"), &FTacticalHeatConfig::PuddleTurnRounds },
		{ TEXT("warm_min_sitters"), &FTacticalHeatConfig::WarmMinSitters } };

	/** Loadout rows by role (EOperativeRole order). */
	static const TCHAR* const RoleKeys[] = { TEXT("commander"), TEXT("engineer"), TEXT("medic_sapper"), TEXT("recruit") };

	static void ReadLoadout(const FJsonObject& Json, FTacticalHeatLoadout& Out)
	{
		int32 Value = 0;
		if (Json.TryGetNumberField(TEXT("heat_packs"), Value))
		{
			Out.HeatPacks = FMath::Max(0, Value);
		}
		if (Json.TryGetNumberField(TEXT("firewood"), Value))
		{
			Out.Firewood = FMath::Max(0, Value);
		}
		if (Json.TryGetNumberField(TEXT("flares"), Value))
		{
			Out.Flares = FMath::Max(0, Value);
		}
		if (Json.TryGetNumberField(TEXT("stoves"), Value))
		{
			Out.Stoves = FMath::Max(0, Value);
		}
	}
}

FString TacticalHeatTuning::GetDefaultPath()
{
	return FPaths::ProjectContentDir() / TEXT("Data/Survival/tactical_heat.json");
}

const FTacticalHeatConfig& TacticalHeatTuning::Get()
{
	return Current;
}

void TacticalHeatTuning::Set(const FTacticalHeatConfig& Config)
{
	Current = Config;
}

FTacticalHeatConfig TacticalHeatTuning::FromJson(const FJsonObject& Json, const FTacticalHeatConfig& Base)
{
	FTacticalHeatConfig Config = Base;
	for (const FFloatKey& Entry : FloatKeys)
	{
		double Value = 0.0;
		if (Json.TryGetNumberField(Entry.Key, Value))
		{
			Config.*Entry.Member = FMath::Max(0.f, static_cast<float>(Value));
		}
	}
	for (const FIntKey& Entry : IntKeys)
	{
		int32 Value = 0;
		if (Json.TryGetNumberField(Entry.Key, Value))
		{
			Config.*Entry.Member = FMath::Max(0, Value);
		}
	}
	bool bPuddle = Config.bBarrelLeavesPuddle;
	if (Json.TryGetBoolField(TEXT("barrel_leaves_puddle"), bPuddle))
	{
		Config.bBarrelLeavesPuddle = bPuddle;
	}
	bool bPull = Config.bWarmPullFollowers;
	if (Json.TryGetBoolField(TEXT("warm_pull_followers"), bPull))
	{
		Config.bWarmPullFollowers = bPull;
	}
	const TSharedPtr<FJsonObject>* Loadout = nullptr;
	if (Json.TryGetObjectField(TEXT("starting_loadout"), Loadout))
	{
		Config.StartingLoadout.SetNum(FMath::Max(Config.StartingLoadout.Num(), static_cast<int32>(UE_ARRAY_COUNT(RoleKeys))));
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(RoleKeys); ++Index)
		{
			const TSharedPtr<FJsonObject>* Row = nullptr;
			if ((*Loadout)->TryGetObjectField(RoleKeys[Index], Row))
			{
				ReadLoadout(**Row, Config.StartingLoadout[Index]);
			}
		}
	}
	return Config;
}

TSharedRef<FJsonObject> TacticalHeatTuning::ToJson(const FTacticalHeatConfig& Config)
{
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	for (const FFloatKey& Entry : FloatKeys)
	{
		Json->SetNumberField(Entry.Key, Config.*Entry.Member);
	}
	for (const FIntKey& Entry : IntKeys)
	{
		Json->SetNumberField(Entry.Key, Config.*Entry.Member);
	}
	Json->SetBoolField(TEXT("barrel_leaves_puddle"), Config.bBarrelLeavesPuddle);
	Json->SetBoolField(TEXT("warm_pull_followers"), Config.bWarmPullFollowers);
	TSharedRef<FJsonObject> Loadout = MakeShared<FJsonObject>();
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(RoleKeys); ++Index)
	{
		const FTacticalHeatLoadout Row = TacticalHeatRules::GetLoadout(Config, Index);
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("heat_packs"), Row.HeatPacks);
		Object->SetNumberField(TEXT("firewood"), Row.Firewood);
		Object->SetNumberField(TEXT("flares"), Row.Flares);
		Object->SetNumberField(TEXT("stoves"), Row.Stoves);
		Loadout->SetObjectField(RoleKeys[Index], Object);
	}
	Json->SetObjectField(TEXT("starting_loadout"), Loadout);
	return Json;
}

bool TacticalHeatTuning::ApplyFile(const FString& Path)
{
	FString Text;
	TSharedPtr<FJsonObject> Json;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
	{
		return false;
	}
	Current = FromJson(*Json);
	UE_LOG(LogCodexTactics, Display, TEXT("Tactical heat from %s: campfire %.0f cm / %.0f s, stove %.0f cm / %.0f s gas, flare %.0f cm / %.0f s, puddle %.0f / %.0f cm"),
		*Path, Current.CampfireHeatRadius, Current.CampfireBurnSeconds, Current.StoveHeatRadius, Current.StoveGasCapacitySeconds,
		Current.FlareHeatRadius, Current.FlareBurnSeconds, Current.PuddleCoreRadius, Current.PuddleHeatRadius);
	return true;
}

namespace TacticalHeatTuning
{
	static void DumpCommand(const TArray<FString>& Args)
	{
		FString Out;
		FJsonSerializer::Serialize(ToJson(Current), TJsonWriterFactory<>::Create(&Out));
		UE_LOG(LogCodexTactics, Display, TEXT("Tactical heat: %s"), *Out);
	}

	static FAutoConsoleCommand DumpHeat(TEXT("CodexTactics.DumpTacticalHeat"), TEXT("Logs the Sprint 15 heat-source values in force (tactical_heat.json)."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&DumpCommand));
}
