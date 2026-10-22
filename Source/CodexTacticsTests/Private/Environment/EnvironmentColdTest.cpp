#include "Misc/AutomationTest.h"
#include "Environment/EnvironmentRules.h"
#include "Survival/ColdRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Environment phase 2: the weather and the time of day scale the squad's open-air cold (UE-only; docs/systems/environment-weather.md §4.3,
// the cold contract above FEnvironmentConfig::ColdMult* in EnvironmentTypes.h). Defaults: weather Clear 1 / Overcast 1 / Snowfall 1.25 /
// Blizzard 2.5, phase EarlyMorning 1.15 / Midday 0.9 / Afternoon 1 / Night 1.3; zone x weather up to 1, max(zone, weather) above.

#define ENV_COLD_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Environment.Cold." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace EnvironmentColdTestUtil
{
	const EWeatherState AllWeathers[] = {EWeatherState::Clear, EWeatherState::Overcast, EWeatherState::Snowfall, EWeatherState::Blizzard};

	/** The open-air accumulation before environment phase 2 (Godot parity formula), written out independently of ColdRules. */
	float LegacyStepCold(const FColdConfig& Config, float Cold, float DeltaSeconds, const FColdEnvironment& Environment, EOperativeStance Stance,
		float Fortitude)
	{
		const float Cut = FMath::Clamp(Fortitude * Config.FortitudeCutPerPoint, 0.f, Config.MaxFortitudeCut);
		const float Wind = (Environment.bElevated ? Config.ElevatedWindMultiplier : 1.f) * (Environment.bInCover ? Config.CoverWindChillMultiplier : 1.f);
		const float Rate = Environment.ZoneMultiplier * FMath::Max(0.f, Environment.LevelMultiplier) * ColdRules::GetStanceMultiplier(Config, Stance) * Wind
			* FMath::Clamp(Environment.AccumulationMultiplier, 0.f, 1.f);
		return FMath::Min(100.f, Cold + Config.AccumulationRate * (1.f - Cut) * Rate * DeltaSeconds);
	}

	/** Cold gained in one second from 0 %, standing, fortitude 0 (= the open-air rate in %/s). */
	float RatePerSecond(const FColdEnvironment& Environment)
	{
		return ColdRules::StepCold(FColdConfig(), 0.f, 1.f, Environment, EOperativeStance::Standing, 0.f);
	}
}

ENV_COLD_TEST(FEnvColdWeatherMultiplierTest, "WeatherMultipliers")
bool FEnvColdWeatherMultiplierTest::RunTest(const FString&)
{
	const FEnvironmentConfig Defaults;
	TestEqual(TEXT("Clear 1.0"), EnvironmentRules::GetColdWeatherMultiplier(Defaults, EWeatherState::Clear), 1.f, 0.0001f);
	TestEqual(TEXT("Overcast 1.0"), EnvironmentRules::GetColdWeatherMultiplier(Defaults, EWeatherState::Overcast), 1.f, 0.0001f);
	TestEqual(TEXT("Snowfall 1.25"), EnvironmentRules::GetColdWeatherMultiplier(Defaults, EWeatherState::Snowfall), 1.25f, 0.0001f);
	TestEqual(TEXT("Blizzard 2.5 (parity with the Blizzard camera zone)"), EnvironmentRules::GetColdWeatherMultiplier(Defaults, EWeatherState::Blizzard),
		2.5f, 0.0001f);

	FEnvironmentConfig Custom;
	Custom.ColdMultClear = 0.7f;
	Custom.ColdMultOvercast = 1.1f;
	Custom.ColdMultSnowfall = 1.6f;
	Custom.ColdMultBlizzard = -3.f;
	TestEqual(TEXT("custom Clear read from its own field"), EnvironmentRules::GetColdWeatherMultiplier(Custom, EWeatherState::Clear), 0.7f, 0.0001f);
	TestEqual(TEXT("custom Overcast"), EnvironmentRules::GetColdWeatherMultiplier(Custom, EWeatherState::Overcast), 1.1f, 0.0001f);
	TestEqual(TEXT("custom Snowfall"), EnvironmentRules::GetColdWeatherMultiplier(Custom, EWeatherState::Snowfall), 1.6f, 0.0001f);
	TestEqual(TEXT("negative clamps to 0"), EnvironmentRules::GetColdWeatherMultiplier(Custom, EWeatherState::Blizzard), 0.f, 0.0001f);
	return true;
}

