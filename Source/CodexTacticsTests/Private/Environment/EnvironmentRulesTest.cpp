#include "Misc/AutomationTest.h"
#include "Math/RandomStream.h"
#include "Environment/EnvironmentRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Day cycle + procedural weather ladder (UE-only; docs/systems/environment-weather.md, user decisions 2026-10-22).
// Defaults = FEnvironmentConfig: phases 05 / 10 / 14 / 18, 300 real s each, dwell 75 %, blend 45 s. Every RNG test uses an explicit seed.

#define ENV_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Environment.Rules." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace EnvironmentRulesTestUtil
{
	const EWeatherState AllWeathers[] = {EWeatherState::Clear, EWeatherState::Overcast, EWeatherState::Snowfall, EWeatherState::Blizzard};
	const ETimeOfDayPhase AllPhases[] = {ETimeOfDayPhase::EarlyMorning, ETimeOfDayPhase::Midday, ETimeOfDayPhase::Afternoon, ETimeOfDayPhase::Night};

	bool SameState(const FEnvironmentState& A, const FEnvironmentState& B)
	{
		return A.Weather == B.Weather && A.NextWeather == B.NextWeather && A.RandomSeed == B.RandomSeed
			&& FMath::IsNearlyEqual(A.WeatherElapsedSeconds, B.WeatherElapsedSeconds, 0.001f)
			&& FMath::IsNearlyEqual(A.WeatherDwellSeconds, B.WeatherDwellSeconds, 0.001f);
	}
}

ENV_TEST(FEnvPhaseFromHourTest, "PhaseFromHourBoundariesAndMidnightWrap")
bool FEnvPhaseFromHourTest::RunTest(const FString&)
{
	const FEnvironmentConfig Config;
	struct FCase
	{
		float Hour;
		ETimeOfDayPhase Expected;
	};
	const FCase Cases[] = {
		{5.f, ETimeOfDayPhase::EarlyMorning}, {4.99f, ETimeOfDayPhase::Night}, {9.99f, ETimeOfDayPhase::EarlyMorning},
		{10.f, ETimeOfDayPhase::Midday}, {13.99f, ETimeOfDayPhase::Midday}, {14.f, ETimeOfDayPhase::Afternoon},
		{17.99f, ETimeOfDayPhase::Afternoon}, {18.f, ETimeOfDayPhase::Night}, {23.99f, ETimeOfDayPhase::Night},
		{0.f, ETimeOfDayPhase::Night}, {2.5f, ETimeOfDayPhase::Night},
		// Out-of-range hours wrap: 24 = midnight, -1 = 23:00, 29 = 05:00.
		{24.f, ETimeOfDayPhase::Night}, {-1.f, ETimeOfDayPhase::Night}, {29.f, ETimeOfDayPhase::EarlyMorning}, {36.f, ETimeOfDayPhase::Midday},
	};
	for (const FCase& Case : Cases)
	{
		TestEqual(FString::Printf(TEXT("hour %.2f"), Case.Hour), EnvironmentRules::PhaseToString(EnvironmentRules::GetPhase(Config, Case.Hour)),
			EnvironmentRules::PhaseToString(Case.Expected));
	}
	// Fixed order: EarlyMorning -> Midday -> Afternoon -> Night -> EarlyMorning.
	TestTrue(TEXT("EM -> Midday"), EnvironmentRules::GetNextPhase(ETimeOfDayPhase::EarlyMorning) == ETimeOfDayPhase::Midday);
	TestTrue(TEXT("Midday -> Afternoon"), EnvironmentRules::GetNextPhase(ETimeOfDayPhase::Midday) == ETimeOfDayPhase::Afternoon);
	TestTrue(TEXT("Afternoon -> Night"), EnvironmentRules::GetNextPhase(ETimeOfDayPhase::Afternoon) == ETimeOfDayPhase::Night);
	TestTrue(TEXT("Night -> EM"), EnvironmentRules::GetNextPhase(ETimeOfDayPhase::Night) == ETimeOfDayPhase::EarlyMorning);
	// Start hours and spans: 5 + 4 + 4 + 11 = 24.
	TestEqual(TEXT("EM starts 05"), EnvironmentRules::GetPhaseStartHour(Config, ETimeOfDayPhase::EarlyMorning), 5.f);
	TestEqual(TEXT("Night starts 18"), EnvironmentRules::GetPhaseStartHour(Config, ETimeOfDayPhase::Night), 18.f);
	TestEqual(TEXT("EM spans 5 h"), EnvironmentRules::GetPhaseGameHours(Config, ETimeOfDayPhase::EarlyMorning), 5.f);
	TestEqual(TEXT("Midday spans 4 h"), EnvironmentRules::GetPhaseGameHours(Config, ETimeOfDayPhase::Midday), 4.f);
	TestEqual(TEXT("Afternoon spans 4 h"), EnvironmentRules::GetPhaseGameHours(Config, ETimeOfDayPhase::Afternoon), 4.f);
	TestEqual(TEXT("Night spans 11 h over midnight"), EnvironmentRules::GetPhaseGameHours(Config, ETimeOfDayPhase::Night), 11.f);
	float Sum = 0.f;
	for (const ETimeOfDayPhase Phase : EnvironmentRulesTestUtil::AllPhases)
	{
		Sum += EnvironmentRules::GetPhaseGameHours(Config, Phase);
	}
	TestEqual(TEXT("spans cover the day"), Sum, 24.f, 0.0001f);
	return true;
}

