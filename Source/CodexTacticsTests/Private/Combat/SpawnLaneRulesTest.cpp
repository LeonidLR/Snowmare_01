#include "Misc/AutomationTest.h"
#include "Combat/SpawnLaneRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Spawn lanes: Godot's containment rule plus the NORTH_GATE <-> «Северные ворота» aliases (Sprint 05-C, decision Q9).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpawnLaneRulesTest, "CodexTactics.Combat.SpawnLanes.Aliases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpawnLaneRulesTest::RunTest(const FString&)
{
	TestEqual(TEXT("Russian north gate"), SpawnLaneRules::CanonicalLane(TEXT("Северные ворота")), FString(TEXT("NORTH_GATE")));
	TestEqual(TEXT("West flank with the suffix"), SpawnLaneRules::CanonicalLane(TEXT("Левый фланг (Прорыв)")), FString(TEXT("WEST_FLANK")));
	TestEqual(TEXT("Key in lower case"), SpawnLaneRules::CanonicalLane(TEXT("east_flank")), FString(TEXT("EAST_FLANK")));
	TestEqual(TEXT("Unknown stays (upper)"), SpawnLaneRules::CanonicalLane(TEXT("Roof")), FString(TEXT("ROOF")));

	TestTrue(TEXT("JSON key -> Russian point"), SpawnLaneRules::LanesMatch(TEXT("Северные ворота"), TEXT("NORTH_GATE")));
	TestTrue(TEXT("Russian request -> key point"), SpawnLaneRules::LanesMatch(TEXT("WEST_FLANK"), TEXT("Левый фланг (Прорыв)")));
	TestTrue(TEXT("Far perimeter"), SpawnLaneRules::LanesMatch(TEXT("Дальний периметр"), TEXT("FAR_PERIMETER")));
	TestFalse(TEXT("Other lane"), SpawnLaneRules::LanesMatch(TEXT("Правый фланг"), TEXT("WEST_FLANK")));
	TestTrue(TEXT("ANY request"), SpawnLaneRules::LanesMatch(TEXT("Правый фланг"), TEXT("ANY")));
	TestTrue(TEXT("Empty request"), SpawnLaneRules::LanesMatch(TEXT("Правый фланг"), FString()));
	TestTrue(TEXT("Godot containment kept"), SpawnLaneRules::LanesMatch(TEXT("Левый фланг (Прорыв)"), TEXT("Левый фланг")));
	return true;
}

#endif
