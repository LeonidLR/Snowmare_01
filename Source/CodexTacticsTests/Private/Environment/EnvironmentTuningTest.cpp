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
		{ TEXT("snowfall_step_up_chance"), &FEnvironmentConfig::SnowfallStepUpChance },
		{ TEXT("cold_mult_clear"), &FEnvironmentConfig::ColdMultClear },
		{ TEXT("cold_mult_overcast"), &FEnvironmentConfig::ColdMultOvercast },
		{ TEXT("cold_mult_snowfall"), &FEnvironmentConfig::ColdMultSnowfall },
		{ TEXT("cold_mult_blizzard"), &FEnvironmentConfig::ColdMultBlizzard },
		{ TEXT("cold_mult_early_morning"), &FEnvironmentConfig::ColdMultEarlyMorning },
		{ TEXT("cold_mult_midday"), &FEnvironmentConfig::ColdMultMidday },
		{ TEXT("cold_mult_afternoon"), &FEnvironmentConfig::ColdMultAfternoon },
		{ TEXT("cold_mult_night"), &FEnvironmentConfig::ColdMultNight } };

	/** Parses a JSON object literal (the test fails on a syntax error in the literal). */
	TSharedRef<FJsonObject> ParseObject(const TCHAR* Text)
	{
		TSharedPtr<FJsonObject> Json;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(Text)), Json);
		return Json.IsValid() ? Json.ToSharedRef() : MakeShared<FJsonObject>();
	}

	const FEnvironmentSuitabilityRow* FindRow(const FEnvironmentRosterConfig& Roster, EEnemyArchetype Type)
	{
		return Roster.Suitability.FindByPredicate([Type](const FEnvironmentSuitabilityRow& Row) { return Row.Type == Type; });
	}

	float Score(const FEnvironmentSuitabilityRow& Row, EWeatherState Weather, ETimeOfDayPhase Phase)
	{
		return Row.Scores[static_cast<int32>(Weather) * 4 + static_cast<int32>(Phase)];
	}

	/** A complete suitability row literal, every score = Value. */
	FString FullRow(float Value)
	{
		const FString Four = FString::Printf(TEXT("[%g, %g, %g, %g]"), Value, Value, Value, Value);
		return FString::Printf(TEXT("{\"Clear\": %s, \"Overcast\": %s, \"Snowfall\": %s, \"Blizzard\": %s}"), *Four, *Four, *Four, *Four);
	}
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
	// The bool key (default false) is read both ways.
	TestFalse(TEXT("cold_on_locked_levels default"), EnvironmentTuning::FromJson(*MakeShared<FJsonObject>()).bColdOnLockedLevels);
	const TSharedRef<FJsonObject> LockedOn = MakeShared<FJsonObject>();
	LockedOn->SetBoolField(TEXT("cold_on_locked_levels"), true);
	TestTrue(TEXT("cold_on_locked_levels read"), EnvironmentTuning::FromJson(*LockedOn).bColdOnLockedLevels);
	FEnvironmentConfig LockedBase;
	LockedBase.bColdOnLockedLevels = true;
	TestTrue(TEXT("cold_on_locked_levels missing keeps Base"), EnvironmentTuning::FromJson(*MakeShared<FJsonObject>(), LockedBase).bColdOnLockedLevels);
	// Out-of-range values clamp like the UPROPERTY meta.
	const TSharedRef<FJsonObject> Bad = MakeShared<FJsonObject>();
	Bad->SetNumberField(TEXT("midday_start_hour"), 99.0);
	Bad->SetNumberField(TEXT("night_real_seconds"), -5.0);
	Bad->SetNumberField(TEXT("overcast_step_up_chance"), 3.0);
	Bad->SetNumberField(TEXT("cold_mult_night"), -2.0);
	const FEnvironmentConfig Clamped = EnvironmentTuning::FromJson(*Bad);
	TestEqual(TEXT("cold multiplier clamped to 0"), Clamped.ColdMultNight, 0.f);
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
	Changed.ColdMultBlizzard = 3.f;
	Changed.ColdMultNight = 1.7f;
	Changed.bColdOnLockedLevels = true;
	FEnvironmentRosterConfig& Roster = Changed.Roster;
	Roster.bEnabled = false;
	Roster.HalfBelow = 1.8f;
	Roster.AllBelow = 1.1f;
	Roster.BonusAbove = 3.f;
	Roster.BonusMultiplier = 2.f;
	Roster.Pairs = { { EEnemyArchetype::Spitter, EEnemyArchetype::Cutter, 3 }, { EEnemyArchetype::Marksman, EEnemyArchetype::Brute, 1 } };
	Roster.BonusTypes = { EEnemyArchetype::FrostHound, EEnemyArchetype::Cutter };
	FEnvironmentSuitabilityRow Row;
	Row.Type = EEnemyArchetype::Spitter;
	for (int32 Index = 0; Index < 16; ++Index)
	{
		Row.Scores.Add(0.25f * static_cast<float>(Index % 17));
	}
	Roster.Suitability = { Row };
	Row.Type = EEnemyArchetype::CryoDrone;
	Row.Scores[15] = 4.f;
	Roster.Suitability.Add(Row);

	const TSharedRef<FJsonObject> Json = EnvironmentTuning::ToJson(Changed);
	// Every key is written.
	for (const FEnvField& Field : EnvFields)
	{
		TestTrue(Field.Key, Json->HasField(Field.Key));
	}
	TestTrue(TEXT("cold_on_locked_levels written"), Json->HasField(TEXT("cold_on_locked_levels")));
	TestTrue(TEXT("roster written"), Json->HasTypedField<EJson::Object>(TEXT("roster")));
	const FEnvironmentConfig Back = EnvironmentTuning::FromJson(*Json);
	for (const FEnvField& Field : EnvFields)
	{
		TestEqual(Field.Key, Back.*Field.Member, Changed.*Field.Member);
	}
	TestTrue(TEXT("cold_on_locked_levels"), Back.bColdOnLockedLevels);
	const FEnvironmentRosterConfig& BackRoster = Back.Roster;
	TestFalse(TEXT("roster enabled"), BackRoster.bEnabled);
	TestEqual(TEXT("half_below"), BackRoster.HalfBelow, 1.8f);
	TestEqual(TEXT("all_below"), BackRoster.AllBelow, 1.1f);
	TestEqual(TEXT("bonus_above"), BackRoster.BonusAbove, 3.f);
	TestEqual(TEXT("bonus_multiplier"), BackRoster.BonusMultiplier, 2.f);
	if (TestEqual(TEXT("pair count"), BackRoster.Pairs.Num(), 2))
	{
		TestTrue(TEXT("pair 0 from"), BackRoster.Pairs[0].From == EEnemyArchetype::Spitter);
		TestTrue(TEXT("pair 0 to"), BackRoster.Pairs[0].To == EEnemyArchetype::Cutter);
		TestEqual(TEXT("pair 0 ratio"), BackRoster.Pairs[0].Ratio, 3);
		TestTrue(TEXT("pair 1 from"), BackRoster.Pairs[1].From == EEnemyArchetype::Marksman);
		TestTrue(TEXT("pair 1 to"), BackRoster.Pairs[1].To == EEnemyArchetype::Brute);
	}
	TestEqual(TEXT("bonus type count"), BackRoster.BonusTypes.Num(), 2);
	TestTrue(TEXT("bonus HOUND"), BackRoster.BonusTypes.Contains(EEnemyArchetype::FrostHound));
	TestTrue(TEXT("bonus CUTTER"), BackRoster.BonusTypes.Contains(EEnemyArchetype::Cutter));
	TestEqual(TEXT("row count"), BackRoster.Suitability.Num(), 2);
	for (const FEnvironmentSuitabilityRow& Expected : Roster.Suitability)
	{
		const FEnvironmentSuitabilityRow* Found = FindRow(BackRoster, Expected.Type);
		if (TestNotNull(TEXT("row present"), Found))
		{
			TestTrue(TEXT("row scores"), Found->Scores == Expected.Scores);
		}
	}
	return true;
}