ENV_TEST(FEnvClockRateTest, "PerPhaseClockRate")
bool FEnvClockRateTest::RunTest(const FString&)
{
	const FEnvironmentConfig Config;
	TestEqual(TEXT("day = 1200 real s"), EnvironmentRules::GetDayRealSeconds(Config), 1200.f);
	for (const ETimeOfDayPhase Phase : EnvironmentRulesTestUtil::AllPhases)
	{
		TestEqual(TEXT("each phase 300 real s"), EnvironmentRules::GetPhaseRealSeconds(Config, Phase), 300.f);
	}
	TestEqual(TEXT("Night: 11 h / 300 s"), EnvironmentRules::GetClockRate(Config, ETimeOfDayPhase::Night), 11.f / 300.f, 1.e-6f);
	TestEqual(TEXT("EM: 5 h / 300 s"), EnvironmentRules::GetClockRate(Config, ETimeOfDayPhase::EarlyMorning), 5.f / 300.f, 1.e-6f);
	TestEqual(TEXT("Midday: 4 h / 300 s"), EnvironmentRules::GetClockRate(Config, ETimeOfDayPhase::Midday), 4.f / 300.f, 1.e-6f);
	// The long night still lasts 5 real minutes; half of it is 5.5 game hours.
	TestEqual(TEXT("18:00 + 150 s = 23:30"), EnvironmentRules::AdvanceClock(Config, 18.f, 150.f), 23.5f, 0.001f);
	TestEqual(TEXT("18:00 + 300 s = 05:00"), EnvironmentRules::AdvanceClock(Config, 18.f, 300.f), 5.f, 0.001f);
	// Midnight wrap inside the night: 23:00 + 60 s = 01:12.
	TestEqual(TEXT("23:00 + 60 s = 01:12"), EnvironmentRules::AdvanceClock(Config, 23.f, 60.f), 1.2f, 0.001f);
	TestEqual(TEXT("10:00 + 75 s = 11:00 (Midday 4 h / 300 s)"), EnvironmentRules::AdvanceClock(Config, 10.f, 75.f), 11.f, 0.001f);
	// A custom real duration changes only that phase.
	FEnvironmentConfig LongNight = Config;
	LongNight.NightRealSeconds = 600.f;
	TestEqual(TEXT("600 s night: 18:00 + 300 s = 23:30"), EnvironmentRules::AdvanceClock(LongNight, 18.f, 300.f), 23.5f, 0.001f);
	TestEqual(TEXT("600 s night: day 1500 s"), EnvironmentRules::GetDayRealSeconds(LongNight), 1500.f);
	return true;
}

ENV_TEST(FEnvFullDayTest, "FullDayReturnsToSameHour")
bool FEnvFullDayTest::RunTest(const FString&)
{
	const FEnvironmentConfig Config;
	const float Hours[] = {0.f, 5.f, 6.f, 12.5f, 18.f, 23.9f};
	for (const float Hour : Hours)
	{
		TestEqual(FString::Printf(TEXT("%.1f + 1200 s"), Hour), EnvironmentRules::AdvanceClock(Config, Hour, 1200.f), Hour, 0.001f);
		TestEqual(FString::Printf(TEXT("%.1f + 2400 s"), Hour), EnvironmentRules::AdvanceClock(Config, Hour, 2400.f), Hour, 0.001f);
	}
	// The same day walked in 1 s frames ends where it started (and passes every phase in order).
	float Hour = 6.f;
	ETimeOfDayPhase Phase = EnvironmentRules::GetPhase(Config, Hour);
	int32 PhaseChanges = 0;
	bool bOrderKept = true;
	for (int32 Second = 0; Second < 1200; ++Second)
	{
		Hour = EnvironmentRules::AdvanceClock(Config, Hour, 1.f);
		const ETimeOfDayPhase Now = EnvironmentRules::GetPhase(Config, Hour);
		if (Now != Phase)
		{
			bOrderKept &= Now == EnvironmentRules::GetNextPhase(Phase);
			Phase = Now;
			++PhaseChanges;
		}
		bOrderKept &= Hour >= 0.f && Hour < 24.f;
	}
	TestEqual(TEXT("1200 x 1 s: back at 06:00"), Hour, 6.f, 0.01f);
	TestEqual(TEXT("four phase changes in a day"), PhaseChanges, 4);
	TestTrue(TEXT("phases in order, hour in [0, 24)"), bOrderKept);
	return true;
}

