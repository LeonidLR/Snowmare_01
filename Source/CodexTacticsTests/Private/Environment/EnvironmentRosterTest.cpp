#include "Misc/AutomationTest.h"
#include "Data/LevelJsonRules.h"
#include "Environment/EnvironmentRosterRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Phase 2b: the enemy roster by time of day and weather (UE-only; docs/systems/environment-weather.md §4.2, roster v2 approved
// 2026-10-22). Reference: docs/design/roster-by-weather.html (its script's JEV table, PAIRS, thresholds 2.0 / 1.4 / 2.7 and resolve()).
// The tests build their own FEnvironmentRosterConfig (the game reads it from environment.json).

#define ROSTER_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Environment.Roster." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace RosterTest
{
	const EWeatherState AllWeathers[] = {EWeatherState::Clear, EWeatherState::Overcast, EWeatherState::Snowfall, EWeatherState::Blizzard};
	const ETimeOfDayPhase AllPhases[] = {ETimeOfDayPhase::EarlyMorning, ETimeOfDayPhase::Midday, ETimeOfDayPhase::Afternoon, ETimeOfDayPhase::Night};

	/** The page's Jev table (6 types x 16 cells, index = weather * 4 + phase: Clear EM / Mid / Aft / Night, Overcast ..., Blizzard ...). */
	FEnvironmentRosterConfig MakeDefaultRoster()
	{
		FEnvironmentRosterConfig Roster;
		Roster.bEnabled = true;
		Roster.HalfBelow = 2.0f;
		Roster.AllBelow = 1.4f;
		Roster.BonusAbove = 2.7f;
		Roster.BonusMultiplier = 1.5f;
		auto AddPair = [&Roster](EEnemyArchetype From, EEnemyArchetype To, int32 Ratio)
		{
			FEnvironmentRosterPair& Pair = Roster.Pairs.AddDefaulted_GetRef();
			Pair.From = From;
			Pair.To = To;
			Pair.Ratio = Ratio;
		};
		AddPair(EEnemyArchetype::Marksman, EEnemyArchetype::Frostbitten, 2);
		AddPair(EEnemyArchetype::Spitter, EEnemyArchetype::Cutter, 1);
		Roster.BonusTypes = {EEnemyArchetype::FrostHound};
		auto Row = [&Roster](EEnemyArchetype Type, TArray<float> Scores)
		{
			FEnvironmentSuitabilityRow& New = Roster.Suitability.AddDefaulted_GetRef();
			New.Type = Type;
			New.Scores = MoveTemp(Scores);
		};
		Row(EEnemyArchetype::FrostHound, {2.33f, 2.22f, 2.4f, 2.43f, 2.44f, 2.3f, 2.38f, 2.75f, 2.73f, 2.78f, 2.77f, 2.98f, 2.98f, 3.09f, 3.1f, 3.16f});
		Row(EEnemyArchetype::Spitter, {3.33f, 3.43f, 3.31f, 2.99f, 2.39f, 2.59f, 2.38f, 2.0f, 1.87f, 1.96f, 1.89f, 1.81f, 1.05f, 1.01f, 1.1f, 1.09f});
		Row(EEnemyArchetype::Brute, {2.51f, 2.47f, 2.46f, 2.29f, 2.34f, 2.35f, 2.25f, 2.23f, 2.3f, 2.19f, 2.23f, 2.09f, 2.06f, 2.11f, 2.12f, 2.04f});
		Row(EEnemyArchetype::Frostbitten, {2.75f, 2.6f, 2.74f, 2.69f, 2.7f, 2.41f, 2.5f, 2.66f, 3.06f, 2.78f, 2.87f, 2.8f, 3.01f, 2.83f, 3.15f, 2.97f});
		Row(EEnemyArchetype::Cutter, {2.94f, 2.96f, 2.96f, 2.66f, 2.44f, 2.61f, 2.5f, 2.58f, 2.65f, 2.51f, 2.58f, 2.72f, 2.39f, 2.52f, 2.57f, 2.57f});
		Row(EEnemyArchetype::Marksman, {3.71f, 3.87f, 3.72f, 3.16f, 2.4f, 2.69f, 2.3f, 1.89f, 1.43f, 1.53f, 1.46f, 1.77f, 0.42f, 0.39f, 0.41f, 0.41f});
		return Roster;
	}

	FEnemySpawnEntry Entry(EEnemyArchetype Type, int32 Count, const TCHAR* Lane = TEXT("ANY"))
	{
		FEnemySpawnEntry Out;
		Out.EnemyType = Type;
		Out.Count = Count;
		Out.SpawnLane = Lane;
		return Out;
	}

	/** The page's example wave: HOUND 3, SPITTER 2, MARKSMAN 2, FROSTBITTEN 4, BRUTE 1. */
	TArray<FEnemySpawnEntry> MakeExampleWave()
	{
		return {Entry(EEnemyArchetype::FrostHound, 3), Entry(EEnemyArchetype::Spitter, 2, TEXT("WEST_FLANK")), Entry(EEnemyArchetype::Marksman, 2, TEXT("NORTH_GATE")),
			Entry(EEnemyArchetype::Frostbitten, 4), Entry(EEnemyArchetype::Brute, 1)};
	}

	struct FExpected
	{
		EEnemyArchetype Type;
		int32 Count;
	};

	bool SameEntries(FAutomationTestBase& Test, const FString& What, const TArray<FEnemySpawnEntry>& Actual, const TArray<FExpected>& Expected)
	{
		bool bOk = Actual.Num() == Expected.Num();
		for (int32 Index = 0; bOk && Index < Expected.Num(); ++Index)
		{
			bOk = Actual[Index].EnemyType == Expected[Index].Type && Actual[Index].Count == Expected[Index].Count;
		}
		if (!bOk)
		{
			FString Got;
			for (const FEnemySpawnEntry& One : Actual)
			{
				Got += FString::Printf(TEXT("%d x%d, "), static_cast<int32>(One.EnemyType), One.Count);
			}
			Test.AddError(FString::Printf(TEXT("%s: got [%s]"), *What, *Got));
		}
		return bOk;
	}

	/** Totals per type (the page aggregates per type). */
	TMap<EEnemyArchetype, int32> Totals(const TArray<FEnemySpawnEntry>& Entries)
	{
		TMap<EEnemyArchetype, int32> Out;
		for (const FEnemySpawnEntry& One : Entries)
		{
			Out.FindOrAdd(One.EnemyType) += One.Count;
		}
		return Out;
	}

	/** A straight port of the page's resolve() (per type: pairs, then HOUND x1.5) - the parity reference. */
	TMap<EEnemyArchetype, int32> PageResolve(const FEnvironmentRosterConfig& Roster, EWeatherState Weather, ETimeOfDayPhase Phase)
	{
		TMap<EEnemyArchetype, int32> R = Totals(MakeExampleWave());
		auto Score = [&](EEnemyArchetype Type)
		{
			float S = 0.f;
			EnvironmentRosterRules::GetSuitability(Roster, Type, Weather, Phase, S);
			return S;
		};
		for (const FEnvironmentRosterPair& Pair : Roster.Pairs)
		{
			const float S = Score(Pair.From);
			const float Share = S < 1.4f ? 1.f : S < 2.0f ? 0.5f : 0.f;
			const int32 Have = R.FindRef(Pair.From);
			const int32 Moved = FMath::FloorToInt(Have * Share + 0.5f);
			if (Share > 0.f && Moved > 0)
			{
				R.Add(Pair.From, Have - Moved);
				R.FindOrAdd(Pair.To) += Moved * Pair.Ratio;
			}
		}
		if (Score(EEnemyArchetype::FrostHound) > 2.7f && R.FindRef(EEnemyArchetype::FrostHound) > 0)
		{
			R.Add(EEnemyArchetype::FrostHound, FMath::FloorToInt(R.FindRef(EEnemyArchetype::FrostHound) * 1.5f + 0.5f));
		}
		for (auto It = R.CreateIterator(); It; ++It)
		{
			if (It.Value() == 0)
			{
				It.RemoveCurrent();
			}
		}
		return R;
	}
}