ENV_TUNING_TEST(FEnvTuningRosterParseTest, "RosterParse")
bool FEnvTuningRosterParseTest::RunTest(const FString&)
{
	// Names are case-insensitive and FROST_HOUND is an alias of HOUND (same parser as the level JSON); scores clamp to 0..4.
	const FString Text = FString::Printf(TEXT("{\"roster\": {"
		"\"enabled\": false, \"half_below\": 1.5, \"all_below\": 0.5, \"bonus_above\": 9, \"bonus_multiplier\": 0.2,"
		"\"pairs\": [{\"from\": \"marksman\", \"to\": \"FROSTBITTEN\", \"ratio\": 2}, {\"from\": \"SPITTER\", \"to\": \"CUTTER\"}, {\"from\": \"NOPE\", \"to\": \"CUTTER\"}],"
		"\"bonus_types\": [\"FROST_HOUND\", \"UNKNOWN\"],"
		"\"suitability\": {\"HOUND\": {\"Clear\": [1, 2, 3, 4], \"Overcast\": [5, -1, 0.5, 4], \"Snowfall\": [0, 0, 0, 0], \"Blizzard\": [4, 3, 2, 1]}, \"BRUTE\": %s}}}"),
		*FullRow(2.5f));
	const FEnvironmentConfig Config = EnvironmentTuning::FromJson(*ParseObject(*Text));
	const FEnvironmentRosterConfig& Roster = Config.Roster;
	TestFalse(TEXT("enabled"), Roster.bEnabled);
	TestEqual(TEXT("half_below"), Roster.HalfBelow, 1.5f);
	TestEqual(TEXT("all_below"), Roster.AllBelow, 0.5f);
	TestEqual(TEXT("bonus_above clamped to 4"), Roster.BonusAbove, 4.f);
	TestEqual(TEXT("bonus_multiplier clamped to 1"), Roster.BonusMultiplier, 1.f);
	if (TestEqual(TEXT("pairs (unknown type skipped)"), Roster.Pairs.Num(), 2))
	{
		TestTrue(TEXT("pair 0 from"), Roster.Pairs[0].From == EEnemyArchetype::Marksman);
		TestTrue(TEXT("pair 0 to"), Roster.Pairs[0].To == EEnemyArchetype::Frostbitten);
		TestEqual(TEXT("pair 0 ratio"), Roster.Pairs[0].Ratio, 2);
		TestTrue(TEXT("pair 1 from"), Roster.Pairs[1].From == EEnemyArchetype::Spitter);
		TestEqual(TEXT("pair 1 ratio defaults to 1"), Roster.Pairs[1].Ratio, 1);
	}
	if (TestEqual(TEXT("bonus types (unknown skipped)"), Roster.BonusTypes.Num(), 1))
	{
		TestTrue(TEXT("bonus HOUND"), Roster.BonusTypes[0] == EEnemyArchetype::FrostHound);
	}
	TestEqual(TEXT("rows"), Roster.Suitability.Num(), 2);
	const FEnvironmentSuitabilityRow* Hound = FindRow(Roster, EEnemyArchetype::FrostHound);
	if (TestNotNull(TEXT("HOUND row"), Hound) && TestEqual(TEXT("16 scores"), Hound->Scores.Num(), 16))
	{
		TestEqual(TEXT("Clear EarlyMorning"), Score(*Hound, EWeatherState::Clear, ETimeOfDayPhase::EarlyMorning), 1.f);
		TestEqual(TEXT("Clear Night"), Score(*Hound, EWeatherState::Clear, ETimeOfDayPhase::Night), 4.f);
		TestEqual(TEXT("Overcast EarlyMorning clamped to 4"), Score(*Hound, EWeatherState::Overcast, ETimeOfDayPhase::EarlyMorning), 4.f);
		TestEqual(TEXT("Overcast Midday clamped to 0"), Score(*Hound, EWeatherState::Overcast, ETimeOfDayPhase::Midday), 0.f);
		TestEqual(TEXT("Overcast Afternoon"), Score(*Hound, EWeatherState::Overcast, ETimeOfDayPhase::Afternoon), 0.5f);
		TestEqual(TEXT("Blizzard Midday"), Score(*Hound, EWeatherState::Blizzard, ETimeOfDayPhase::Midday), 3.f);
		TestEqual(TEXT("Blizzard Night"), Score(*Hound, EWeatherState::Blizzard, ETimeOfDayPhase::Night), 1.f);
	}
	return true;
}