ENV_TEST(FEnvAdvanceAcrossBoundariesTest, "AdvanceClockAcrossSeveralBoundaries")
bool FEnvAdvanceAcrossBoundariesTest::RunTest(const FString&)
{
	const FEnvironmentConfig Config;
	// 06:00: 4 h of EM left = 240 s; + a whole Midday 300 s; + 150 s of Afternoon = 2 h -> 16:00.
	TestEqual(TEXT("06:00 + 690 s = 16:00 (2 boundaries)"), EnvironmentRules::AdvanceClock(Config, 6.f, 690.f), 16.f, 0.001f);
	// 16:00: 2 h of Afternoon = 150 s; a whole Night 300 s (over midnight); 60 s of EM = 1 h -> 06:00.
	TestEqual(TEXT("16:00 + 510 s = 06:00 (2 boundaries + midnight)"), EnvironmentRules::AdvanceClock(Config, 16.f, 510.f), 6.f, 0.001f);
	// Three boundaries: 09:00 (60 s of EM left) + 300 + 300 + 150 s of night -> 23:30.
	TestEqual(TEXT("09:00 + 810 s = 23:30 (3 boundaries)"), EnvironmentRules::AdvanceClock(Config, 9.f, 810.f), 23.5f, 0.001f);
	// One big step equals the same time split into pieces.
	const float Split = EnvironmentRules::AdvanceClock(Config, EnvironmentRules::AdvanceClock(Config, 9.f, 400.f), 410.f);
	TestEqual(TEXT("split steps agree"), Split, 23.5f, 0.001f);
	// Exactly on a boundary lands on the next phase.
	TestTrue(TEXT("13:00 + 75 s = Afternoon"), EnvironmentRules::GetPhase(Config, EnvironmentRules::AdvanceClock(Config, 13.f, 75.f)) == ETimeOfDayPhase::Afternoon);
	// More than a day: whole days drop out.
	TestEqual(TEXT("06:00 + 1200 + 690 s = 16:00"), EnvironmentRules::AdvanceClock(Config, 6.f, 1890.f), 16.f, 0.001f);
	// No time, negative time: the hour (wrapped) is kept.
	TestEqual(TEXT("zero delta"), EnvironmentRules::AdvanceClock(Config, 7.25f, 0.f), 7.25f);
	TestEqual(TEXT("negative delta"), EnvironmentRules::AdvanceClock(Config, 7.25f, -50.f), 7.25f);
	TestEqual(TEXT("30:00 wraps to 06:00"), EnvironmentRules::AdvanceClock(Config, 30.f, 0.f), 6.f, 0.0001f);
	return true;
}

ENV_TEST(FEnvShouldAdvanceTest, "FreezeInPauseTurnBasedAndPausedAI")
bool FEnvShouldAdvanceTest::RunTest(const FString&)
{
	TestTrue(TEXT("exploration: runs"), EnvironmentRules::ShouldAdvance(ECodexCombatMode::None, false));
	TestTrue(TEXT("real-time combat: runs"), EnvironmentRules::ShouldAdvance(ECodexCombatMode::RealTime, false));
	TestFalse(TEXT("tactical pause: frozen"), EnvironmentRules::ShouldAdvance(ECodexCombatMode::TacticalPause, false));
	TestFalse(TEXT("turn-based: frozen for the whole fight (decision 8)"), EnvironmentRules::ShouldAdvance(ECodexCombatMode::TurnBased, false));
	TestFalse(TEXT("world AI paused (dialogue / cutscene / menu): frozen"), EnvironmentRules::ShouldAdvance(ECodexCombatMode::None, true));
	TestFalse(TEXT("real-time but AI paused: frozen"), EnvironmentRules::ShouldAdvance(ECodexCombatMode::RealTime, true));
	return true;
}