ENV_COLD_TEST(FEnvColdPhaseMultiplierTest, "PhaseMultipliers")
bool FEnvColdPhaseMultiplierTest::RunTest(const FString&)
{
	const FEnvironmentConfig Defaults;
	TestEqual(TEXT("EarlyMorning 1.15"), EnvironmentRules::GetColdPhaseMultiplier(Defaults, ETimeOfDayPhase::EarlyMorning), 1.15f, 0.0001f);
	TestEqual(TEXT("Midday 0.9"), EnvironmentRules::GetColdPhaseMultiplier(Defaults, ETimeOfDayPhase::Midday), 0.9f, 0.0001f);
	TestEqual(TEXT("Afternoon 1.0"), EnvironmentRules::GetColdPhaseMultiplier(Defaults, ETimeOfDayPhase::Afternoon), 1.f, 0.0001f);
	TestEqual(TEXT("Night 1.3"), EnvironmentRules::GetColdPhaseMultiplier(Defaults, ETimeOfDayPhase::Night), 1.3f, 0.0001f);

	FEnvironmentConfig Custom;
	Custom.ColdMultEarlyMorning = 1.2f;
	Custom.ColdMultMidday = 0.5f;
	Custom.ColdMultAfternoon = 1.05f;
	Custom.ColdMultNight = -1.f;
	TestEqual(TEXT("custom EarlyMorning"), EnvironmentRules::GetColdPhaseMultiplier(Custom, ETimeOfDayPhase::EarlyMorning), 1.2f, 0.0001f);
	TestEqual(TEXT("custom Midday"), EnvironmentRules::GetColdPhaseMultiplier(Custom, ETimeOfDayPhase::Midday), 0.5f, 0.0001f);
	TestEqual(TEXT("custom Afternoon"), EnvironmentRules::GetColdPhaseMultiplier(Custom, ETimeOfDayPhase::Afternoon), 1.05f, 0.0001f);
	TestEqual(TEXT("negative clamps to 0"), EnvironmentRules::GetColdPhaseMultiplier(Custom, ETimeOfDayPhase::Night), 0.f, 0.0001f);

	// The phase follows the clock (EnvironmentRules::GetPhase): 23:00 is Night, 12:00 Midday.
	TestEqual(TEXT("23:00 -> Night 1.3"), EnvironmentRules::GetColdPhaseMultiplier(Defaults, EnvironmentRules::GetPhase(Defaults, 23.f)), 1.3f, 0.0001f);
	TestEqual(TEXT("12:00 -> Midday 0.9"), EnvironmentRules::GetColdPhaseMultiplier(Defaults, EnvironmentRules::GetPhase(Defaults, 12.f)), 0.9f, 0.0001f);
	return true;
}

ENV_COLD_TEST(FEnvColdBlendTest, "WeatherBlendLerp")
bool FEnvColdBlendTest::RunTest(const FString&)
{
	// Default dwell of a phase = 225 s, the blend is its last 45 s (180 .. 225).
	const FEnvironmentConfig Config;
	FEnvironmentState State;
	State.Weather = EWeatherState::Snowfall;
	State.NextWeather = EWeatherState::Blizzard;
	State.WeatherDwellSeconds = 225.f;

	State.WeatherElapsedSeconds = 0.f;
	TestEqual(TEXT("before the blend: Snowfall 1.25"), EnvironmentRules::GetBlendedColdWeatherMultiplier(Config, State), 1.25f, 0.0001f);
	State.WeatherElapsedSeconds = 180.f;
	TestEqual(TEXT("blend start: still 1.25"), EnvironmentRules::GetBlendedColdWeatherMultiplier(Config, State), 1.25f, 0.0001f);
	State.WeatherElapsedSeconds = 202.5f;
	TestEqual(TEXT("half way: 1.875"), EnvironmentRules::GetBlendedColdWeatherMultiplier(Config, State), 1.875f, 0.0001f);
	State.WeatherElapsedSeconds = 225.f;
	TestEqual(TEXT("end of the dwell: Blizzard 2.5"), EnvironmentRules::GetBlendedColdWeatherMultiplier(Config, State), 2.5f, 0.0001f);

	// Downward too: Blizzard -> Snowfall a quarter of the way.
	State.Weather = EWeatherState::Blizzard;
	State.NextWeather = EWeatherState::Snowfall;
	State.WeatherElapsedSeconds = 191.25f;
	TestEqual(TEXT("Blizzard -> Snowfall 25 %: 2.1875"), EnvironmentRules::GetBlendedColdWeatherMultiplier(Config, State), 2.1875f, 0.0001f);

	// A locked state never blends (the tutorial's Snowfall stays Snowfall).
	State.Weather = EWeatherState::Snowfall;
	State.NextWeather = EWeatherState::Blizzard;
	State.WeatherElapsedSeconds = 225.f;
	State.bLocked = true;
	TestEqual(TEXT("locked: no blend"), EnvironmentRules::GetBlendedColdWeatherMultiplier(Config, State), 1.25f, 0.0001f);
	return true;
}

