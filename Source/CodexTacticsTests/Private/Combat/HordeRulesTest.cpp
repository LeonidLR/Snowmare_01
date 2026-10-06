// CodexTactics.Combat.Horde.* — the horde after a long real-time fight (user request 2026-10-06, UE-only; HordeRules,
// UHordeSubsystem): the clock counts only running real-time fight time, the first horde at 240 s, once unless repeating,
// the spawn point inside the 25-45 m band (hidden first), the mix, the per-level JSON.

#include "Misc/AutomationTest.h"
#include "Combat/HordeRules.h"
#include "Data/LevelJsonRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define HORDE_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Combat.Horde." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

HORDE_TEST(FHordeTimerCountsOnlyRunningRealTimeTest, "TimerCountsOnlyRunningRealTime")
bool FHordeTimerCountsOnlyRunningRealTimeTest::RunTest(const FString&)
{
	using namespace HordeRules;
	TestTrue(TEXT("Wave fight in real time counts"), IsCountingTime(ECodexGamePhase::WaveCombat, ECodexCombatMode::RealTime, false));
	TestFalse(TEXT("Tactical pause does not"), IsCountingTime(ECodexGamePhase::WaveCombat, ECodexCombatMode::TacticalPause, false));
	TestFalse(TEXT("Turn-based does not"), IsCountingTime(ECodexGamePhase::WaveCombat, ECodexCombatMode::TurnBased, false));
	TestFalse(TEXT("Dialogue / cutscene AI pause does not"), IsCountingTime(ECodexGamePhase::WaveCombat, ECodexCombatMode::RealTime, true));
	TestFalse(TEXT("Preparation does not"), IsCountingTime(ECodexGamePhase::Preparation, ECodexCombatMode::None, false));
	TestFalse(TEXT("Exploration does not"), IsCountingTime(ECodexGamePhase::Exploration, ECodexCombatMode::None, false));

	const FHordeConfig Config;
	FHordeTimer Timer;
	// 200 s of real time, then 100 s of pause and 100 s of turn-based, then 39 s of real time: 239 s counted, no horde.
	for (int32 Step = 0; Step < 200; ++Step)
	{
		TestFalse(TEXT("No horde before 240 s"), Timer.Advance(1.f, true, Config));
	}
	for (int32 Step = 0; Step < 200; ++Step)
	{
		TestFalse(TEXT("Stopped clock never fires"), Timer.Advance(1.f, false, Config));
	}
	TestEqual(TEXT("Paused / turn-based time not counted"), Timer.CombatSeconds, 200.f);
	for (int32 Step = 0; Step < 39; ++Step)
	{
		Timer.Advance(1.f, true, Config);
	}
	TestEqual(TEXT("239 s counted"), Timer.CombatSeconds, 239.f);
	TestEqual(TEXT("No horde yet"), Timer.HordesReleased, 0);
	return true;
}

HORDE_TEST(FHordeTriggersOnceAt240Test, "TriggersOnceAt240")
bool FHordeTriggersOnceAt240Test::RunTest(const FString&)
{
	const FHordeConfig Config;
	TestEqual(TEXT("Default trigger 240 s"), Config.TriggerSeconds, 240.f);
	TestFalse(TEXT("Default: once per fight"), Config.bRepeats);
	FHordeTimer Timer;
	TestFalse(TEXT("239.5 s"), Timer.Advance(239.5f, true, Config));
	TestTrue(TEXT("240 s: the horde"), Timer.Advance(0.5f, true, Config));
	TestEqual(TEXT("One horde"), Timer.HordesReleased, 1);
	int32 More = 0;
	for (int32 Step = 0; Step < 3600; ++Step)
	{
		More += Timer.Advance(1.f, true, Config) ? 1 : 0;
	}
	TestEqual(TEXT("Never again in that fight"), More, 0);
	// A long frame never releases two at once; a new fight restarts the clock.
	Timer.Reset();
	TestTrue(TEXT("New fight: a 500 s jump releases one"), Timer.Advance(500.f, true, Config));
	TestEqual(TEXT("Only one"), Timer.HordesReleased, 1);
	FHordeConfig Off;
	Off.bEnabled = false;
	FHordeTimer OffTimer;
	TestFalse(TEXT("Disabled: never"), OffTimer.Advance(1000.f, true, Off));
	return true;
}

HORDE_TEST(FHordeRepeatOptionTest, "RepeatOption")
bool FHordeRepeatOptionTest::RunTest(const FString&)
{
	FHordeConfig Config;
	Config.bRepeats = true;
	Config.RepeatSeconds = 120.f;
	Config.CountIncreasePerRepeat = 2;
	FHordeTimer Timer;
	TArray<float> ReleasedAt;
	for (int32 Step = 1; Step <= 600; ++Step)
	{
		if (Timer.Advance(1.f, true, Config))
		{
			ReleasedAt.Add(Timer.CombatSeconds);
		}
	}
	TestEqual(TEXT("240 / 360 / 480 / 600"), ReleasedAt.Num(), 4);
	if (ReleasedAt.Num() == 4)
	{
		TestEqual(TEXT("First"), ReleasedAt[0], 240.f);
		TestEqual(TEXT("Second"), ReleasedAt[1], 360.f);
		TestEqual(TEXT("Fourth"), ReleasedAt[3], 600.f);
	}
	TestEqual(TEXT("First horde size"), HordeRules::GetHordeSize(Config, 0), 8);
	TestEqual(TEXT("Third horde grows by 2 x 2"), HordeRules::GetHordeSize(Config, 2), 12);
	return true;
}

