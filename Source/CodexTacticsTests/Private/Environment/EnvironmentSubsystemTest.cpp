#include "Misc/AutomationTest.h"
#include "Data/LevelJsonRules.h"
#include "Dom/JsonObject.h"
#include "Environment/EnvironmentRules.h"
#include "Environment/EnvironmentSubsystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

// The level JSON "environment" block and the start-state decision of UEnvironmentSubsystem (docs/systems/environment-weather.md,
// user decisions 11 and 17: only the Demo 01 tutorial is locked; every later level continues where the previous one ended).

namespace
{
	TSharedPtr<FJsonObject> EnvironmentTestJson(const FString& Text)
	{
		TSharedPtr<FJsonObject> Root;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
		return Root;
	}

	FEnvironmentLevelSettings LockedTutorialSettings()
	{
		FEnvironmentLevelSettings Settings;
		Settings.bLocked = true;
		Settings.bHasStartHour = true;
		Settings.StartHour = 23.f;
		Settings.bHasWeather = true;
		Settings.Weather = EWeatherState::Snowfall;
		return Settings;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnvironmentLevelParseTest, "CodexTactics.Environment.Level.ParseEnvironmentBlock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnvironmentLevelParseTest::RunTest(const FString&)
{
	// Present: every key.
	{
		const TSharedPtr<FJsonObject> Json = EnvironmentTestJson(TEXT(R"({"locked": true, "start_hour": 23, "weather": "snowfall", "seed": 42})"));
		if (!TestTrue(TEXT("Json parses"), Json.IsValid()))
		{
			return false;
		}
		FString Error;
		const FEnvironmentLevelSettings Settings = LevelJsonRules::ParseEnvironmentSettings(*Json, &Error);
		TestTrue(TEXT("locked"), Settings.bLocked);
		TestTrue(TEXT("has start_hour"), Settings.bHasStartHour);
		TestEqual(TEXT("start_hour"), Settings.StartHour, 23.f);
		TestTrue(TEXT("has weather"), Settings.bHasWeather);
		TestTrue(TEXT("weather (any case)"), Settings.Weather == EWeatherState::Snowfall);
		TestEqual(TEXT("seed"), Settings.Seed, 42);
		TestTrue(TEXT("no error"), Error.IsEmpty());
	}
	// Absent keys keep the procedural default.
	{
		const TSharedPtr<FJsonObject> Json = EnvironmentTestJson(TEXT("{}"));
		const FEnvironmentLevelSettings Settings = LevelJsonRules::ParseEnvironmentSettings(*Json);
		TestFalse(TEXT("not locked"), Settings.bLocked);
		TestFalse(TEXT("no start_hour"), Settings.bHasStartHour);
		TestFalse(TEXT("no weather"), Settings.bHasWeather);
		TestEqual(TEXT("seed 0 = random"), Settings.Seed, 0);
	}
	// A bad weather string is ignored (reported), the other keys still apply; start_hour clamps.
	{
		const TSharedPtr<FJsonObject> Json = EnvironmentTestJson(TEXT(R"({"weather": "Rain", "start_hour": 30, "locked": false})"));
		FString Error;
		const FEnvironmentLevelSettings Settings = LevelJsonRules::ParseEnvironmentSettings(*Json, &Error);
		TestFalse(TEXT("unknown weather ignored"), Settings.bHasWeather);
		TestTrue(TEXT("unknown weather reported"), Error.Contains(TEXT("Rain")));
		TestTrue(TEXT("start_hour still read"), Settings.bHasStartHour);
		TestEqual(TEXT("start_hour clamped"), Settings.StartHour, 24.f);
	}
	// Through ParseLevel: with and without the block.
	{
		FLevelCombatConfig Config;
		FString Error;
		TestTrue(TEXT("Level with environment parses"), LevelJsonRules::ParseLevel(
			TEXT(R"({"level_id": "env", "environment": { "locked": true, "start_hour": 23, "weather": "Snowfall" }})"), TEXT("env"), Config, Error));
		TestTrue(TEXT("Level locked"), Config.Environment.bLocked);
		TestEqual(TEXT("Level start_hour"), Config.Environment.StartHour, 23.f);
		TestTrue(TEXT("Level weather"), Config.Environment.bHasWeather && Config.Environment.Weather == EWeatherState::Snowfall);

		FLevelCombatConfig Plain;
		TestTrue(TEXT("Level without environment parses"), LevelJsonRules::ParseLevel(TEXT(R"({"level_id": "plain"})"), TEXT("plain"), Plain, Error));
		TestFalse(TEXT("Plain level unlocked"), Plain.Environment.bLocked);
		TestFalse(TEXT("Plain level no hour"), Plain.Environment.bHasStartHour);
		TestFalse(TEXT("Plain level no weather"), Plain.Environment.bHasWeather);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnvironmentResolveInitialStateTest, "CodexTactics.Environment.Level.ResolveInitialState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnvironmentResolveInitialStateTest::RunTest(const FString&)
{
	const FEnvironmentConfig Config;
	const TOptional<FEnvironmentState> NoCarry;

	// Locked level: its hour / weather, nothing else matters.
	{
		const FEnvironmentState State = UEnvironmentSubsystem::ResolveInitialState(Config, LockedTutorialSettings(), NoCarry, 7);
		TestTrue(TEXT("Locked"), State.bLocked);
		TestEqual(TEXT("Locked hour"), State.Hour, 23.f);
		TestTrue(TEXT("Locked weather"), State.Weather == EWeatherState::Snowfall);
		TestTrue(TEXT("Locked next weather is a neighbour"), EnvironmentRules::AreNeighbours(State.Weather, State.NextWeather));
		TestTrue(TEXT("Locked night phase"), EnvironmentRules::GetPhase(Config, State.Hour) == ETimeOfDayPhase::Night);
		TestEqual(TEXT("Locked dwell = night dwell"), State.WeatherDwellSeconds, EnvironmentRules::GetWeatherDwellSeconds(Config, ETimeOfDayPhase::Night), 0.01f);
		TestEqual(TEXT("Locked elapsed 0"), State.WeatherElapsedSeconds, 0.f);
	}
	// Locked level without hour / weather: the defaults and a seeded start weather.
	{
		FEnvironmentLevelSettings Settings;
		Settings.bLocked = true;
		const FEnvironmentState A = UEnvironmentSubsystem::ResolveInitialState(Config, Settings, NoCarry, 11);
		const FEnvironmentState B = UEnvironmentSubsystem::ResolveInitialState(Config, Settings, NoCarry, 11);
		TestTrue(TEXT("Locked default locked"), A.bLocked);
		TestEqual(TEXT("Locked default hour"), A.Hour, Config.DefaultStartHour);
		TestTrue(TEXT("Same seed, same weather"), A.Weather == B.Weather && A.NextWeather == B.NextWeather);
		TestEqual(TEXT("Same seed, same RNG state"), A.RandomSeed, B.RandomSeed);
	}
	// A locked level ignores a carried state (the tutorial is fixed even when replayed after another level).
	{
		FEnvironmentState Previous;
		Previous.Hour = 12.f;
		Previous.Weather = EWeatherState::Clear;
		const FEnvironmentState State = UEnvironmentSubsystem::ResolveInitialState(Config, LockedTutorialSettings(), TOptional<FEnvironmentState>(Previous), 3);
		TestTrue(TEXT("Locked beats carry: locked"), State.bLocked);
		TestEqual(TEXT("Locked beats carry: hour"), State.Hour, 23.f);
		TestTrue(TEXT("Locked beats carry: weather"), State.Weather == EWeatherState::Snowfall);
	}
	// Fresh with level settings: unlocked, the level's hour / weather / seed.
	{
		FEnvironmentLevelSettings Settings;
		Settings.bHasStartHour = true;
		Settings.StartHour = 15.5f;
		Settings.bHasWeather = true;
		Settings.Weather = EWeatherState::Blizzard;
		Settings.Seed = 99;
		const FEnvironmentState A = UEnvironmentSubsystem::ResolveInitialState(Config, Settings, NoCarry, 1);
		const FEnvironmentState B = UEnvironmentSubsystem::ResolveInitialState(Config, Settings, NoCarry, 2);
		TestFalse(TEXT("Fresh unlocked"), A.bLocked);
		TestEqual(TEXT("Fresh hour"), A.Hour, 15.5f);
		TestTrue(TEXT("Fresh weather"), A.Weather == EWeatherState::Blizzard);
		TestTrue(TEXT("Blizzard forced down"), A.NextWeather == EWeatherState::Snowfall);
		TestEqual(TEXT("Level seed wins over the random seed"), A.RandomSeed, B.RandomSeed);
		TestEqual(TEXT("Afternoon dwell"), A.WeatherDwellSeconds, EnvironmentRules::GetWeatherDwellSeconds(Config, ETimeOfDayPhase::Afternoon), 0.01f);
	}
	// Fresh without settings: DefaultStartHour, a random start weather from the given seed (deterministic per seed).
	{
		const FEnvironmentLevelSettings Settings;
		const FEnvironmentState A = UEnvironmentSubsystem::ResolveInitialState(Config, Settings, NoCarry, 5);
		const FEnvironmentState B = UEnvironmentSubsystem::ResolveInitialState(Config, Settings, NoCarry, 5);
		TestFalse(TEXT("Fresh default unlocked"), A.bLocked);
		TestEqual(TEXT("Fresh default hour"), A.Hour, Config.DefaultStartHour);
		TestTrue(TEXT("Fresh default deterministic"), A.Weather == B.Weather && A.NextWeather == B.NextWeather && A.RandomSeed == B.RandomSeed);
		TestTrue(TEXT("Fresh default neighbours"), EnvironmentRules::AreNeighbours(A.Weather, A.NextWeather));
		TestNotEqual(TEXT("Fresh default seed is never 0"), A.RandomSeed, 0);
		// Different seeds reach every start weather eventually (the weights are all > 0).
		TSet<EWeatherState> Seen;
		for (int32 Seed = 1; Seed <= 64; ++Seed)
		{
			Seen.Add(UEnvironmentSubsystem::ResolveInitialState(Config, Settings, NoCarry, Seed).Weather);
		}
		TestEqual(TEXT("All four start weathers occur over 64 seeds"), Seen.Num(), 4);
	}
	// Carried: continued exactly (hour, weather, next, elapsed, dwell, RNG) even when the level names a start hour / weather.
	{
		FEnvironmentState Previous;
		Previous.Hour = 16.25f;
		Previous.Weather = EWeatherState::Snowfall;
		Previous.NextWeather = EWeatherState::Overcast;
		Previous.WeatherElapsedSeconds = 100.f;
		Previous.WeatherDwellSeconds = 225.f;
		Previous.RandomSeed = 12345;
		FEnvironmentLevelSettings Settings;
		Settings.bHasStartHour = true;
		Settings.StartHour = 6.f;
		Settings.bHasWeather = true;
		Settings.Weather = EWeatherState::Clear;
		const FEnvironmentState State = UEnvironmentSubsystem::ResolveInitialState(Config, Settings, TOptional<FEnvironmentState>(Previous), 8);
		TestFalse(TEXT("Carried unlocked"), State.bLocked);
		TestEqual(TEXT("Carried hour"), State.Hour, 16.25f);
		TestTrue(TEXT("Carried weather"), State.Weather == EWeatherState::Snowfall);
		TestTrue(TEXT("Carried next"), State.NextWeather == EWeatherState::Overcast);
		TestEqual(TEXT("Carried elapsed"), State.WeatherElapsedSeconds, 100.f);
		TestEqual(TEXT("Carried dwell"), State.WeatherDwellSeconds, 225.f);
		TestEqual(TEXT("Carried RNG"), State.RandomSeed, 12345);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnvironmentCarryLockedTutorialTest, "CodexTactics.Environment.Carry.LockedTutorialHandsOver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnvironmentCarryLockedTutorialTest::RunTest(const FString&)
{
	const FEnvironmentConfig Config;
	// The Demo 01 tutorial (locked Night + Snowfall) ends; the subsystem hands its END state over as is (bLocked still true).
	const FEnvironmentState TutorialEnd = UEnvironmentSubsystem::ResolveInitialState(Config, LockedTutorialSettings(), TOptional<FEnvironmentState>(), 21);
	TestTrue(TEXT("Tutorial end locked"), TutorialEnd.bLocked);

	// The next level (no "environment" block) continues at the same hour / weather, UNLOCKED, and its clock may run.
	const FEnvironmentState Next = UEnvironmentSubsystem::ResolveInitialState(Config, FEnvironmentLevelSettings(), TOptional<FEnvironmentState>(TutorialEnd), 22);
	TestFalse(TEXT("Next level unlocked"), Next.bLocked);
	TestEqual(TEXT("Next level same hour"), Next.Hour, 23.f);
	TestTrue(TEXT("Next level same weather"), Next.Weather == EWeatherState::Snowfall);
	TestTrue(TEXT("Next level same next weather"), Next.NextWeather == TutorialEnd.NextWeather);
	TestEqual(TEXT("Next level same dwell"), Next.WeatherDwellSeconds, TutorialEnd.WeatherDwellSeconds);
	TestEqual(TEXT("Next level same RNG"), Next.RandomSeed, TutorialEnd.RandomSeed);
	TestTrue(TEXT("Next level is night"), EnvironmentRules::GetPhase(Config, Next.Hour) == ETimeOfDayPhase::Night);

	// Its clock runs: the weather steps (a locked copy would not).
	FEnvironmentState Running = Next;
	const FWeatherStepResult Step = EnvironmentRules::StepWeather(Config, Running, Running.WeatherDwellSeconds + 1.f);
	TestTrue(TEXT("Unlocked copy steps the weather"), Step.bWeatherChanged);
	FEnvironmentState Frozen = TutorialEnd;
	EnvironmentRules::StepWeather(Config, Frozen, Frozen.WeatherDwellSeconds + 1.f);
	TestTrue(TEXT("Locked tutorial state never steps"), Frozen.Weather == EWeatherState::Snowfall && Frozen.WeatherElapsedSeconds == 0.f);

	// The level after that continues from wherever the previous one ended.
	const FEnvironmentState Third = UEnvironmentSubsystem::ResolveInitialState(Config, FEnvironmentLevelSettings(), TOptional<FEnvironmentState>(Running), 23);
	TestTrue(TEXT("Third level continues the running state"), Third.Weather == Running.Weather && Third.RandomSeed == Running.RandomSeed);
	TestFalse(TEXT("Third level unlocked"), Third.bLocked);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