ENV_COLD_TEST(FEnvColdZoneWeatherTableTest, "ZoneWeatherCombination")
bool FEnvColdZoneWeatherTableTest::RunTest(const FString&)
{
	const FEnvironmentConfig Config;
	struct FRow
	{
		const TCHAR* Name;
		float Zone;
		float Expected[4]; // Clear, Overcast, Snowfall, Blizzard
	};
	const FRow Rows[] = {
		{TEXT("closed 0"), 0.f, {0.f, 0.f, 0.f, 0.f}},
		{TEXT("shelter 0.5"), 0.5f, {0.5f, 0.5f, 0.625f, 1.25f}},
		{TEXT("standard 1"), 1.f, {1.f, 1.f, 1.25f, 2.5f}},
		{TEXT("wave cold_drain_mult 1.5"), 1.5f, {1.5f, 1.5f, 1.5f, 2.5f}},
		{TEXT("Blizzard camera zone 2.5"), 2.5f, {2.5f, 2.5f, 2.5f, 2.5f}},
	};
	for (const FRow& Row : Rows)
	{
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const EWeatherState Weather = EnvironmentColdTestUtil::AllWeathers[Index];
			const float WeatherMult = EnvironmentRules::GetColdWeatherMultiplier(Config, Weather);
			TestEqual(FString::Printf(TEXT("%s x %s"), Row.Name, *EnvironmentRules::WeatherToString(Weather)),
				ColdRules::CombineZoneWeather(Row.Zone, WeatherMult), Row.Expected[Index], 0.0001f);
		}
	}
	// Edge cases: a negative zone counts as closed, a negative weather as 0; weather 1 always returns the zone (parity).
	TestEqual(TEXT("negative zone -> 0"), ColdRules::CombineZoneWeather(-1.f, 2.5f), 0.f, 0.0001f);
	TestEqual(TEXT("negative weather, standard zone -> 0"), ColdRules::CombineZoneWeather(1.f, -2.f), 0.f, 0.0001f);
	TestEqual(TEXT("negative weather, storm zone -> zone"), ColdRules::CombineZoneWeather(2.5f, -2.f), 2.5f, 0.0001f);
	TestEqual(TEXT("storm zone below a stronger weather -> weather"), ColdRules::CombineZoneWeather(2.f, 3.f), 3.f, 0.0001f);
	for (const float Zone : {0.25f, 0.5f, 0.8f, 1.f, 1.5f, 2.5f, 4.f})
	{
		TestEqual(FString::Printf(TEXT("weather 1 keeps zone %.2f"), Zone), ColdRules::CombineZoneWeather(Zone, 1.f), Zone);
	}
	return true;
}