ENV_TEST(FEnvLadderTest, "WeatherLadderOnlyNeighbours")
bool FEnvLadderTest::RunTest(const FString&)
{
	using EnvironmentRules::AreNeighbours;
	TestTrue(TEXT("Clear-Overcast"), AreNeighbours(EWeatherState::Clear, EWeatherState::Overcast));
	TestTrue(TEXT("Snowfall-Overcast"), AreNeighbours(EWeatherState::Snowfall, EWeatherState::Overcast));
	TestTrue(TEXT("Snowfall-Blizzard"), AreNeighbours(EWeatherState::Snowfall, EWeatherState::Blizzard));
	TestFalse(TEXT("Clear-Snowfall"), AreNeighbours(EWeatherState::Clear, EWeatherState::Snowfall));
	TestFalse(TEXT("Blizzard-Clear"), AreNeighbours(EWeatherState::Blizzard, EWeatherState::Clear));
	TestFalse(TEXT("Overcast-Overcast"), AreNeighbours(EWeatherState::Overcast, EWeatherState::Overcast));

	const FEnvironmentConfig Config;
	FRandomStream Random(424242);
	for (const EWeatherState From : EnvironmentRulesTestUtil::AllWeathers)
	{
		int32 Up = 0;
		int32 Down = 0;
		bool bAllNeighbours = true;
		for (int32 Roll = 0; Roll < 1000; ++Roll)
		{
			const EWeatherState Next = EnvironmentRules::PickNextWeather(Config, From, Random);
			bAllNeighbours &= AreNeighbours(From, Next);
			(static_cast<int32>(Next) > static_cast<int32>(From) ? Up : Down)++;
		}
		const FString Name = EnvironmentRules::WeatherToString(From);
		TestTrue(FString::Printf(TEXT("%s: 1000 rolls, all neighbours"), *Name), bAllNeighbours);
		if (From == EWeatherState::Clear)
		{
			TestEqual(TEXT("Clear: forced up"), Up, 1000);
		}
		else if (From == EWeatherState::Blizzard)
		{
			TestEqual(TEXT("Blizzard: forced down"), Down, 1000);
		}
		else
		{
			// 50 / 50 by default: both directions show up plenty.
			TestTrue(FString::Printf(TEXT("%s: both directions (%d up / %d down)"), *Name, Up, Down), Up > 350 && Down > 350);
		}
	}

	// Step-up chance extremes.
	FEnvironmentConfig AlwaysDown = Config;
	AlwaysDown.OvercastStepUpChance = 0.f;
	AlwaysDown.SnowfallStepUpChance = 0.f;
	FEnvironmentConfig AlwaysUp = Config;
	AlwaysUp.OvercastStepUpChance = 1.f;
	AlwaysUp.SnowfallStepUpChance = 1.f;
	bool bDown = true;
	bool bUp = true;
	for (int32 Roll = 0; Roll < 1000; ++Roll)
	{
		bDown &= EnvironmentRules::PickNextWeather(AlwaysDown, EWeatherState::Overcast, Random) == EWeatherState::Clear;
		bDown &= EnvironmentRules::PickNextWeather(AlwaysDown, EWeatherState::Snowfall, Random) == EWeatherState::Overcast;
		bUp &= EnvironmentRules::PickNextWeather(AlwaysUp, EWeatherState::Overcast, Random) == EWeatherState::Snowfall;
		bUp &= EnvironmentRules::PickNextWeather(AlwaysUp, EWeatherState::Snowfall, Random) == EWeatherState::Blizzard;
	}
	TestTrue(TEXT("chance 0: always down"), bDown);
	TestTrue(TEXT("chance 1: always up"), bUp);
	return true;
}

