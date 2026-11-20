// CodexTactics.LevelGen.BuildingWalk.* - S2.9 / P0b (docs/design/levelgen/S2_ROOMS_PLAN.md, risk rows 1 and 4): the pure rules of the
// building walk smoke (CodexTactics.LevelGenBuildingsSmoke walk=1) - flat entries from units[] + partitions[].openings[] (roles recorded,
// mismatches reported), arrival, the no-progress tracker, the squad <= 2 x solo gate and the building pass share.
// Pure, no world. UE-only (no Godot reference).

#include "LevelGen/BuildingWalkRules.h"
#include "Misc/AutomationTest.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

#define BUILDING_WALK_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.LevelGen.BuildingWalk." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace BuildingWalkTestLocal
{
	// The shape of gen_bgen_plans_check.json B_x storey 0: flat s0L is entered by d1 (role "room" - the plan's mismatch), flat s0R by
	// d3 (unit_entry) and d5 (top-level opening); d0 is a unit_entry between the lobby and the corridor that no flat lists; d9 is
	// listed but missing; d7 joins two rooms of the same flat.
	const TCHAR* const Building = TEXT(R"json({
		"id": "B_x",
		"units": [
			{ "id": "U_L", "kind": "flat", "floor": 0, "rooms": ["r_c1", "r_c4"], "entries": ["d1", "d9"] },
			{ "id": "U_R", "kind": "flat", "floor": 0, "rooms": ["r_c6", "r_c7"], "entries": ["d3", "d5", "d7"] },
			{ "id": "S_1", "kind": "shop", "floor": 0, "rooms": ["r_s"], "entries": ["d8"] }
		],
		"partitions": [
			{ "id": "p0", "openings": [
				{ "id": "d1", "kind": "door", "floor": 0, "links": ["r_c0", "r_c1"], "role": "room" },
				{ "id": "d0", "kind": "door", "floor": 0, "links": ["r_c0", "r_c3"], "role": "unit_entry" } ] },
			{ "id": "p1", "openings": [
				{ "id": "d3", "kind": "door", "floor": 0, "links": ["r_c6", "r_c3"], "role": "unit_entry" },
				{ "id": "d7", "kind": "door", "floor": 0, "links": ["r_c6", "r_c7"], "role": "room" },
				{ "id": "d8", "kind": "door", "floor": 0, "links": ["r_s", "street"], "role": "unit_entry" } ] }
		],
		"openings": [ { "id": "d5", "kind": "door", "floor": 1, "links": ["r_c5", "r_c7"], "role": "unit_entry" } ]
	})json");

	TSharedPtr<FJsonObject> Parse(const TCHAR* Json)
	{
		TSharedPtr<FJsonObject> Object;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object);
		return Object;
	}
}

BUILDING_WALK_TEST(FBuildingWalkFlatEntriesTest, "FlatEntries.FromUnits")
bool FBuildingWalkFlatEntriesTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> Building = BuildingWalkTestLocal::Parse(BuildingWalkTestLocal::Building);
	if (!TestTrue(TEXT("fixture parses"), Building.IsValid()))
	{
		return false;
	}
	const FBuildingFlatEntries Scan = BuildingWalkRules::ScanFlatEntries(*Building);
	if (!TestEqual(TEXT("three resolved flat entries (d1, d3, d5)"), Scan.Entries.Num(), 3))
	{
		return false;
	}
	TestEqual(TEXT("d1 flat"), Scan.Entries[0].Unit, FString(TEXT("U_L")));
	TestEqual(TEXT("d1 from the lobby"), Scan.Entries[0].FromRoom, FString(TEXT("r_c0")));
	TestEqual(TEXT("d1 into the flat room"), Scan.Entries[0].ToRoom, FString(TEXT("r_c1")));
	TestEqual(TEXT("d1 keeps its own role"), Scan.Entries[0].Role, FString(TEXT("room")));
	TestEqual(TEXT("d3 from the corridor"), Scan.Entries[1].FromRoom, FString(TEXT("r_c3")));
	TestEqual(TEXT("d3 into r_c6"), Scan.Entries[1].ToRoom, FString(TEXT("r_c6")));
	TestEqual(TEXT("d5 from the top-level openings"), Scan.Entries[2].ToRoom, FString(TEXT("r_c7")));
	TestEqual(TEXT("d5 floor from the opening"), Scan.Entries[2].Floor, 1);
	TestEqual(TEXT("entries without the unit_entry role"), Scan.EntriesWithoutRole, TArray<FString>{ TEXT("d1") });
	// d8 belongs to a shop (not a flat): a unit_entry no flat lists, like d0.
	TestEqual(TEXT("unit_entry openings no flat lists"), Scan.RoleNotAnEntry, TArray<FString>({ TEXT("d0"), TEXT("d8") }));
	TestEqual(TEXT("missing / same-flat links unresolved"), Scan.Unresolved, TArray<FString>({ TEXT("d9"), TEXT("d7") }));
	return true;
}