ROSTER_TEST(FEnvRosterSuitabilityLookupTest, "SuitabilityLookupAndSwapShare")
bool FEnvRosterSuitabilityLookupTest::RunTest(const FString&)
{
	FEnvironmentRosterConfig Roster = RosterTest::MakeDefaultRoster();
	float Score = -1.f;
	TestTrue(TEXT("MARKSMAN has a row"), EnvironmentRosterRules::GetSuitability(Roster, EEnemyArchetype::Marksman, EWeatherState::Overcast, ETimeOfDayPhase::Night, Score));
	TestEqual(TEXT("index = weather * 4 + phase: Overcast Night = 1.89"), Score, 1.89f, 0.0001f);
	TestTrue(TEXT("HOUND Blizzard Midday"), EnvironmentRosterRules::GetSuitability(Roster, EEnemyArchetype::FrostHound, EWeatherState::Blizzard, ETimeOfDayPhase::Midday, Score));
	TestEqual(TEXT("HOUND Blizzard Midday = 3.09"), Score, 3.09f, 0.0001f);
	Score = -1.f;
	TestFalse(TEXT("CRYO_DRONE has no row"), EnvironmentRosterRules::GetSuitability(Roster, EEnemyArchetype::CryoDrone, EWeatherState::Clear, ETimeOfDayPhase::Midday, Score));
	TestEqual(TEXT("OutScore untouched without a row"), Score, -1.f);
	Roster.Suitability[0].Scores.SetNum(8); // HOUND row cut after Overcast
	TestFalse(TEXT("a short row has no Snowfall cell"), EnvironmentRosterRules::GetSuitability(Roster, EEnemyArchetype::FrostHound, EWeatherState::Snowfall, ETimeOfDayPhase::EarlyMorning, Score));
	TestEqual(TEXT("OutScore untouched for a short row"), Score, -1.f);

	TestEqual(TEXT("1.39 -> all"), EnvironmentRosterRules::GetSwapShare(Roster, 1.39f), 1.f);
	TestEqual(TEXT("1.4 -> half (strictly below)"), EnvironmentRosterRules::GetSwapShare(Roster, 1.4f), 0.5f);
	TestEqual(TEXT("1.99 -> half"), EnvironmentRosterRules::GetSwapShare(Roster, 1.99f), 0.5f);
	TestEqual(TEXT("2.0 -> none (strictly below)"), EnvironmentRosterRules::GetSwapShare(Roster, 2.0f), 0.f);
	return true;
}