HORDE_TEST(FHordeSpawnPointDistanceBandTest, "SpawnPointDistanceBand")
bool FHordeSpawnPointDistanceBandTest::RunTest(const FString&)
{
	using namespace HordeRules;
	const FHordeConfig Config;
	TestTrue(TEXT("25 m in"), IsInDistanceBand(2500.f, Config));
	TestTrue(TEXT("45 m in"), IsInDistanceBand(4500.f, Config));
	TestFalse(TEXT("24 m out"), IsInDistanceBand(2400.f, Config));
	TestFalse(TEXT("46 m out"), IsInDistanceBand(4600.f, Config));

	TArray<FHordeSpawnCandidate> Candidates;
	auto Add = [&Candidates](float Distance, bool bReachable, bool bVisible)
	{
		FHordeSpawnCandidate Candidate;
		Candidate.DistanceCm = Distance;
		Candidate.Location = FVector(Distance, 0.f, 0.f);
		Candidate.bReachable = bReachable;
		Candidate.bVisibleToSquad = bVisible;
		Candidates.Add(Candidate);
	};
	Add(1500.f, true, false);  // 0: too close
	Add(6000.f, true, false);  // 1: too far
	Add(3500.f, false, false); // 2: no path
	Add(3500.f, true, true);   // 3: in the band, seen
	Add(4400.f, true, false);  // 4: in the band, hidden
	TestEqual(TEXT("The hidden reachable point in the band wins"), PickSpawnPoint(Candidates, Config), 4);
	FHordeConfig Visible = Config;
	Visible.bPreferOutOfSight = false;
	TestEqual(TEXT("Without the sight preference: the band's middle"), PickSpawnPoint(Candidates, Visible), 3);
	Candidates.RemoveAt(4);
	TestEqual(TEXT("Only a seen one left: taken"), PickSpawnPoint(Candidates, Config), 3);
	Candidates.RemoveAt(3);
	TestEqual(TEXT("Nothing valid: none"), PickSpawnPoint(Candidates, Config), INDEX_NONE);

	// The cluster stays within its radius.
	for (int32 Index = 0; Index < 8; ++Index)
	{
		TestTrue(FString::Printf(TEXT("Member %d inside 4 m"), Index), ClusterOffset(Index, 8, Config.ClusterRadiusCm).Size2D() <= Config.ClusterRadiusCm + 0.1f);
	}
	TestTrue(TEXT("Distinct spots"), !ClusterOffset(1, 8, 400.f).Equals(ClusterOffset(2, 8, 400.f), 10.f));
	return true;
}

HORDE_TEST(FHordeCompositionAndDataTest, "CompositionAndLevelData")
bool FHordeCompositionAndDataTest::RunTest(const FString&)
{
	using namespace HordeRules;
	const FHordeConfig Config;
	const TArray<EEnemyArchetype> Mix = BuildComposition(Config, 8);
	TestEqual(TEXT("8 members"), Mix.Num(), 8);
	TestEqual(TEXT("5 hounds"), Mix.FilterByPredicate([](EEnemyArchetype T) { return T == EEnemyArchetype::FrostHound; }).Num(), 5);
	TestEqual(TEXT("3 frostbitten"), Mix.FilterByPredicate([](EEnemyArchetype T) { return T == EEnemyArchetype::Frostbitten; }).Num(), 3);
	TestEqual(TEXT("Rounding keeps the total"), BuildComposition(Config, 11).Num(), 11);

	FHordeConfig FromJson;
	TestTrue(TEXT("JSON applied"), ApplyJsonText(TEXT("{\"trigger_seconds\": 180, \"repeats\": true, \"count\": 10, \"min_distance_m\": 30, \"max_distance_m\": 50, \"composition\": {\"BRUTE\": 1, \"NOPE\": 3}}"), FromJson));
	TestEqual(TEXT("Trigger"), FromJson.TriggerSeconds, 180.f);
	TestTrue(TEXT("Repeats"), FromJson.bRepeats);
	TestEqual(TEXT("Count"), FromJson.Count, 10);
	TestEqual(TEXT("Band in cm"), FromJson.MinDistanceCm, 3000.f);
	TestEqual(TEXT("Only known types"), FromJson.Composition.Num(), 1);
	TestFalse(TEXT("Bad JSON changes nothing"), ApplyJsonText(TEXT("not json"), FromJson));

	// Level JSON: "horde_enabled" and the "horde" overrides.
	FLevelCombatConfig Level;
	FString Error;
	TestTrue(TEXT("Level parsed"), LevelJsonRules::ParseLevel(TEXT("{\"level_id\": \"t\", \"horde_enabled\": false, \"horde\": {\"count\": 12}, \"waves\": []}"),
		TEXT("t"), Level, Error));
	TestFalse(TEXT("Level switches it off"), Level.bHordeEnabled);
	const FHordeConfig Resolved = ResolveForLevel(FHordeConfig(), Level.bHordeEnabled, Level.HordeOverrideJson);
	TestFalse(TEXT("Off for that level"), Resolved.bEnabled);
	TestEqual(TEXT("Level override kept"), Resolved.Count, 12);
	FLevelCombatConfig Plain;
	TestTrue(TEXT("Plain level parsed"), LevelJsonRules::ParseLevel(TEXT("{\"level_id\": \"p\", \"waves\": []}"), TEXT("p"), Plain, Error));
	TestTrue(TEXT("Default: on"), Plain.bHordeEnabled && ResolveForLevel(FHordeConfig(), Plain.bHordeEnabled, Plain.HordeOverrideJson).bEnabled);
	return true;
}

#undef HORDE_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
