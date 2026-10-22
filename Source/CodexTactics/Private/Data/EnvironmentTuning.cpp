#include "Data/EnvironmentTuning.h"

#include "CodexTactics.h"
#include "Dom/JsonObject.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace EnvironmentTuning
{
	static FEnvironmentConfig Current;

	struct FFloatKey
	{
		const TCHAR* Key;
		float FEnvironmentConfig::* Member;
		/** Lower / upper clamp (mirrors the UPROPERTY meta of the field). */
		float Min;
		float Max;
	};

	static const FFloatKey FloatKeys[] = {
		{ TEXT("early_morning_start_hour"), &FEnvironmentConfig::EarlyMorningStartHour, 0.f, 24.f },
		{ TEXT("midday_start_hour"), &FEnvironmentConfig::MiddayStartHour, 0.f, 24.f },
		{ TEXT("afternoon_start_hour"), &FEnvironmentConfig::AfternoonStartHour, 0.f, 24.f },
		{ TEXT("night_start_hour"), &FEnvironmentConfig::NightStartHour, 0.f, 24.f },
		{ TEXT("early_morning_real_seconds"), &FEnvironmentConfig::EarlyMorningRealSeconds, 1.f, BIG_NUMBER },
		{ TEXT("midday_real_seconds"), &FEnvironmentConfig::MiddayRealSeconds, 1.f, BIG_NUMBER },
		{ TEXT("afternoon_real_seconds"), &FEnvironmentConfig::AfternoonRealSeconds, 1.f, BIG_NUMBER },
		{ TEXT("night_real_seconds"), &FEnvironmentConfig::NightRealSeconds, 1.f, BIG_NUMBER },
		{ TEXT("default_start_hour"), &FEnvironmentConfig::DefaultStartHour, 0.f, 24.f },
		{ TEXT("weather_dwell_phase_fraction"), &FEnvironmentConfig::WeatherDwellPhaseFraction, 0.05f, BIG_NUMBER },
		{ TEXT("weather_transition_seconds"), &FEnvironmentConfig::WeatherTransitionSeconds, 0.f, BIG_NUMBER },
		{ TEXT("start_weight_clear"), &FEnvironmentConfig::StartWeightClear, 0.f, BIG_NUMBER },
		{ TEXT("start_weight_overcast"), &FEnvironmentConfig::StartWeightOvercast, 0.f, BIG_NUMBER },
		{ TEXT("start_weight_snowfall"), &FEnvironmentConfig::StartWeightSnowfall, 0.f, BIG_NUMBER },
		{ TEXT("start_weight_blizzard"), &FEnvironmentConfig::StartWeightBlizzard, 0.f, BIG_NUMBER },
		{ TEXT("overcast_step_up_chance"), &FEnvironmentConfig::OvercastStepUpChance, 0.f, 1.f },
		{ TEXT("snowfall_step_up_chance"), &FEnvironmentConfig::SnowfallStepUpChance, 0.f, 1.f } };
}

FString EnvironmentTuning::GetDefaultPath()
{
	return FPaths::ProjectContentDir() / TEXT("Data/Environment/environment.json");
}

const FEnvironmentConfig& EnvironmentTuning::Get()
{
	return Current;
}

void EnvironmentTuning::Set(const FEnvironmentConfig& Config)
{
	Current = Config;
}

FEnvironmentConfig EnvironmentTuning::FromJson(const FJsonObject& Json, const FEnvironmentConfig& Base)
{
	FEnvironmentConfig Config = Base;
	for (const FFloatKey& Entry : FloatKeys)
	{
		double Value = 0.0;
		if (Json.TryGetNumberField(Entry.Key, Value))
		{
			Config.*Entry.Member = FMath::Clamp(static_cast<float>(Value), Entry.Min, Entry.Max);
		}
	}
	return Config;
}

TSharedRef<FJsonObject> EnvironmentTuning::ToJson(const FEnvironmentConfig& Config)
{
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	for (const FFloatKey& Entry : FloatKeys)
	{
		Json->SetNumberField(Entry.Key, Config.*Entry.Member);
	}
	return Json;
}

bool EnvironmentTuning::ApplyFile(const FString& Path)
{
	FString Text;
	TSharedPtr<FJsonObject> Json;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
	{
		return false;
	}
	Current = FromJson(*Json);
	UE_LOG(LogCodexTactics, Display, TEXT("Environment from %s: phases %.0f/%.0f/%.0f/%.0f h, %.0f/%.0f/%.0f/%.0f s, weather dwell %.2f of phase, blend %.0f s"),
		*Path, Current.EarlyMorningStartHour, Current.MiddayStartHour, Current.AfternoonStartHour, Current.NightStartHour,
		Current.EarlyMorningRealSeconds, Current.MiddayRealSeconds, Current.AfternoonRealSeconds, Current.NightRealSeconds,
		Current.WeatherDwellPhaseFraction, Current.WeatherTransitionSeconds);
	return true;
}

namespace EnvironmentTuning
{
	static void DumpCommand(const TArray<FString>& Args)
	{
		FString Out;
		FJsonSerializer::Serialize(ToJson(Current), TJsonWriterFactory<>::Create(&Out));
		UE_LOG(LogCodexTactics, Display, TEXT("Environment: %s"), *Out);
	}

	static FAutoConsoleCommand DumpEnvironment(TEXT("CodexTactics.DumpEnvironment"), TEXT("Logs the day cycle / weather values in force (environment.json)."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&DumpCommand));
}