ROSTER_TEST(FEnvRosterClearUnchangedTest, "ClearMiddayAndClearNightKeepTheWave")
bool FEnvRosterClearUnchangedTest::RunTest(const FString&)
{
	const FEnvironmentRosterConfig Roster = RosterTest::MakeDefaultRoster();
	const TArray<RosterTest::FExpected> AsWritten = {{EEnemyArchetype::FrostHound, 3}, {EEnemyArchetype::Spitter, 2}, {EEnemyArchetype::Marksman, 2},
		{EEnemyArchetype::Frostbitten, 4}, {EEnemyArchetype::Brute, 1}};
	TArray<FString> Log;
	RosterTest::SameEntries(*this, TEXT("Clear Midday"), EnvironmentRosterRules::ResolveSpawns(Roster, RosterTest::MakeExampleWave(), EWeatherState::Clear, ETimeOfDayPhase::Midday, &Log), AsWritten);
	TestEqual(TEXT("Clear Midday: no log lines"), Log.Num(), 0);
	// User decision 13: a clear night keeps the marksmen (MARKSMAN 3.16; HOUND 2.43 is no hunting weather).
	RosterTest::SameEntries(*this, TEXT("Clear Night"), EnvironmentRosterRules::ResolveSpawns(Roster, RosterTest::MakeExampleWave(), EWeatherState::Clear, ETimeOfDayPhase::Night, &Log), AsWritten);
	TestEqual(TEXT("Clear Night: no log lines"), Log.Num(), 0);
	return true;
}

