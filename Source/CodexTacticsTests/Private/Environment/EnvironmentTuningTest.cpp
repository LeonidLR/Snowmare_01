#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Data/EnvironmentTuning.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

// Environment tuning JSON (UE-only; docs/systems/environment-weather.md). Defaults = FEnvironmentConfig.

#define ENV_TUNING_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Environment.Tuning." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	/** Every float field of the config paired with its JSON key, for the field-by-field checks. */
	struct FEnvField
	{
		const TCHAR* Key;
		float FEnvironmentConfig::* Member;
	};

	const FEnvField EnvFields[] = {
		{ TEXT("early_morning_start_hour"), &FEnvironmentConfig::EarlyMorningStartHour },
		{ TEXT("midday_start_hour"), &FEnvironmentConfig::MiddayStartHour },
		{ TEXT("afternoon_start_hour"), &FEnvironmentConfig::AfternoonStartHour },
		{ TEXT("night_start_hour"), &FEnvironmentConfig::NightStartHour },
		{ TEXT("early_morning_real_seconds"), &FEnvironmentConfig::EarlyMorningRealSeconds },
		{ TEXT("midday_real_seconds"), &FEnvironmentConfig::MiddayRealSeconds },
		{ TEXT("afternoon_real_seconds"), &FEnvironmentConfig::AfternoonRealSeconds },
		{ TEXT("night_real_seconds"), &FEnvironmentConfig::NightRealSeconds },
		{ TEXT("default_start_hour"), &FEnvironmentConfig::DefaultStartHour },
		{ TEXT("weather_dwell_phase_fraction"), &FEnvironmentConfig::WeatherDwellPhaseFraction },
		{ TEXT("weather_transition_seconds"), &FEnvironmentConfig::WeatherTransitionSeconds },
		{ TEXT("start_weight_clear"), &FEnvironmentConfig::StartWeightClear },
		{ TEXT("start_weight_overcast"), &FEnvironmentConfig::StartWeightOvercast },
		{ TEXT("start_weight_snowfall"), &FEnvironmentConfig::StartWeightSnowfall },
		{ TEXT("start_weight_blizzard"), &FEnvironmentConfig::StartWeightBlizzard },
		{ TEXT("overcast_step_up_chance"), &FEnvironmentConfig::OvercastStepUpChance },
		{ TEXT("snowfall_step_up_chance"), &FEnvironmentConfig::SnowfallStepUpChance } };
}

ENV_TUNING_TEST(FEnvTuningDefaultsTest, "DefaultsWhenKeysMissing")
bool FEnvTuningDefaultsTest::RunTest(const FString&)
{
	const FEnvironmentConfig Defaults;
	const FEnvironmentConfig Empty = EnvironmentTuning::FromJson(*MakeShared<FJsonObject>());
	for (const FEnvField& Field : EnvFields)
	{
		TestEqual(Field.Key, Empty.*Field.Member, Defaults.*Field.Member);
	}
	// One key present keeps every other field at Base (here a non-default Base).
	FEnvironmentConfig Base;
	Base.MiddayRealSeconds = 111.f;
	Base.NightStartHour = 20.f;
	const TSharedRef<FJsonObject> Partial = MakeShared<FJsonObject>();
	Partial->SetNumberField(TEXT("night_real_seconds"), 400.0);
	const FEnvironmentConfig Config = EnvironmentTuning::FromJson(*Partial, Base);
	TestEqual(TEXT("key read"), Config.NightRealSeconds, 400.f);
	TestEqual(TEXT("base midday kept"), Config.MiddayRealSeconds, 111.f);
	TestEqual(TEXT("base night start kept"), Config.NightStartHour, 20.f);
	return true;
}