ENV_TEST(FEnvStartWeatherTest, "StartWeatherWeights")
bool FEnvStartWeatherTest::RunTest(const FString&)
{
	FRandomStream Random(9001);
	// All weight on one state: always that state.
	for (const EWeatherState Only : EnvironmentRulesTestUtil::AllWeathers)
	{
		FEnvironmentConfig Config;
		Config.StartWeightClear = Only == EWeatherState::Clear ? 1.f : 0.f;
		Config.StartWeightOvercast = Only == EWeatherState::Overcast ? 1.f : 0.f;
		Config.StartWeightSnowfall = Only == EWeatherState::Snowfall ? 1.f : 0.f;
		Config.StartWeightBlizzard = Only == EWeatherState::Blizzard ? 1.f : 0.f;
		bool bAlways = true;
		for (int32 Roll = 0; Roll < 500; ++Roll)
		{
			bAlways &= EnvironmentRules::PickStartWeather(Config, Random) == Only;
		}
		TestTrue(FString::Printf(TEXT("only %s"), *EnvironmentRules::WeatherToString(Only)), bAlways);
	}
	// All zero (or negative): Overcast.
	FEnvironmentConfig Zero;
	Zero.StartWeightClear = Zero.StartWeightOvercast = Zero.StartWeightSnowfall = 0.f;
	Zero.StartWeightBlizzard = -1.f;
	TestTrue(TEXT("all zero -> Overcast"), EnvironmentRules::PickStartWeather(Zero, Random) == EWeatherState::Overcast);
	// Default weights: every state can start a run, roughly by weight (0.3 / 0.3 / 0.25 / 0.15).
	const FEnvironmentConfig Defaults;
	int32 Counts[4] = {0, 0, 0, 0};
	for (int32 Roll = 0; Roll < 4000; ++Roll)
	{
		++Counts[static_cast<int32>(EnvironmentRules::PickStartWeather(Defaults, Random))];
	}
	TestTrue(FString::Printf(TEXT("Clear ~30 %% (%d / 4000)"), Counts[0]), Counts[0] > 1000 && Counts[0] < 1400);
	TestTrue(FString::Printf(TEXT("Overcast ~30 %% (%d / 4000)"), Counts[1]), Counts[1] > 1000 && Counts[1] < 1400);
	TestTrue(FString::Printf(TEXT("Snowfall ~25 %% (%d / 4000)"), Counts[2]), Counts[2] > 800 && Counts[2] < 1200);
	TestTrue(FString::Printf(TEXT("Blizzard ~15 %% (%d / 4000)"), Counts[3]), Counts[3] > 400 && Counts[3] < 800);
	return true;
}

ENV_TEST(FEnvDwellBlendTest, "DwellAndTransitionBlend")
bool FEnvDwellBlendTest::RunTest(const FString&)
{
	const FEnvironmentConfig Config;
	for (const ETimeOfDayPhase Phase : EnvironmentRulesTestUtil::AllPhases)
	{
		TestEqual(TEXT("dwell = 0.75 x 300 = 225 s (3:45)"), EnvironmentRules::GetWeatherDwellSeconds(Config, Phase), 225.f, 0.001f);
	}
	TestEqual(TEXT("transition 45 s"), EnvironmentRules::GetTransitionSeconds(Config, 225.f), 45.f);
	TestEqual(TEXT("transition clamped to a short dwell"), EnvironmentRules::GetTransitionSeconds(Config, 30.f), 30.f);
	TestEqual(TEXT("no dwell, no transition"), EnvironmentRules::GetTransitionSeconds(Config, -5.f), 0.f);

	FEnvironmentState State;
	State.WeatherDwellSeconds = 225.f;
	const auto BlendAt = [&Config, &State](float Elapsed)
	{
		State.WeatherElapsedSeconds = Elapsed;
		return EnvironmentRules::GetTransitionBlend(Config, State);
	};
	TestEqual(TEXT("0 s: 0"), BlendAt(0.f), 0.f);
	TestEqual(TEXT("179.9 s: 0"), BlendAt(179.9f), 0.f);
	TestEqual(TEXT("180 s: blend starts at 0"), BlendAt(180.f), 0.f);
	TestEqual(TEXT("202.5 s: 0.5"), BlendAt(202.5f), 0.5f, 0.0001f);
	TestEqual(TEXT("225 s: 1"), BlendAt(225.f), 1.f, 0.0001f);
	TestEqual(TEXT("past the dwell: clamped to 1"), BlendAt(300.f), 1.f);
	// A locked state shows its weather fully.
	State.bLocked = true;
	TestEqual(TEXT("locked: 0"), BlendAt(220.f), 0.f);
	// No transition: a hard cut at the end of the dwell.
	FEnvironmentConfig HardCut = Config;
	HardCut.WeatherTransitionSeconds = 0.f;
	State.bLocked = false;
	State.WeatherElapsedSeconds = 224.f;
	TestEqual(TEXT("hard cut before the end: 0"), EnvironmentRules::GetTransitionBlend(HardCut, State), 0.f);
	State.WeatherElapsedSeconds = 225.f;
	TestEqual(TEXT("hard cut at the end: 1"), EnvironmentRules::GetTransitionBlend(HardCut, State), 1.f);
	return true;
}