ROSTER_TEST(FEnvRosterOvercastNightTest, "OvercastNightHalfTheMarksmenAndHounds")
bool FEnvRosterOvercastNightTest::RunTest(const FString&)
{
	const FEnvironmentRosterConfig Roster = RosterTest::MakeDefaultRoster();
	TArray<FString> Log;
	const TArray<FEnemySpawnEntry> Out = EnvironmentRosterRules::ResolveSpawns(Roster, RosterTest::MakeExampleWave(), EWeatherState::Overcast, ETimeOfDayPhase::Night, &Log);
	// MARKSMAN 1.89 < 2.0: 1 of 2 -> 2 FROSTBITTEN (right after it); SPITTER 2.0 is not below 2.0; HOUND 2.75 > 2.7: 3 x 1.5 = 4.5 -> 5.
	RosterTest::SameEntries(*this, TEXT("Overcast Night"), Out, {{EEnemyArchetype::FrostHound, 5}, {EEnemyArchetype::Spitter, 2}, {EEnemyArchetype::Marksman, 1},
		{EEnemyArchetype::Frostbitten, 2}, {EEnemyArchetype::Frostbitten, 4}, {EEnemyArchetype::Brute, 1}});
	TestEqual(TEXT("two log lines"), Log.Num(), 2);
	TestTrue(TEXT("swap line"), Log.Contains(TEXT("MARKSMAN 1.89 < 2.0: 1 -> 2 FROSTBITTEN")));
	TestTrue(TEXT("bonus line"), Log.Contains(TEXT("HOUND 2.75 > 2.7: 3 -> 5")));
	if (Out.Num() == 6)
	{
		TestEqual(TEXT("replacement keeps the lane"), Out[3].SpawnLane, FString(TEXT("NORTH_GATE")));
	}
	return true;
}

ROSTER_TEST(FEnvRosterSnowfallMiddayTest, "SnowfallMiddayHalfSwapsAndHounds")
bool FEnvRosterSnowfallMiddayTest::RunTest(const FString&)
{
	const FEnvironmentRosterConfig Roster = RosterTest::MakeDefaultRoster();
	TArray<FString> Log;
	const TArray<FEnemySpawnEntry> Out = EnvironmentRosterRules::ResolveSpawns(Roster, RosterTest::MakeExampleWave(), EWeatherState::Snowfall, ETimeOfDayPhase::Midday, &Log);
	// SPITTER 1.96: 1 of 2 -> 1 CUTTER; MARKSMAN 1.53: 1 of 2 -> 2 FROSTBITTEN; HOUND 2.78: 3 -> 5.
	RosterTest::SameEntries(*this, TEXT("Snowfall Midday"), Out, {{EEnemyArchetype::FrostHound, 5}, {EEnemyArchetype::Spitter, 1}, {EEnemyArchetype::Cutter, 1},
		{EEnemyArchetype::Marksman, 1}, {EEnemyArchetype::Frostbitten, 2}, {EEnemyArchetype::Frostbitten, 4}, {EEnemyArchetype::Brute, 1}});
	TestEqual(TEXT("three log lines"), Log.Num(), 3);
	TestTrue(TEXT("spitter line"), Log.Contains(TEXT("SPITTER 1.96 < 2.0: 1 -> 1 CUTTER")));
	return true;
}

ROSTER_TEST(FEnvRosterBlizzardNightTest, "BlizzardNightAllSwapsAndHounds")
bool FEnvRosterBlizzardNightTest::RunTest(const FString&)
{
	const FEnvironmentRosterConfig Roster = RosterTest::MakeDefaultRoster();
	TArray<FString> Log;
	const TArray<FEnemySpawnEntry> Out = EnvironmentRosterRules::ResolveSpawns(Roster, RosterTest::MakeExampleWave(), EWeatherState::Blizzard, ETimeOfDayPhase::Night, &Log);
	// SPITTER 1.09 < 1.4: all 2 -> 2 CUTTER; MARKSMAN 0.41: all 2 -> 4 FROSTBITTEN; HOUND 3.16: 3 -> 5. The emptied entries are dropped.
	RosterTest::SameEntries(*this, TEXT("Blizzard Night"), Out, {{EEnemyArchetype::FrostHound, 5}, {EEnemyArchetype::Cutter, 2}, {EEnemyArchetype::Frostbitten, 4},
		{EEnemyArchetype::Frostbitten, 4}, {EEnemyArchetype::Brute, 1}});
	TestTrue(TEXT("marksman line"), Log.Contains(TEXT("MARKSMAN 0.41 < 1.4: 2 -> 4 FROSTBITTEN")));
	if (Out.Num() == 5)
	{
		TestEqual(TEXT("cutter on the spitter's lane"), Out[1].SpawnLane, FString(TEXT("WEST_FLANK")));
	}
	return true;
}