ENV_COLD_TEST(FEnvColdStepTest, "StepColdWeatherAndPhase")
bool FEnvColdStepTest::RunTest(const FString&)
{
	using namespace EnvironmentColdTestUtil;
	const FColdConfig Config;
	const FEnvironmentConfig EnvConfig;

	// Open ground, Blizzard at night: 1.0 x 2.5 x 1.3 = 3.25 %/s.
	FColdEnvironment Storm;
	Storm.WeatherMultiplier = EnvironmentRules::GetColdWeatherMultiplier(EnvConfig, EWeatherState::Blizzard);
	Storm.PhaseMultiplier = EnvironmentRules::GetColdPhaseMultiplier(EnvConfig, ETimeOfDayPhase::Night);
	TestEqual(TEXT("open, Blizzard night"), RatePerSecond(Storm), 3.25f, 0.001f);

	// Clear midday: 1.0 x 1.0 x 0.9.
	FColdEnvironment Calm;
	Calm.WeatherMultiplier = EnvironmentRules::GetColdWeatherMultiplier(EnvConfig, EWeatherState::Clear);
	Calm.PhaseMultiplier = EnvironmentRules::GetColdPhaseMultiplier(EnvConfig, ETimeOfDayPhase::Midday);
	TestEqual(TEXT("open, Clear midday"), RatePerSecond(Calm), 0.9f, 0.001f);

	// Commander (fortitude 15 -> 12 % cut), Snowfall early morning, 10 s: 0.88 x 1.25 x 1.15 x 10 = 12.65.
	FColdEnvironment Morning;
	Morning.WeatherMultiplier = 1.25f;
	Morning.PhaseMultiplier = 1.15f;
	TestEqual(TEXT("commander, Snowfall morning 10 s"), ColdRules::StepCold(Config, 0.f, 10.f, Morning, EOperativeStance::Standing, 15.f), 12.65f, 0.001f);

	// A Blizzard camera zone under a Blizzard at night: max(2.5, 2.5) x 1.3 = 3.25 (not 2.5 x 2.5 x 1.3).
	FColdEnvironment StormZone = Storm;
	StormZone.ZoneMultiplier = 2.5f;
	TestEqual(TEXT("Blizzard zone + Blizzard weather, night"), RatePerSecond(StormZone), 3.25f, 0.001f);
	// ... and on elevated ground the wind still doubles it, in cover halves it (other factors unchanged).
	FColdEnvironment StormRidge = StormZone;
	StormRidge.bElevated = true;
	TestEqual(TEXT("elevated x2"), RatePerSecond(StormRidge), 6.5f, 0.001f);
	StormRidge.bInCover = true;
	TestEqual(TEXT("elevated x2, cover x0.5"), RatePerSecond(StormRidge), 3.25f, 0.001f);

	// Shelter 0.5 under Snowfall at midday: 0.5 x 1.25 x 0.9 = 0.5625; the level rate 0.4 and the heat-pack buffer stack on top.
	FColdEnvironment Shelter;
	Shelter.ZoneMultiplier = 0.5f;
	Shelter.WeatherMultiplier = 1.25f;
	Shelter.PhaseMultiplier = 0.9f;
	TestEqual(TEXT("shelter, Snowfall midday"), RatePerSecond(Shelter), 0.5625f, 0.001f);
	Shelter.LevelMultiplier = 0.4f;
	Shelter.AccumulationMultiplier = 0.5f;
	TestEqual(TEXT("x level 0.4 x buffer 0.5"), RatePerSecond(Shelter), 0.1125f, 0.001f);

	// A prone operative: the stance still applies (x0.6).
	TestEqual(TEXT("prone, Blizzard night"), ColdRules::StepCold(Config, 0.f, 1.f, Storm, EOperativeStance::Prone, 0.f), 1.95f, 0.001f);

	// Clamped at 100 however hard the storm.
	TestEqual(TEXT("clamped at 100"), ColdRules::StepCold(Config, 99.f, 10.f, Storm, EOperativeStance::Standing, 0.f), 100.f);

	// Zero phase multiplier: no accumulation, no warming; negative clamps to 0.
	FColdEnvironment NoPhase;
	NoPhase.PhaseMultiplier = -1.f;
	TestEqual(TEXT("negative phase -> no accumulation"), ColdRules::StepCold(Config, 30.f, 10.f, NoPhase, EOperativeStance::Standing, 0.f), 30.f, 0.001f);
	return true;
}

ENV_COLD_TEST(FEnvColdNotScaledTest, "WarmingSprintShockNotScaled")
bool FEnvColdNotScaledTest::RunTest(const FString&)
{
	const FColdConfig Config;
	auto WithStorm = [](FColdEnvironment Environment)
	{
		Environment.WeatherMultiplier = 2.5f;
		Environment.PhaseMultiplier = 1.3f;
		return Environment;
	};

	FColdEnvironment Warm;
	Warm.bWarm = true;
	TestEqual(TEXT("warming at heat unscaled"), ColdRules::StepCold(Config, 50.f, 1.f, WithStorm(Warm), EOperativeStance::Standing, 15.f),
		ColdRules::StepCold(Config, 50.f, 1.f, Warm, EOperativeStance::Standing, 15.f), 0.0001f);
	FColdEnvironment Prep;
	Prep.bPreparation = true;
	TestEqual(TEXT("preparation unscaled"), ColdRules::StepCold(Config, 50.f, 1.f, WithStorm(Prep), EOperativeStance::Standing, 15.f),
		ColdRules::StepCold(Config, 50.f, 1.f, Prep, EOperativeStance::Standing, 15.f), 0.0001f);
	FColdEnvironment Closed;
	Closed.ZoneMultiplier = 0.f;
	// A closed bunker stays immune and warms (8 %/s x 1.15 for fortitude 15) whatever the storm outside.
	TestEqual(TEXT("closed zone warms in a Blizzard night"), ColdRules::StepCold(Config, 50.f, 1.f, WithStorm(Closed), EOperativeStance::Standing, 15.f),
		40.8f, 0.001f);
	FColdEnvironment Sprint;
	Sprint.bSprinting = true;
	TestEqual(TEXT("sprint warm-up unscaled"), ColdRules::StepCold(Config, 50.f, 1.f, WithStorm(Sprint), EOperativeStance::Standing, 15.f),
		ColdRules::StepCold(Config, 50.f, 1.f, Sprint, EOperativeStance::Standing, 15.f), 0.0001f);
	// Scripted shocks never see the environment at all (ApplyColdShock takes no FColdEnvironment): +20, at least 42.
	TestEqual(TEXT("cold shock unchanged"), ColdRules::ApplyColdShock(5.f, 20.f, 42.f), 42.f, 0.0001f);
	return true;
}