BUILDING_WALK_TEST(FBuildingWalkFlatEntriesEmptyTest, "FlatEntries.NoUnits")
bool FBuildingWalkFlatEntriesEmptyTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> Office = BuildingWalkTestLocal::Parse(TEXT(R"json({ "id": "B_o", "openings": [ { "id": "e", "role": "entry" } ] })json"));
	const FBuildingFlatEntries Scan = BuildingWalkRules::ScanFlatEntries(*Office);
	TestEqual(TEXT("an office has no flat entries"), Scan.Entries.Num(), 0);
	TestEqual(TEXT("nothing to report"), Scan.EntriesWithoutRole.Num() + Scan.RoleNotAnEntry.Num() + Scan.Unresolved.Num(), 0);
	return true;
}

BUILDING_WALK_TEST(FBuildingWalkArrivalTest, "Arrival")
bool FBuildingWalkArrivalTest::RunTest(const FString& Parameters)
{
	const FVector Goal(1000.0, 0.0, 300.0);
	TestTrue(TEXT("79 cm off, same storey"), BuildingWalkRules::IsArrived(FVector(1079.0, 0.0, 310.0), Goal));
	TestFalse(TEXT("81 cm off"), BuildingWalkRules::IsArrived(FVector(1081.0, 0.0, 300.0), Goal));
	TestFalse(TEXT("right XY, the storey below"), BuildingWalkRules::IsArrived(FVector(1000.0, 0.0, 0.0), Goal));
	TestFalse(TEXT("right XY, 71 cm up a flight"), BuildingWalkRules::IsArrived(FVector(1000.0, 0.0, 371.0), Goal));
	return true;
}

BUILDING_WALK_TEST(FBuildingWalkStuckTest, "Stuck")
bool FBuildingWalkStuckTest::RunTest(const FString& Parameters)
{
	FBuildingWalkProgress Progress;
	TestFalse(TEXT("first sample sets the mark"), Progress.IsStuck(10.0, FVector::ZeroVector, 50.0, 4.0));
	TestFalse(TEXT("3.9 s on the spot"), Progress.IsStuck(13.9, FVector(40.0, 0.0, 0.0), 50.0, 4.0));
	TestTrue(TEXT("4 s within 50 cm"), Progress.IsStuck(14.0, FVector(49.0, 0.0, 0.0), 50.0, 4.0));
	TestFalse(TEXT("left the mark: progress"), Progress.IsStuck(14.5, FVector(60.0, 0.0, 0.0), 50.0, 4.0));
	TestFalse(TEXT("the mark moved with him"), Progress.IsStuck(18.0, FVector(60.0, 30.0, 0.0), 50.0, 4.0));
	TestTrue(TEXT("4 s from the new mark"), Progress.IsStuck(18.5, FVector(60.0, 30.0, 0.0), 50.0, 4.0));
	// A switchback flight: away from the goal in 2D but moving - never stuck.
	FBuildingWalkProgress Flight;
	for (int32 Step = 0; Step <= 40; ++Step)
	{
		TestFalse(TEXT("walking round a flight"), Flight.IsStuck(Step * 0.25, FVector(Step * 30.0 * (Step % 2 ? 1.0 : 0.9), 0.0, Step * 8.0), 50.0, 4.0));
	}
	return true;
}

BUILDING_WALK_TEST(FBuildingWalkGatesTest, "Gates")
bool FBuildingWalkGatesTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("squad 2 x solo passes"), BuildingWalkRules::SquadWithinRatio(8.0, 4.0));
	TestFalse(TEXT("squad 2.01 x solo fails"), BuildingWalkRules::SquadWithinRatio(8.04, 4.0));
	TestFalse(TEXT("no solo time: fails"), BuildingWalkRules::SquadWithinRatio(3.0, 0.0));
	TestTrue(TEXT("9 / 9 >= 0.95"), BuildingWalkRules::PassesShare(9, 9, BuildingWalkRules::TargetMinPass));
	TestFalse(TEXT("8 / 9 < 0.95"), BuildingWalkRules::PassesShare(8, 9, BuildingWalkRules::TargetMinPass));
	TestTrue(TEXT("19 / 20 = 0.95"), BuildingWalkRules::PassesShare(19, 20, 0.95));
	TestTrue(TEXT("minpass 0: any checked run passes"), BuildingWalkRules::PassesShare(0, 3, 0.0));
	TestFalse(TEXT("nothing checked never passes"), BuildingWalkRules::PassesShare(0, 0, 0.0));
	TestEqual(TEXT("leg limit 0 m"), BuildingWalkRules::LegLimitSeconds(0.0), 20.0);
	TestEqual(TEXT("leg limit 30 m"), BuildingWalkRules::LegLimitSeconds(3000.0), 50.0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