ENV_TEST(FEnvInitialStateTest, "InitialState")
bool FEnvInitialStateTest::RunTest(const FString&)
{
	const FEnvironmentConfig Config;
	const FEnvironmentState State = EnvironmentRules::MakeInitialState(Config, 30.f, EWeatherState::Snowfall, 1234);
	TestEqual(TEXT("hour wrapped"), State.Hour, 6.f, 0.0001f);
	TestTrue(TEXT("weather as given"), State.Weather == EWeatherState::Snowfall);
	TestTrue(TEXT("next is a neighbour"), EnvironmentRules::AreNeighbours(State.Weather, State.NextWeather));
	TestEqual(TEXT("elapsed 0"), State.WeatherElapsedSeconds, 0.f);
	TestEqual(TEXT("dwell 225"), State.WeatherDwellSeconds, 225.f, 0.001f);
	TestFalse(TEXT("not locked"), State.bLocked);
	TestNotEqual(TEXT("RNG state stored (advanced by the roll)"), State.RandomSeed, 1234);
	const FEnvironmentState Again = EnvironmentRules::MakeInitialState(Config, 30.f, EWeatherState::Snowfall, 1234);
	TestTrue(TEXT("same seed, same state"), EnvironmentRulesTestUtil::SameState(State, Again));
	// The dwell comes from the phase of the start hour.
	FEnvironmentConfig LongNight = Config;
	LongNight.NightRealSeconds = 400.f;
	TestEqual(TEXT("night start: 0.75 x 400"), EnvironmentRules::MakeInitialState(LongNight, 20.f, EWeatherState::Clear, 7).WeatherDwellSeconds, 300.f, 0.001f);
	TestEqual(TEXT("morning start: 0.75 x 300"), EnvironmentRules::MakeInitialState(LongNight, 6.f, EWeatherState::Clear, 7).WeatherDwellSeconds, 225.f, 0.001f);
	TestTrue(TEXT("Clear: next forced Overcast"), EnvironmentRules::MakeInitialState(Config, 6.f, EWeatherState::Clear, 7).NextWeather == EWeatherState::Overcast);
	return true;
}

ENV_TEST(FEnvStepChangeTest, "StepWeatherChangesAtDwellEndAndCarriesLeftover")
bool FEnvStepChangeTest::RunTest(const FString&)
{
	const FEnvironmentConfig Config;
	FEnvironmentState State = EnvironmentRules::MakeInitialState(Config, 6.f, EWeatherState::Clear, 12345);
	TestTrue(TEXT("Clear -> Overcast next"), State.NextWeather == EWeatherState::Overcast);

	FWeatherStepResult Step = EnvironmentRules::StepWeather(Config, State, 100.f);
	TestTrue(TEXT("100 s: nothing yet"), !Step.bTransitionStarted && !Step.bWeatherChanged && State.Weather == EWeatherState::Clear);
	TestEqual(TEXT("elapsed 100"), State.WeatherElapsedSeconds, 100.f);
	Step = EnvironmentRules::StepWeather(Config, State, 100.f);
	TestTrue(TEXT("200 s: the blend started (180 s), weather unchanged"), Step.bTransitionStarted && !Step.bWeatherChanged && State.Weather == EWeatherState::Clear);
	Step = EnvironmentRules::StepWeather(Config, State, 24.f);
	TestTrue(TEXT("224 s: still Clear"), !Step.bTransitionStarted && !Step.bWeatherChanged && State.Weather == EWeatherState::Clear);
	Step = EnvironmentRules::StepWeather(Config, State, 1.f);
	TestTrue(TEXT("225 s: changed"), Step.bWeatherChanged);
	TestTrue(TEXT("Clear -> Overcast"), Step.OldWeather == EWeatherState::Clear && State.Weather == EWeatherState::Overcast);
	TestTrue(TEXT("new next is a neighbour"), EnvironmentRules::AreNeighbours(State.Weather, State.NextWeather));
	TestEqual(TEXT("new dwell starts at 0"), State.WeatherElapsedSeconds, 0.f);
	TestEqual(TEXT("new dwell 225"), State.WeatherDwellSeconds, 225.f, 0.001f);

	// Leftover seconds carry into the next dwell.
	FEnvironmentState Carry = EnvironmentRules::MakeInitialState(Config, 6.f, EWeatherState::Blizzard, 99);
	Step = EnvironmentRules::StepWeather(Config, Carry, 230.f);
	TestTrue(TEXT("230 s: Blizzard -> Snowfall"), Step.bWeatherChanged && Step.bTransitionStarted && Step.OldWeather == EWeatherState::Blizzard && Carry.Weather == EWeatherState::Snowfall);
	TestEqual(TEXT("5 s carried"), Carry.WeatherElapsedSeconds, 5.f, 0.0001f);

	// The new dwell follows the phase of State.Hour at the change (the clock moved into a longer night meanwhile).
	FEnvironmentConfig LongNight = Config;
	LongNight.NightRealSeconds = 400.f;
	FEnvironmentState Night = EnvironmentRules::MakeInitialState(LongNight, 6.f, EWeatherState::Overcast, 5);
	Night.Hour = 20.f;
	EnvironmentRules::StepWeather(LongNight, Night, 225.f);
	TestEqual(TEXT("new dwell 0.75 x 400 at night"), Night.WeatherDwellSeconds, 300.f, 0.001f);

	// Zero / negative delta: nothing.
	const FEnvironmentState Before = State;
	Step = EnvironmentRules::StepWeather(Config, State, 0.f);
	Step = EnvironmentRules::StepWeather(Config, State, -10.f);
	TestTrue(TEXT("no time, no change"), !Step.bWeatherChanged && EnvironmentRulesTestUtil::SameState(Before, State));
	return true;
}