ENV_TUNING_TEST(FEnvTuningRosterRejectTest, "RosterRejectsBadRows")
bool FEnvTuningRosterRejectTest::RunTest(const FString&)
{
	// Warnings are logged for the rejected rows (LogCodexTactics); the good rows survive.
	const FString Text = FString::Printf(TEXT("{\"roster\": {\"suitability\": {"
		"\"HOUND\": %s,"
		"\"SPITTER\": {\"Clear\": [1, 1, 1, 1], \"Overcast\": [1, 1, 1, 1], \"Snowfall\": [1, 1, 1, 1]},"
		"\"BRUTE\": {\"Clear\": [1, 1, 1], \"Overcast\": [1, 1, 1, 1], \"Snowfall\": [1, 1, 1, 1], \"Blizzard\": [1, 1, 1, 1]},"
		"\"CUTTER\": {\"Clear\": [1, 1, 1, 1], \"Overcast\": [1, 1, 1, 1], \"Snowfall\": [1, 1, 1, 1], \"Blizzard\": [1, \"x\", 1, 1]},"
		"\"YETI\": %s,"
		"\"MARKSMAN\": %s}}}"),
		*FullRow(2.f), *FullRow(2.f), *FullRow(1.f));
	const FEnvironmentConfig Config = EnvironmentTuning::FromJson(*ParseObject(*Text));
	TestEqual(TEXT("only the complete known rows are kept"), Config.Roster.Suitability.Num(), 2);
	TestNotNull(TEXT("HOUND kept"), FindRow(Config.Roster, EEnemyArchetype::FrostHound));
	TestNotNull(TEXT("MARKSMAN kept"), FindRow(Config.Roster, EEnemyArchetype::Marksman));
	TestNull(TEXT("SPITTER (weather missing) rejected, not half-filled"), FindRow(Config.Roster, EEnemyArchetype::Spitter));
	TestNull(TEXT("BRUTE (short weather) rejected"), FindRow(Config.Roster, EEnemyArchetype::Brute));
	TestNull(TEXT("CUTTER (non-numeric score) rejected"), FindRow(Config.Roster, EEnemyArchetype::Cutter));

	// No "roster" key at all keeps Base.Roster untouched.
	FEnvironmentConfig Base;
	Base.Roster.bEnabled = false;
	Base.Roster.Pairs = { { EEnemyArchetype::Spitter, EEnemyArchetype::Cutter, 4 } };
	Base.Roster.Suitability = Config.Roster.Suitability;
	const FEnvironmentConfig Kept = EnvironmentTuning::FromJson(*MakeShared<FJsonObject>(), Base);
	TestFalse(TEXT("Base roster disabled kept"), Kept.Roster.bEnabled);
	TestEqual(TEXT("Base pairs kept"), Kept.Roster.Pairs.Num(), 1);
	TestEqual(TEXT("Base rows kept"), Kept.Roster.Suitability.Num(), 2);
	// A roster object with only some keys keeps the rest of Base.
	const FEnvironmentConfig Partial = EnvironmentTuning::FromJson(*ParseObject(TEXT("{\"roster\": {\"half_below\": 1.7}}")), Base);
	TestEqual(TEXT("half_below read"), Partial.Roster.HalfBelow, 1.7f);
	TestEqual(TEXT("pairs kept"), Partial.Roster.Pairs.Num(), 1);
	TestEqual(TEXT("rows kept"), Partial.Roster.Suitability.Num(), 2);
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
	TestTrue(TEXT("file has cold_on_locked_levels"), Json->HasField(TEXT("cold_on_locked_levels")));
	TestTrue(TEXT("cold_on_locked_levels"), Loaded.bColdOnLockedLevels == Defaults.bColdOnLockedLevels);

	// The shipped roster (Jev scores, roster v2 approved 2026-10-22): the struct default roster is empty, so it is checked on its own.
	const FEnvironmentRosterConfig& Roster = Loaded.Roster;
	TestTrue(TEXT("roster enabled"), Roster.bEnabled);
	TestEqual(TEXT("half_below"), Roster.HalfBelow, 2.0f);
	TestEqual(TEXT("all_below"), Roster.AllBelow, 1.4f);
	TestEqual(TEXT("bonus_above"), Roster.BonusAbove, 2.7f);
	TestEqual(TEXT("bonus_multiplier"), Roster.BonusMultiplier, 1.5f);
	if (TestEqual(TEXT("2 pairs"), Roster.Pairs.Num(), 2))
	{
		TestTrue(TEXT("MARKSMAN -> FROSTBITTEN"), Roster.Pairs[0].From == EEnemyArchetype::Marksman && Roster.Pairs[0].To == EEnemyArchetype::Frostbitten);
		TestEqual(TEXT("MARKSMAN ratio"), Roster.Pairs[0].Ratio, 2);
		TestTrue(TEXT("SPITTER -> CUTTER"), Roster.Pairs[1].From == EEnemyArchetype::Spitter && Roster.Pairs[1].To == EEnemyArchetype::Cutter);
		TestEqual(TEXT("SPITTER ratio"), Roster.Pairs[1].Ratio, 1);
	}
	if (TestEqual(TEXT("bonus types"), Roster.BonusTypes.Num(), 1))
	{
		TestTrue(TEXT("bonus HOUND"), Roster.BonusTypes[0] == EEnemyArchetype::FrostHound);
	}
	if (TestEqual(TEXT("6 suitability rows"), Roster.Suitability.Num(), 6))
	{
		for (const FEnvironmentSuitabilityRow& Row : Roster.Suitability)
		{
			TestEqual(TEXT("16 scores"), Row.Scores.Num(), 16);
		}
		const FEnvironmentSuitabilityRow* Marksman = FindRow(Roster, EEnemyArchetype::Marksman);
		const FEnvironmentSuitabilityRow* Hound = FindRow(Roster, EEnemyArchetype::FrostHound);
		if (TestNotNull(TEXT("MARKSMAN row"), Marksman) && Marksman->Scores.Num() == 16)
		{
			TestEqual(TEXT("MARKSMAN Blizzard Night"), Score(*Marksman, EWeatherState::Blizzard, ETimeOfDayPhase::Night), 0.41f);
			TestEqual(TEXT("MARKSMAN Clear Midday"), Score(*Marksman, EWeatherState::Clear, ETimeOfDayPhase::Midday), 3.87f);
		}
		if (TestNotNull(TEXT("HOUND row"), Hound) && Hound->Scores.Num() == 16)
		{
			TestEqual(TEXT("HOUND Overcast Night"), Score(*Hound, EWeatherState::Overcast, ETimeOfDayPhase::Night), 2.75f);
		}
	}
	return true;
}

#undef ENV_TUNING_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