ROSTER_TEST(FEnvRosterPageParityTest, "PageParityAllSixteenCells")
bool FEnvRosterPageParityTest::RunTest(const FString&)
{
	const FEnvironmentRosterConfig Roster = RosterTest::MakeDefaultRoster();
	for (const EWeatherState Weather : RosterTest::AllWeathers)
	{
		for (const ETimeOfDayPhase Phase : RosterTest::AllPhases)
		{
			const TMap<EEnemyArchetype, int32> Got = RosterTest::Totals(EnvironmentRosterRules::ResolveSpawns(Roster, RosterTest::MakeExampleWave(), Weather, Phase));
			const TMap<EEnemyArchetype, int32> Want = RosterTest::PageResolve(Roster, Weather, Phase);
			bool bSame = Got.Num() == Want.Num();
			for (const TPair<EEnemyArchetype, int32>& Pair : Want)
			{
				bSame &= Got.FindRef(Pair.Key) == Pair.Value;
			}
			TestTrue(FString::Printf(TEXT("cell weather %d phase %d matches the page"), static_cast<int32>(Weather), static_cast<int32>(Phase)), bSame);
		}
	}
	return true;
}

ROSTER_TEST(FEnvRosterReplacementShapeTest, "ReplacementCopiesLaneDelaysNotCustomStats")
bool FEnvRosterReplacementShapeTest::RunTest(const FString&)
{
	const FEnvironmentRosterConfig Roster = RosterTest::MakeDefaultRoster();
	FEnemySpawnEntry Marksman = RosterTest::Entry(EEnemyArchetype::Marksman, 3, TEXT("EAST_FLANK"));
	Marksman.SpawnDelaySec = 2.5f;
	Marksman.InitialDelaySec = 4.f;
	Marksman.CustomHealth = 300.f;
	Marksman.CustomDamage = 40.f;
	Marksman.CustomSpeed = 2.f;
	Marksman.CustomAttackRange = 50.f;
	Marksman.CustomAttackCooldown = 3.f;
	const TArray<FEnemySpawnEntry> Out = EnvironmentRosterRules::ResolveSpawns(Roster, {Marksman}, EWeatherState::Snowfall, ETimeOfDayPhase::Night);
	// MARKSMAN 1.77 < 2.0: 3 x 0.5 = 1.5 -> 2 move (round half up), 1 stays.
	if (RosterTest::SameEntries(*this, TEXT("Snowfall Night, 3 marksmen"), Out, {{EEnemyArchetype::Marksman, 1}, {EEnemyArchetype::Frostbitten, 4}}))
	{
		TestEqual(TEXT("the rest keeps its custom health"), Out[0].CustomHealth, 300.f);
		const FEnemySpawnEntry& Swap = Out[1];
		TestEqual(TEXT("lane"), Swap.SpawnLane, FString(TEXT("EAST_FLANK")));
		TestEqual(TEXT("spawn delay"), Swap.SpawnDelaySec, 2.5f);
		TestEqual(TEXT("initial delay"), Swap.InitialDelaySec, 4.f);
		TestTrue(TEXT("no custom stats"), Swap.CustomHealth == 0.f && Swap.CustomDamage == 0.f && Swap.CustomSpeed == 0.f
			&& Swap.CustomAttackRange == 0.f && Swap.CustomAttackCooldown == 0.f);
		TestFalse(TEXT("not locked"), Swap.bEnvironmentLocked);
	}
	return true;
}

