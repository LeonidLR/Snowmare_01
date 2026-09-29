#include "Misc/AutomationTest.h"
#include "Tactics/ExposedZones.h"

#if WITH_DEV_AUTOMATION_TESTS

// Parity with Godot tests/test_exposed_zones_mechanic.gd (quadrants, turn counter / breach / limit, elite exclusion,
// 6-enemy cap and diversity pool). Coverage itself (living operative / working turret) is decided by the subsystem.

namespace ExposedZonesTest
{
	const FIntPoint GridSize(14, 14);

	FExposedZones::FRosterEntry Entry(EEnemyArchetype Type, float Health, float Damage)
	{
		return { Type, Health, Damage, 6.f };
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExposedZonesQuadrantsTest, "CodexTactics.Tactics.ExposedZones.Quadrants",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExposedZonesQuadrantsTest::RunTest(const FString&)
{
	using namespace ExposedZonesTest;
	TestEqual(TEXT("(0,0) NW"), FExposedZones::GetQuadrant(FIntPoint(0, 0), GridSize), 0);
	TestEqual(TEXT("(6,6) NW"), FExposedZones::GetQuadrant(FIntPoint(6, 6), GridSize), 0);
	TestEqual(TEXT("(7,0) NE"), FExposedZones::GetQuadrant(FIntPoint(7, 0), GridSize), 1);
	TestEqual(TEXT("(13,6) NE"), FExposedZones::GetQuadrant(FIntPoint(13, 6), GridSize), 1);
	TestEqual(TEXT("(0,7) SW"), FExposedZones::GetQuadrant(FIntPoint(0, 7), GridSize), 2);
	TestEqual(TEXT("(6,13) SW"), FExposedZones::GetQuadrant(FIntPoint(6, 13), GridSize), 2);
	TestEqual(TEXT("(7,7) SE"), FExposedZones::GetQuadrant(FIntPoint(7, 7), GridSize), 3);
	TestEqual(TEXT("(13,13) SE"), FExposedZones::GetQuadrant(FIntPoint(13, 13), GridSize), 3);
	const FIntRect NorthWest = FExposedZones::GetQuadrantRect(0, GridSize);
	TestTrue(TEXT("NW rect (0,0, 7x7)"), NorthWest.Min == FIntPoint(0, 0) && NorthWest.Size() == FIntPoint(7, 7));
	TestEqual(TEXT("SE name"), FExposedZones::GetQuadrantName(3), FString(TEXT("Юго-Восток")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExposedZonesBreachTest, "CodexTactics.Tactics.ExposedZones.TurnCounterAndBreach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExposedZonesBreachTest::RunTest(const FString&)
{
	FExposedZones Zones;
	Zones.Reset();
	const bool Covered[4] = { true, false, false, false }; // an operative in NW only
	int32 SpawnCalls = 0;
	int32 LastQuadrant = INDEX_NONE;
	auto Roll = []() { return 2; };
	auto Spawn = [&SpawnCalls, &LastQuadrant](int32 Quadrant, int32 Count) { ++SpawnCalls; LastQuadrant = Quadrant; return Count; };

	FExposedZones::FTurnResult Turn = Zones.EndSquadTurn(Covered, 3, Roll, Spawn);
	TestEqual(TEXT("Turn 1: no breach"), Turn.BreachQuadrant, INDEX_NONE);
	TestEqual(TEXT("NW covered: 0"), Turn.Turns[0], 0);
	TestEqual(TEXT("SE uncovered: 1 (warning)"), Turn.Turns[3], 1);

	Turn = Zones.EndSquadTurn(Covered, 3, Roll, Spawn);
	TestEqual(TEXT("Turn 2: no breach (threshold > 2)"), Turn.BreachQuadrant, INDEX_NONE);
	TestEqual(TEXT("SE uncovered: 2 (danger)"), Turn.Turns[3], 2);

	Turn = Zones.EndSquadTurn(Covered, 3, Roll, Spawn);
	TestEqual(TEXT("Turn 3: breach through the first uncovered quadrant (NE)"), Turn.BreachQuadrant, 1);
	TestEqual(TEXT("2 reinforcements"), Turn.Spawned, 2);
	TestEqual(TEXT("Breached quadrant resets"), Turn.Turns[1], 0);
	TestEqual(TEXT("Other quadrants keep counting"), Turn.Turns[3], 3);
	TestEqual(TEXT("One breach recorded"), Zones.GetReinforcementsSpawned(), 1);

	Turn = Zones.EndSquadTurn(Covered, 5, Roll, Spawn);
	TestEqual(TEXT("Turn 4: no second breach in the fight"), Turn.BreachQuadrant, INDEX_NONE);
	TestEqual(TEXT("Spawn asked once"), SpawnCalls, 1);

	// No free cell: no breach, the counter goes on and the limit is not used up.
	FExposedZones Blocked;
	const bool NoneCovered[4] = { false, false, false, false };
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Turn = Blocked.EndSquadTurn(NoneCovered, 0, Roll, [](int32, int32) { return 0; });
	}
	TestEqual(TEXT("No cells: no breach"), Turn.BreachQuadrant, INDEX_NONE);
	TestEqual(TEXT("No cells: counter keeps going"), Turn.Turns[0], 3);
	TestEqual(TEXT("No cells: limit unused"), Blocked.GetReinforcementsSpawned(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExposedZonesRosterTest, "CodexTactics.Tactics.ExposedZones.RosterAndCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExposedZonesRosterTest::RunTest(const FString&)
{
	using namespace ExposedZonesTest;
	FExposedZones Zones;
	// A: 4 hounds + 1 brute -> the brute is the elite.
	Zones.EvaluateRoster({ Entry(EEnemyArchetype::FrostHound, 45, 12), Entry(EEnemyArchetype::FrostHound, 45, 12),
		Entry(EEnemyArchetype::FrostHound, 45, 12), Entry(EEnemyArchetype::FrostHound, 45, 12), Entry(EEnemyArchetype::Brute, 180, 30) });
	TestTrue(TEXT("A: pool = hounds only"), Zones.GetPool() == TArray<EEnemyArchetype>{ EEnemyArchetype::FrostHound });

	// B: 4 brutes + 1 boss -> brutes are the standard enemy.
	Zones.EvaluateRoster({ Entry(EEnemyArchetype::Brute, 180, 30), Entry(EEnemyArchetype::Brute, 180, 30),
		Entry(EEnemyArchetype::Brute, 180, 30), Entry(EEnemyArchetype::Brute, 180, 30), Entry(EEnemyArchetype::CryoDrone, 600, 80) });
	TestTrue(TEXT("B: pool = brutes only"), Zones.GetPool() == TArray<EEnemyArchetype>{ EEnemyArchetype::Brute });

	// Diversity: hound + cutter stay, brute drops.
	Zones.EvaluateRoster({ Entry(EEnemyArchetype::FrostHound, 45, 12), Entry(EEnemyArchetype::Cutter, 55, 15), Entry(EEnemyArchetype::Brute, 180, 30) });
	TestTrue(TEXT("Pool has hound and cutter, not brute"), Zones.GetPool().Contains(EEnemyArchetype::FrostHound)
		&& Zones.GetPool().Contains(EEnemyArchetype::Cutter) && !Zones.GetPool().Contains(EEnemyArchetype::Brute));

	// Cap: 5 living enemies -> at most 1 arrives (6 in total).
	Zones.Reset();
	const bool Covered[4] = { true, false, false, false };
	int32 Asked = 0;
	FExposedZones::FTurnResult Turn;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Turn = Zones.EndSquadTurn(Covered, 5, []() { return 2; }, [&Asked](int32, int32 Count) { Asked = Count; return Count; });
	}
	TestEqual(TEXT("Breach with 5 enemies alive"), Turn.BreachQuadrant, 1);
	TestEqual(TEXT("Only 1 reinforcement (cap 6)"), Asked, 1);

	// Full field: no breach.
	FExposedZones Full;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Turn = Full.EndSquadTurn(Covered, 6, []() { return 2; }, [](int32, int32 Count) { return Count; });
	}
	TestEqual(TEXT("6 enemies alive: no breach"), Turn.BreachQuadrant, INDEX_NONE);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