ENV_COLD_TEST(FEnvColdParityTest, "ParityWithoutEnvironment")
bool FEnvColdParityTest::RunTest(const FString&)
{
	// With WeatherMultiplier = PhaseMultiplier = 1 (no subsystem, or a locked level) every step equals the pre-phase-2 formula exactly.
	using namespace EnvironmentColdTestUtil;
	const FColdConfig Config;
	const float Zones[] = {0.25f, 0.5f, 1.f, 1.5f, 2.5f};
	const float Levels[] = {0.4f, 0.8f, 1.f};
	const EOperativeStance Stances[] = {EOperativeStance::Standing, EOperativeStance::Crouching, EOperativeStance::Prone};
	const float Fortitudes[] = {0.f, 15.f, 25.f, 100.f};
	int32 Cases = 0;
	for (const float Zone : Zones)
	{
		for (const float Level : Levels)
		{
			for (int32 Flags = 0; Flags < 8; ++Flags)
			{
				FColdEnvironment Environment;
				Environment.ZoneMultiplier = Zone;
				Environment.LevelMultiplier = Level;
				Environment.bElevated = (Flags & 1) != 0;
				Environment.bInCover = (Flags & 2) != 0;
				Environment.AccumulationMultiplier = (Flags & 4) != 0 ? 0.5f : 1.f;
				if (Environment.WeatherMultiplier != 1.f || Environment.PhaseMultiplier != 1.f)
				{
					AddError(TEXT("FColdEnvironment defaults must be weather 1 / phase 1"));
					return false;
				}
				for (const EOperativeStance Stance : Stances)
				{
					for (const float Fortitude : Fortitudes)
					{
						const float Expected = LegacyStepCold(Config, 12.f, 3.f, Environment, Stance, Fortitude);
						const float Actual = ColdRules::StepCold(Config, 12.f, 3.f, Environment, Stance, Fortitude);
						if (!FMath::IsNearlyEqual(Actual, Expected, 1.e-5f))
						{
							AddError(FString::Printf(TEXT("parity: zone %.2f level %.2f flags %d stance %d fortitude %.0f: %.6f != %.6f"), Zone, Level,
								Flags, static_cast<int32>(Stance), Fortitude, Actual, Expected));
						}
						++Cases;
					}
				}
			}
		}
	}
	TestEqual(TEXT("cases checked"), Cases, 5 * 3 * 8 * 3 * 4);
	return true;
}

ENV_COLD_TEST(FEnvColdLockedLevelTest, "LockedLevelRule")
bool FEnvColdLockedLevelTest::RunTest(const FString&)
{
	FEnvironmentConfig Config;
	TestFalse(TEXT("default: off on locked levels"), Config.bColdOnLockedLevels);
	TestTrue(TEXT("a running level always applies"), EnvironmentRules::ShouldApplyCold(Config, false));
	TestFalse(TEXT("the locked Demo 01 tutorial keeps its tuned cold"), EnvironmentRules::ShouldApplyCold(Config, true));
	Config.bColdOnLockedLevels = true;
	TestTrue(TEXT("opt-in: locked levels too"), EnvironmentRules::ShouldApplyCold(Config, true));
	TestTrue(TEXT("opt-in: running levels unchanged"), EnvironmentRules::ShouldApplyCold(Config, false));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