ROSTER_TEST(FEnvRosterRoundingCountOneTest, "CountOneHalfShareMovesOne")
bool FEnvRosterRoundingCountOneTest::RunTest(const FString&)
{
	const FEnvironmentRosterConfig Roster = RosterTest::MakeDefaultRoster();
	TArray<FString> Log;
	// Snowfall EarlyMorning: MARKSMAN 1.43 (half): 1 x 0.5 = 0.5 -> 1 moves (round half up); the emptied entry is dropped.
	RosterTest::SameEntries(*this, TEXT("one marksman, half share"),
		EnvironmentRosterRules::ResolveSpawns(Roster, {RosterTest::Entry(EEnemyArchetype::Marksman, 1)}, EWeatherState::Snowfall, ETimeOfDayPhase::EarlyMorning, &Log),
		{{EEnemyArchetype::Frostbitten, 2}});
	TestTrue(TEXT("log"), Log.Contains(TEXT("MARKSMAN 1.43 < 2.0: 1 -> 2 FROSTBITTEN")));
	// One hound in hunting weather: 1 x 1.5 = 1.5 -> 2.
	RosterTest::SameEntries(*this, TEXT("one hound, bonus"),
		EnvironmentRosterRules::ResolveSpawns(Roster, {RosterTest::Entry(EEnemyArchetype::FrostHound, 1)}, EWeatherState::Blizzard, ETimeOfDayPhase::Midday),
		{{EEnemyArchetype::FrostHound, 2}});
	// A written count of 0 is dropped.
	RosterTest::SameEntries(*this, TEXT("count 0 dropped"),
		EnvironmentRosterRules::ResolveSpawns(Roster, {RosterTest::Entry(EEnemyArchetype::Brute, 0), RosterTest::Entry(EEnemyArchetype::Brute, 2)}, EWeatherState::Clear, ETimeOfDayPhase::Midday),
		{{EEnemyArchetype::Brute, 2}});
	return true;
}

ROSTER_TEST(FEnvRosterLockedEntryTest, "LockedEntryUntouched")
bool FEnvRosterLockedEntryTest::RunTest(const FString&)
{
	const FEnvironmentRosterConfig Roster = RosterTest::MakeDefaultRoster();
	FEnemySpawnEntry Marksman = RosterTest::Entry(EEnemyArchetype::Marksman, 2);
	Marksman.bEnvironmentLocked = true;
	FEnemySpawnEntry Hound = RosterTest::Entry(EEnemyArchetype::FrostHound, 3);
	Hound.bEnvironmentLocked = true;
	TArray<FString> Log;
	const TArray<FEnemySpawnEntry> Out = EnvironmentRosterRules::ResolveSpawns(Roster, {Marksman, Hound, RosterTest::Entry(EEnemyArchetype::FrostHound, 3)},
		EWeatherState::Blizzard, ETimeOfDayPhase::Night, &Log);
	if (RosterTest::SameEntries(*this, TEXT("Blizzard Night with locked rows"), Out,
		{{EEnemyArchetype::Marksman, 2}, {EEnemyArchetype::FrostHound, 3}, {EEnemyArchetype::FrostHound, 5}}))
	{
		TestTrue(TEXT("still locked"), Out[0].bEnvironmentLocked && Out[1].bEnvironmentLocked);
	}
	TestEqual(TEXT("only the unlocked hound is logged"), Log.Num(), 1);
	return true;
}

ROSTER_TEST(FEnvRosterUnknownTypeTest, "TypeWithoutRowUntouched")
bool FEnvRosterUnknownTypeTest::RunTest(const FString&)
{
	FEnvironmentRosterConfig Roster = RosterTest::MakeDefaultRoster();
	// Bosses / future types have no row (decision 18): never substituted, never boosted.
	RosterTest::SameEntries(*this, TEXT("CRYO_DRONE / BASE in a blizzard"),
		EnvironmentRosterRules::ResolveSpawns(Roster, {RosterTest::Entry(EEnemyArchetype::CryoDrone, 2), RosterTest::Entry(EEnemyArchetype::Base, 1)}, EWeatherState::Blizzard,
			ETimeOfDayPhase::Night),
		{{EEnemyArchetype::CryoDrone, 2}, {EEnemyArchetype::Base, 1}});
	// A pair's From without a row does not swap; a bonus type without a row is not boosted.
	Roster.Suitability.RemoveAll([](const FEnvironmentSuitabilityRow& Row)
	{
		return Row.Type == EEnemyArchetype::Marksman || Row.Type == EEnemyArchetype::FrostHound;
	});
	RosterTest::SameEntries(*this, TEXT("rows removed"),
		EnvironmentRosterRules::ResolveSpawns(Roster, {RosterTest::Entry(EEnemyArchetype::Marksman, 2), RosterTest::Entry(EEnemyArchetype::FrostHound, 3)}, EWeatherState::Blizzard,
			ETimeOfDayPhase::Night),
		{{EEnemyArchetype::Marksman, 2}, {EEnemyArchetype::FrostHound, 3}});
	return true;
}