ENV_TEST(FEnvStepDeterminismTest, "StepWeatherDeterministicBySeed")
bool FEnvStepDeterminismTest::RunTest(const FString&)
{
	const FEnvironmentConfig Config;
	FEnvironmentState A = EnvironmentRules::MakeInitialState(Config, 6.f, EWeatherState::Overcast, 2026);
	FEnvironmentState B = EnvironmentRules::MakeInitialState(Config, 6.f, EWeatherState::Overcast, 2026);
	bool bSame = true;
	int32 Changes = 0;
	for (int32 Step = 0; Step < 400; ++Step)
	{
		const float Delta = 7.5f + static_cast<float>(Step % 5);
		const FWeatherStepResult RA = EnvironmentRules::StepWeather(Config, A, Delta);
		const FWeatherStepResult RB = EnvironmentRules::StepWeather(Config, B, Delta);
		bSame &= RA.bWeatherChanged == RB.bWeatherChanged && RA.OldWeather == RB.OldWeather && EnvironmentRulesTestUtil::SameState(A, B);
		Changes += RA.bWeatherChanged ? 1 : 0;
	}
	TestTrue(TEXT("same seed, same sequence"), bSame);
	TestTrue(FString::Printf(TEXT("the sequence moved (%d changes)"), Changes), Changes >= 10);

	// A saved state (RandomSeed written back) continues exactly like the original.
	FEnvironmentState Saved = A;
	EnvironmentRules::StepWeather(Config, A, 1000.f);
	EnvironmentRules::StepWeather(Config, Saved, 1000.f);
	TestTrue(TEXT("a copy (save / carry-over) continues the same sequence"), EnvironmentRulesTestUtil::SameState(A, Saved));
	return true;
}

ENV_TEST(FEnvBigStepTest, "BigStepMatchesSmallStepsAndNeverSkipsARung")
bool FEnvBigStepTest::RunTest(const FString&)
{
	const FEnvironmentConfig Config;
	const int32 Seeds[] = {777, 31337, 5};
	const EWeatherState Starts[] = {EWeatherState::Overcast, EWeatherState::Clear, EWeatherState::Blizzard};
	for (int32 Index = 0; Index < static_cast<int32>(UE_ARRAY_COUNT(Seeds)); ++Index)
	{
		FEnvironmentState Small = EnvironmentRules::MakeInitialState(Config, 6.f, Starts[Index], Seeds[Index]);
		FEnvironmentState Big = Small;
		bool bNeighbourSteps = true;
		int32 SmallChanges = 0;
		for (int32 Second = 0; Second < 2000; ++Second)
		{
			const EWeatherState Prev = Small.Weather;
			const FWeatherStepResult Result = EnvironmentRules::StepWeather(Config, Small, 1.f);
			if (Result.bWeatherChanged)
			{
				++SmallChanges;
				bNeighbourSteps &= EnvironmentRules::AreNeighbours(Prev, Small.Weather) && Result.OldWeather == Prev;
			}
		}
		const EWeatherState BigBefore = Big.Weather;
		const FWeatherStepResult BigResult = EnvironmentRules::StepWeather(Config, Big, 2000.f);
		const FString Label = FString::Printf(TEXT("seed %d"), Seeds[Index]);
		TestTrue(Label + TEXT(": 1 s steps only ever move one rung"), bNeighbourSteps);
		// 2000 s / 225 s = 8 changes, 200 s into the 9th dwell.
		TestEqual(Label + TEXT(": 8 changes"), SmallChanges, 8);
		TestEqual(Label + TEXT(": 200 s into the dwell"), Big.WeatherElapsedSeconds, 200.f, 0.001f);
		TestTrue(Label + TEXT(": one 2000 s step = 2000 x 1 s"), EnvironmentRulesTestUtil::SameState(Small, Big));
		TestTrue(Label + TEXT(": big step reports a change"), BigResult.bWeatherChanged && BigResult.bTransitionStarted);
		TestTrue(Label + TEXT(": OldWeather -> Weather is one rung"), EnvironmentRules::AreNeighbours(BigResult.OldWeather, Big.Weather));
		TestTrue(Label + TEXT(": next is a neighbour"), EnvironmentRules::AreNeighbours(Big.Weather, Big.NextWeather));
		// After an even number of one-rung moves the ladder parity is unchanged.
		TestEqual(Label + TEXT(": ladder parity after 8 moves"), (static_cast<int32>(Big.Weather) - static_cast<int32>(BigBefore)) % 2, 0);
	}
	return true;
}