ENV_TUNING_TEST(FEnvTuningEveryKeyTest, "EveryKeyRead")
bool FEnvTuningEveryKeyTest::RunTest(const FString&)
{
	// A distinct in-range value per key: chances stay in 0..1, everything else is 1.5 + index (<= 24 for the hours).
	const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	TArray<float> Expected;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(EnvFields); ++Index)
	{
		const bool bChance = FCString::Strstr(EnvFields[Index].Key, TEXT("chance")) != nullptr;
		const float Value = bChance ? 0.1f + 0.01f * Index : 1.5f + static_cast<float>(Index);
		Expected.Add(Value);
		Json->SetNumberField(EnvFields[Index].Key, Value);
	}
	const FEnvironmentConfig Config = EnvironmentTuning::FromJson(*Json);
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(EnvFields); ++Index)
	{
		TestEqual(EnvFields[Index].Key, Config.*EnvFields[Index].Member, Expected[Index]);
	}
	// Out-of-range values clamp like the UPROPERTY meta.
	const TSharedRef<FJsonObject> Bad = MakeShared<FJsonObject>();
	Bad->SetNumberField(TEXT("midday_start_hour"), 99.0);
	Bad->SetNumberField(TEXT("night_real_seconds"), -5.0);
	Bad->SetNumberField(TEXT("overcast_step_up_chance"), 3.0);
	const FEnvironmentConfig Clamped = EnvironmentTuning::FromJson(*Bad);
	TestEqual(TEXT("hour clamped to 24"), Clamped.MiddayStartHour, 24.f);
	TestEqual(TEXT("seconds clamped to 1"), Clamped.NightRealSeconds, 1.f);
	TestEqual(TEXT("chance clamped to 1"), Clamped.OvercastStepUpChance, 1.f);
	return true;
}

ENV_TUNING_TEST(FEnvTuningRoundTripTest, "JsonRoundTrip")
bool FEnvTuningRoundTripTest::RunTest(const FString&)
{
	FEnvironmentConfig Changed;
	Changed.MiddayStartHour = 11.f;
	Changed.NightRealSeconds = 420.f;
	Changed.WeatherDwellPhaseFraction = 0.6f;
	Changed.WeatherTransitionSeconds = 30.f;
	Changed.StartWeightBlizzard = 0.4f;
	Changed.SnowfallStepUpChance = 0.25f;
	const TSharedRef<FJsonObject> Json = EnvironmentTuning::ToJson(Changed);
	// Every key is written.
	for (const FEnvField& Field : EnvFields)
	{
		TestTrue(Field.Key, Json->HasField(Field.Key));
	}
	const FEnvironmentConfig Back = EnvironmentTuning::FromJson(*Json);
	for (const FEnvField& Field : EnvFields)
	{
		TestEqual(Field.Key, Back.*Field.Member, Changed.*Field.Member);
	}
	return true;
}

ENV_TUNING_TEST(FEnvTuningShippedFileTest, "ShippedFileEqualsDefaults")
bool FEnvTuningShippedFileTest::RunTest(const FString&)
{
	FString Text;
	TSharedPtr<FJsonObject> Json;
	if (!TestTrue(TEXT("environment.json readable"), FFileHelper::LoadFileToString(Text, *EnvironmentTuning::GetDefaultPath())))
	{
		return false;
	}
	if (!TestTrue(TEXT("environment.json parses"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) && Json.IsValid()))
	{
		return false;
	}
	const FEnvironmentConfig Defaults;
	for (const FEnvField& Field : EnvFields)
	{
		TestTrue(*FString::Printf(TEXT("file has key %s"), Field.Key), Json->HasField(Field.Key));
	}
	// Start from a deliberately different Base so a key missing in the file would show up as a mismatch.
	FEnvironmentConfig Wrong;
	for (const FEnvField& Field : EnvFields)
	{
		Wrong.*Field.Member = 7.f;
	}
	const FEnvironmentConfig Loaded = EnvironmentTuning::FromJson(*Json, Wrong);
	for (const FEnvField& Field : EnvFields)
	{
		TestEqual(Field.Key, Loaded.*Field.Member, Defaults.*Field.Member);
	}
	return true;
}

#undef ENV_TUNING_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