ROSTER_TEST(FEnvRosterDisabledTest, "DisabledRosterKeepsTheWave")
bool FEnvRosterDisabledTest::RunTest(const FString&)
{
	FEnvironmentRosterConfig Roster = RosterTest::MakeDefaultRoster();
	Roster.bEnabled = false;
	TArray<FString> Log;
	TArray<FEnemySpawnEntry> Wave = RosterTest::MakeExampleWave();
	Wave.Add(RosterTest::Entry(EEnemyArchetype::Brute, 0)); // even a count-0 entry stays: nothing is touched
	RosterTest::SameEntries(*this, TEXT("disabled, Blizzard Night"), EnvironmentRosterRules::ResolveSpawns(Roster, Wave, EWeatherState::Blizzard, ETimeOfDayPhase::Night, &Log),
		{{EEnemyArchetype::FrostHound, 3}, {EEnemyArchetype::Spitter, 2}, {EEnemyArchetype::Marksman, 2}, {EEnemyArchetype::Frostbitten, 4},
			{EEnemyArchetype::Brute, 1}, {EEnemyArchetype::Brute, 0}});
	TestEqual(TEXT("no log"), Log.Num(), 0);
	// An empty roster (no environment.json roster yet) changes nothing either.
	RosterTest::SameEntries(*this, TEXT("empty roster"), EnvironmentRosterRules::ResolveSpawns(FEnvironmentRosterConfig(), RosterTest::MakeExampleWave(), EWeatherState::Blizzard,
		ETimeOfDayPhase::Night), {{EEnemyArchetype::FrostHound, 3}, {EEnemyArchetype::Spitter, 2}, {EEnemyArchetype::Marksman, 2},
			{EEnemyArchetype::Frostbitten, 4}, {EEnemyArchetype::Brute, 1}});
	return true;
}

ROSTER_TEST(FEnvRosterLevelJsonLockTest, "LevelJsonEnvironmentLockParses")
bool FEnvRosterLevelJsonLockTest::RunTest(const FString&)
{
	const FString Json = TEXT(R"({
		"level_id": "roster_lock",
		"waves": [ { "wave_index": 1, "spawns": [
			{ "enemy_type": "MARKSMAN", "count": 2, "spawn_lane": "ANY", "environment_lock": true },
			{ "enemy_type": "MARKSMAN", "count": 2, "spawn_lane": "ANY", "environment_lock": false },
			{ "enemy_type": "HOUND", "count": 3, "spawn_lane": "ANY" } ] } ]
	})");
	FLevelCombatConfig Config;
	FString Error;
	TestTrue(TEXT("parses"), LevelJsonRules::ParseLevel(Json, TEXT("roster_lock"), Config, Error));
	if (TestEqual(TEXT("one wave"), Config.Waves.Num(), 1) && TestEqual(TEXT("three spawns"), Config.Waves[0].Spawns.Num(), 3))
	{
		const TArray<FEnemySpawnEntry>& Spawns = Config.Waves[0].Spawns;
		TestTrue(TEXT("environment_lock: true"), Spawns[0].bEnvironmentLocked);
		TestFalse(TEXT("environment_lock: false"), Spawns[1].bEnvironmentLocked);
		TestFalse(TEXT("no key -> unlocked"), Spawns[2].bEnvironmentLocked);
		// The parsed lock reaches the roster: the locked marksmen stay in a blizzard, the others turn into frostbitten.
		RosterTest::SameEntries(*this, TEXT("parsed wave, Blizzard Night"),
			EnvironmentRosterRules::ResolveSpawns(RosterTest::MakeDefaultRoster(), Spawns, EWeatherState::Blizzard, ETimeOfDayPhase::Night),
			{{EEnemyArchetype::Marksman, 2}, {EEnemyArchetype::Frostbitten, 4}, {EEnemyArchetype::FrostHound, 5}});
	}
	return true;
}

#undef ROSTER_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