ENV_TEST(FEnvLockedTest, "LockedStateNeverChanges")
bool FEnvLockedTest::RunTest(const FString&)
{
	const FEnvironmentConfig Config;
	FEnvironmentState State = EnvironmentRules::MakeInitialState(Config, 22.f, EWeatherState::Snowfall, 17);
	State.bLocked = true; // Demo 01 tutorial: Night + Snowfall.
	State.WeatherElapsedSeconds = 220.f;
	const FEnvironmentState Before = State;
	const FWeatherStepResult Result = EnvironmentRules::StepWeather(Config, State, 10000.f);
	TestFalse(TEXT("no change"), Result.bWeatherChanged);
	TestFalse(TEXT("no blend"), Result.bTransitionStarted);
	TestTrue(TEXT("state untouched"), EnvironmentRulesTestUtil::SameState(Before, State) && State.Hour == Before.Hour && State.bLocked);
	TestEqual(TEXT("locked blend 0"), EnvironmentRules::GetTransitionBlend(Config, State), 0.f);
	return true;
}

ENV_TEST(FEnvStringsTest, "StringRoundTrips")
bool FEnvStringsTest::RunTest(const FString&)
{
	for (const EWeatherState Weather : EnvironmentRulesTestUtil::AllWeathers)
	{
		EWeatherState Back = EWeatherState::Clear;
		const FString Text = EnvironmentRules::WeatherToString(Weather);
		TestTrue(FString::Printf(TEXT("%s round-trips"), *Text), EnvironmentRules::WeatherFromString(Text, Back) && Back == Weather);
		TestTrue(FString::Printf(TEXT("%s lower case"), *Text), EnvironmentRules::WeatherFromString(Text.ToLower(), Back) && Back == Weather);
		TestTrue(FString::Printf(TEXT("%s upper case"), *Text), EnvironmentRules::WeatherFromString(Text.ToUpper(), Back) && Back == Weather);
	}
	TestEqual(TEXT("Clear"), EnvironmentRules::WeatherToString(EWeatherState::Clear), FString(TEXT("Clear")));
	TestEqual(TEXT("Blizzard"), EnvironmentRules::WeatherToString(EWeatherState::Blizzard), FString(TEXT("Blizzard")));
	EWeatherState Kept = EWeatherState::Snowfall;
	TestFalse(TEXT("Rain is not a winter state"), EnvironmentRules::WeatherFromString(TEXT("Rain"), Kept));
	TestFalse(TEXT("empty"), EnvironmentRules::WeatherFromString(FString(), Kept));
	TestTrue(TEXT("unknown text leaves the output alone"), Kept == EWeatherState::Snowfall);
	TestEqual(TEXT("EarlyMorning"), EnvironmentRules::PhaseToString(ETimeOfDayPhase::EarlyMorning), FString(TEXT("EarlyMorning")));
	TestEqual(TEXT("Midday"), EnvironmentRules::PhaseToString(ETimeOfDayPhase::Midday), FString(TEXT("Midday")));
	TestEqual(TEXT("Afternoon"), EnvironmentRules::PhaseToString(ETimeOfDayPhase::Afternoon), FString(TEXT("Afternoon")));
	TestEqual(TEXT("Night"), EnvironmentRules::PhaseToString(ETimeOfDayPhase::Night), FString(TEXT("Night")));
	return true;
}

#undef ENV_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
