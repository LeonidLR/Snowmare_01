// CodexTactics.Combat.Horde.* — the horde (UE-only; HordeRules, UHordeSubsystem): the spawn point inside the 25-45 m band (hidden first),
// the mix, the per-level JSON. The real-time-fight clock tests (TimerCountsOnlyRunningRealTime, TriggersOnceAt240, RepeatOption) went with
// the clock on 2026-11-09 (game designer: only the firefight streak summons the horde - CodexTactics.Combat.Firefight.NoTimerHordeOnlyTheStreak).

#include "Misc/AutomationTest.h"
#include "Combat/HordeRules.h"
#include "Data/LevelJsonRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define HORDE_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Combat.Horde." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

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
	TestTrue(TEXT("JSON applied"), ApplyJsonText(TEXT("{\"count\": 10, \"min_distance_m\": 30, \"max_distance_m\": 50, \"composition\": {\"BRUTE\": 1, \"NOPE\": 3}}"), FromJson));
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
