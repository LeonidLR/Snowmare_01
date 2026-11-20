// CodexTactics.LevelGen.MissionGoals.* - F-4 «а» (designer 2026-11-17): the level generator's goals as mission objectives. The runtime
// file codex.mission_goals/1 (Content/Data/LevelGen/objectives/<Map>.objectives.json, written by Tools/TrainingGround
// src/levelgen/missionGoals.ts), the reach rule (radius 2D + same floor; a room goal inside its room), free / sequence order, the
// zone-clear rule (designer 2026-11-20: no goal counts while a resisting enemy is in its zone), the zone outline and the HUD line.
// Pure, no world. UE-only (no Godot reference).

#include "LevelGen/MissionGoalsRules.h"
#include "Misc/AutomationTest.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

#define MISSION_GOALS_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.LevelGen.MissionGoals." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace MissionGoalsTestLocal
{
	const TCHAR* const ThreeGoals = TEXT(R"json({
		"schema": "codex.mission_goals/1", "map": "L_Gen_T", "layout": "t", "order": "free",
		"default_radius_cm": 300, "floor_tolerance_cm": 150,
		"goals": [
			{ "id": "GOAL_1", "kind": "street", "layer": "ground", "pos": [1000, 0, 0], "radius_cm": 300, "zone": "Z_ST_1" },
			{ "id": "GOAL_2", "kind": "room", "layer": "ground", "pos": [5000, 5000, 0], "building": "B_1",
			  "area": [[4000, 4000], [6000, 4000], [6000, 5500], [4000, 5500]] },
			{ "id": "GOAL_3", "kind": "yard", "layer": "roof:B_1", "pos": [-3000, 0, 600] }
		] })json");

	FMissionGoalsFile ParseOk(FAutomationTestBase& Test, const FString& Json)
	{
		FMissionGoalsFile File;
		TArray<FString> Errors;
		Test.TestTrue(TEXT("parses"), MissionGoalsRules::Parse(Json, File, Errors));
		for (const FString& Error : Errors)
		{
			Test.AddInfo(Error);
		}
		return File;
	}
}

MISSION_GOALS_TEST(FMissionGoalsParseTest, "Parse")
bool FMissionGoalsParseTest::RunTest(const FString&)
{
	using namespace MissionGoalsTestLocal;
	const FMissionGoalsFile File = ParseOk(*this, ThreeGoals);
	TestEqual(TEXT("map"), File.Map, FString(TEXT("L_Gen_T")));
	TestEqual(TEXT("three goals"), File.Goals.Num(), 3);
	TestTrue(TEXT("free order"), File.Order == EMissionGoalOrder::Free);
	if (File.Goals.Num() == 3)
	{
		TestTrue(TEXT("GOAL_1 street"), File.Goals[0].Kind == EMissionGoalKind::Street);
		TestEqual(TEXT("GOAL_1 location"), File.Goals[0].Location, FVector(1000.0, 0.0, 0.0));
		TestTrue(TEXT("GOAL_2 room with its area"), File.Goals[1].Kind == EMissionGoalKind::Room && File.Goals[1].Area.Num() == 4);
		TestEqual(TEXT("GOAL_2 takes the default radius"), File.Goals[1].RadiusCm, 300.f);
		TestEqual(TEXT("GOAL_3 roof layer"), File.Goals[2].Layer, FString(TEXT("roof:B_1")));
		TestEqual(TEXT("GOAL_3 z"), File.Goals[2].Location.Z, 600.0);
	}

	FMissionGoalsFile Bad;
	TArray<FString> Errors;
	TestFalse(TEXT("a newer schema is refused"), MissionGoalsRules::Parse(TEXT(R"({"schema":"codex.mission_goals/2","goals":[]})"), Bad, Errors));
	TestFalse(TEXT("no goals is refused"), MissionGoalsRules::Parse(TEXT(R"({"schema":"codex.mission_goals/1","goals":[]})"), Bad, Errors));
	TestFalse(TEXT("not JSON"), MissionGoalsRules::Parse(TEXT("nope"), Bad, Errors));
	Errors.Reset();
	TestTrue(TEXT("a bad goal is skipped, the rest kept"), MissionGoalsRules::Parse(TEXT(R"({"schema":"codex.mission_goals/1","order":"sequence","goals":[{"id":"A","pos":[1]},{"id":"B","pos":[0,0,0]},{"id":"B","pos":[1,1,0]}]})"), Bad, Errors));
	TestEqual(TEXT("only B survives"), Bad.Goals.Num(), 1);
	TestEqual(TEXT("two errors"), Errors.Num(), 2);
	TestTrue(TEXT("sequence order"), Bad.Order == EMissionGoalOrder::Sequence);
	return true;
}

MISSION_GOALS_TEST(FMissionGoalsReachTest, "Reach")
bool FMissionGoalsReachTest::RunTest(const FString&)
{
	using namespace MissionGoalsTestLocal;
	const FMissionGoalsFile File = ParseOk(*this, ThreeGoals);
	if (!TestEqual(TEXT("three goals"), File.Goals.Num(), 3))
	{
		return false;
	}
	const FMissionGoal& Street = File.Goals[0];
	TestTrue(TEXT("on the point"), MissionGoalsRules::IsAtGoal(Street, FVector(1000.0, 0.0, 0.0), 150.f));
	TestTrue(TEXT("299 cm away (2D)"), MissionGoalsRules::IsAtGoal(Street, FVector(1299.0, 0.0, 0.0), 150.f));
	TestFalse(TEXT("301 cm away"), MissionGoalsRules::IsAtGoal(Street, FVector(1301.0, 0.0, 0.0), 150.f));
	TestTrue(TEXT("140 cm higher still counts (same floor)"), MissionGoalsRules::IsAtGoal(Street, FVector(1000.0, 0.0, 140.0), 150.f));
	TestFalse(TEXT("a floor above does not"), MissionGoalsRules::IsAtGoal(Street, FVector(1000.0, 0.0, 300.0), 150.f));

	const FMissionGoal& Room = File.Goals[1];
	TestTrue(TEXT("room: a corner of the room, 1100 cm from its point"), MissionGoalsRules::IsAtGoal(Room, FVector(4100.0, 4100.0, 0.0), 150.f));
	TestFalse(TEXT("room: 200 cm from the point but behind the wall"), MissionGoalsRules::IsAtGoal(Room, FVector(5000.0, 5700.0, 0.0), 150.f));
	TestFalse(TEXT("room: the floor above"), MissionGoalsRules::IsAtGoal(Room, FVector(5000.0, 5000.0, 300.0), 150.f));

	FMissionGoal NoArea = Room;
	NoArea.Area.Reset();
	TestTrue(TEXT("a room goal without an area uses its radius"), MissionGoalsRules::IsAtGoal(NoArea, FVector(5000.0, 5250.0, 0.0), 150.f));

	TestTrue(TEXT("polygon inside"), MissionGoalsRules::IsInsidePolygon(FVector2D(5, 5), { FVector2D(0, 0), FVector2D(10, 0), FVector2D(10, 10), FVector2D(0, 10) }));
	TestFalse(TEXT("polygon outside"), MissionGoalsRules::IsInsidePolygon(FVector2D(15, 5), { FVector2D(0, 0), FVector2D(10, 0), FVector2D(10, 10), FVector2D(0, 10) }));
	TestFalse(TEXT("degenerate polygon"), MissionGoalsRules::IsInsidePolygon(FVector2D(0, 0), { FVector2D(0, 0), FVector2D(1, 1) }));
	return true;
}

MISSION_GOALS_TEST(FMissionGoalsOrderTest, "Order")
bool FMissionGoalsOrderTest::RunTest(const FString&)
{
	using namespace MissionGoalsTestLocal;
	FMissionGoalsFile File = ParseOk(*this, ThreeGoals);
	if (!TestEqual(TEXT("three goals"), File.Goals.Num(), 3))
	{
		return false;
	}
	const FVector AtGoal1(1000.0, 0.0, 0.0);
	const FVector AtGoal2(5000.0, 5000.0, 0.0);
	const FVector AtGoal3(-3000.0, 0.0, 600.0);
	const FVector Nowhere(20000.0, 20000.0, 0.0);

	// Free order: any goal, in any order; no victory before the last.
	TArray<bool> Reached;
	TArray<FMissionGoalHit> Hits = MissionGoalsRules::UpdateReached(File, Reached, { Nowhere });
	TestEqual(TEXT("nobody at a goal"), Hits.Num(), 0);
	TestEqual(TEXT("reached sized to the goals"), Reached.Num(), 3);
	Hits = MissionGoalsRules::UpdateReached(File, Reached, { Nowhere, AtGoal3 });
	TestTrue(TEXT("free: goal 3 first, by the second operative"), Hits.Num() == 1 && Hits[0].GoalIndex == 2 && Hits[0].OperativeIndex == 1);
	TestFalse(TEXT("not all after one"), MissionGoalsRules::AreAllReached(Reached));
	Hits = MissionGoalsRules::UpdateReached(File, Reached, { AtGoal3 });
	TestEqual(TEXT("a reached goal never counts twice"), Hits.Num(), 0);
	Hits = MissionGoalsRules::UpdateReached(File, Reached, { AtGoal1, AtGoal2 });
	TestEqual(TEXT("two operatives on two goals: both count"), Hits.Num(), 2);
	TestTrue(TEXT("all reached after the last"), MissionGoalsRules::AreAllReached(Reached));
	TestEqual(TEXT("count"), MissionGoalsRules::CountReached(Reached), 3);

	// Sequence: only the next goal counts.
	File.Order = EMissionGoalOrder::Sequence;
	Reached.Reset();
	Hits = MissionGoalsRules::UpdateReached(File, Reached, { AtGoal2 });
	TestEqual(TEXT("sequence: goal 2 before goal 1 does not count"), Hits.Num(), 0);
	TestTrue(TEXT("sequence: goal 1 can count"), MissionGoalsRules::CanCount(File.Order, Reached, 0));
	TestFalse(TEXT("sequence: goal 2 cannot yet"), MissionGoalsRules::CanCount(File.Order, Reached, 1));
	Hits = MissionGoalsRules::UpdateReached(File, Reached, { AtGoal1, AtGoal2 });
	TestTrue(TEXT("sequence: goal 1 then goal 2 in the same check"), Hits.Num() == 2 && Hits[0].GoalIndex == 0 && Hits[1].GoalIndex == 1);
	TestFalse(TEXT("sequence: not done before goal 3"), MissionGoalsRules::AreAllReached(Reached));
	Hits = MissionGoalsRules::UpdateReached(File, Reached, { AtGoal3 });
	TestTrue(TEXT("sequence: goal 3 last"), Hits.Num() == 1 && MissionGoalsRules::AreAllReached(Reached));

	TestFalse(TEXT("no goals = no goal victory"), MissionGoalsRules::AreAllReached(TArray<bool>()));
	return true;
}

MISSION_GOALS_TEST(FMissionGoalsHudLineTest, "HudLine")
bool FMissionGoalsHudLineTest::RunTest(const FString&)
{
	TestEqual(TEXT("1/3"), MissionGoalsRules::FormatHudLine(1, 3), FString(TEXT("GOALS: 1/3")));
	TestEqual(TEXT("clamped"), MissionGoalsRules::FormatHudLine(5, 3), FString(TEXT("GOALS: 3/3")));
	TestEqual(TEXT("zone not clear"), MissionGoalsRules::FormatHudLine(1, 3, true), FString(TEXT("GOALS: 1/3 - CLEAR THE ZONE")));
	TestTrue(TEXT("default path"), MissionGoalsRules::GetDefaultPathForMap(TEXT("L_Gen_Test_X2")).EndsWith(TEXT("Data/LevelGen/objectives/L_Gen_Test_X2.objectives.json")));
	TestEqual(TEXT("kind names"), FString(MissionGoalsRules::KindToString(MissionGoalsRules::ParseKind(TEXT("Room")))), FString(TEXT("room")));
	return true;
}

MISSION_GOALS_TEST(FMissionGoalsFloorToleranceParseTest, "FloorToleranceKept")
bool FMissionGoalsFloorToleranceParseTest::RunTest(const FString&)
{
	// Regression: a misplaced else reset floor_tolerance_cm to the default whenever label_world_size_cm was absent.
	FMissionGoalsFile File;
	TArray<FString> Errors;
	TestTrue(TEXT("parses"), MissionGoalsRules::Parse(TEXT(R"({"schema":"codex.mission_goals/1","floor_tolerance_cm":220,"goals":[{"id":"A","pos":[0,0,0]}]})"), File, Errors));
	TestEqual(TEXT("the file's floor tolerance is kept"), File.FloorToleranceCm, 220.f);
	TestTrue(TEXT("parses without a tolerance"), MissionGoalsRules::Parse(TEXT(R"({"schema":"codex.mission_goals/1","goals":[{"id":"A","pos":[0,0,0]}]})"), File, Errors));
	TestEqual(TEXT("default tolerance"), File.FloorToleranceCm, MissionGoalsRules::DefaultFloorToleranceCm);
	return true;
}

MISSION_GOALS_TEST(FMissionGoalsResistingTest, "ZoneClear.Resisting")
bool FMissionGoalsResistingTest::RunTest(const FString&)
{
	// Designer 2026-11-20: only an enemy that still resists keeps a zone; the dead / dying do not.
	FMissionGoalHostile Alive;
	Alive.Feet = FVector(1.0, 2.0, 3.0);
	FMissionGoalHostile Dead = Alive;
	Dead.bAlive = false;
	FMissionGoalHostile Dying = Alive;
	Dying.bDying = true;
	TestTrue(TEXT("a living enemy resists"), MissionGoalsRules::IsResisting(Alive));
	TestFalse(TEXT("a dead one does not"), MissionGoalsRules::IsResisting(Dead));
	TestFalse(TEXT("a dying / killed-in-action one does not"), MissionGoalsRules::IsResisting(Dying));
	const TArray<FVector> Feet = MissionGoalsRules::ResistingFeet({ Dead, Alive, Dying });
	TestTrue(TEXT("only the living one's feet"), Feet.Num() == 1 && Feet[0] == Alive.Feet);
	TestEqual(TEXT("nobody"), MissionGoalsRules::ResistingFeet({}).Num(), 0);
	return true;
}

MISSION_GOALS_TEST(FMissionGoalsContestedTest, "ZoneClear.Contested")
bool FMissionGoalsContestedTest::RunTest(const FString&)
{
	using namespace MissionGoalsTestLocal;
	const FMissionGoalsFile File = ParseOk(*this, ThreeGoals);
	if (!TestEqual(TEXT("three goals"), File.Goals.Num(), 3))
	{
		return false;
	}
	const FMissionGoal& Street = File.Goals[0];
	const FMissionGoal& Room = File.Goals[1];
	const float Tol = File.FloorToleranceCm;
	TestFalse(TEXT("no enemies: clear"), MissionGoalsRules::IsGoalContested(Street, {}, Tol));
	TestTrue(TEXT("an enemy 250 cm from the point: contested"), MissionGoalsRules::IsGoalContested(Street, { FVector(1250.0, 0.0, 0.0) }, Tol));
	TestFalse(TEXT("an enemy 350 cm away (outside the 300 cm zone): clear"), MissionGoalsRules::IsGoalContested(Street, { FVector(1350.0, 0.0, 0.0) }, Tol));
	TestFalse(TEXT("an enemy on the floor above: clear"), MissionGoalsRules::IsGoalContested(Street, { FVector(1000.0, 0.0, 320.0) }, Tol));
	TestTrue(TEXT("one of several inside is enough"),
		MissionGoalsRules::IsGoalContested(Street, { FVector(9000.0, 0.0, 0.0), FVector(1000.0, 100.0, 0.0) }, Tol));
	TestTrue(TEXT("room: an enemy in the room's corner, far from its point"), MissionGoalsRules::IsGoalContested(Room, { FVector(4100.0, 5400.0, 0.0) }, Tol));
	TestFalse(TEXT("room: an enemy behind the wall, near its point"), MissionGoalsRules::IsGoalContested(Room, { FVector(5000.0, 5700.0, 0.0) }, Tol));
	return true;
}

MISSION_GOALS_TEST(FMissionGoalsBlockedReachTest, "ZoneClear.BlocksReach")
bool FMissionGoalsBlockedReachTest::RunTest(const FString&)
{
	using namespace MissionGoalsTestLocal;
	const FMissionGoalsFile File = ParseOk(*this, ThreeGoals);
	if (!TestEqual(TEXT("three goals"), File.Goals.Num(), 3))
	{
		return false;
	}
	const FVector AtGoal1(1000.0, 0.0, 0.0);
	const FVector EnemyAtGoal1(1200.0, 50.0, 0.0);
	const FVector EnemyAtGoal2(4500.0, 4500.0, 0.0);

	TArray<bool> Reached;
	TArray<int32> Blocked;
	TArray<FMissionGoalHit> Hits = MissionGoalsRules::UpdateReached(File, Reached, { AtGoal1 }, { EnemyAtGoal1 }, &Blocked);
	TestEqual(TEXT("an enemy in the zone: the goal does not count"), Hits.Num(), 0);
	TestFalse(TEXT("goal 1 not reached"), Reached[0]);
	TestTrue(TEXT("goal 1 reported blocked"), Blocked.Num() == 1 && Blocked[0] == 0);

	Hits = MissionGoalsRules::UpdateReached(File, Reached, { AtGoal1, AtGoal1 + FVector(80.0, 0.0, 0.0) }, { EnemyAtGoal1 }, &Blocked);
	TestEqual(TEXT("more operatives do not outweigh a resisting enemy"), Hits.Num(), 0);
	TestEqual(TEXT("blocked once, not per operative"), Blocked.Num(), 1);

	TArray<int32> NotBlocked;
	Hits = MissionGoalsRules::UpdateReached(File, Reached, { FVector(20000.0, 0.0, 0.0) }, { EnemyAtGoal1 }, &NotBlocked);
	TestEqual(TEXT("an enemy in a zone with nobody of ours there: nothing blocked"), NotBlocked.Num(), 0);

	FMissionGoalHostile Killed;
	Killed.Feet = EnemyAtGoal1;
	Killed.bAlive = false;
	Hits = MissionGoalsRules::UpdateReached(File, Reached, { AtGoal1 }, MissionGoalsRules::ResistingFeet({ Killed }), &Blocked);
	TestTrue(TEXT("the enemy killed: the cleared zone counts at once"), Hits.Num() == 1 && Hits[0].GoalIndex == 0 && Reached[0]);
	TestEqual(TEXT("nothing blocked after"), Blocked.Num(), 0);

	Reached.Reset();
	Hits = MissionGoalsRules::UpdateReached(File, Reached, { AtGoal1 }, { EnemyAtGoal2 }, &Blocked);
	TestTrue(TEXT("an enemy in ANOTHER goal's zone does not block this one"), Hits.Num() == 1 && Hits[0].GoalIndex == 0);

	Reached.Reset();
	Hits = MissionGoalsRules::UpdateReached(File, Reached, { AtGoal1 }, { FVector(1000.0, 0.0, 400.0) });
	TestEqual(TEXT("an enemy on the floor above does not block (and no OutBlocked is fine)"), Hits.Num(), 1);
	return true;
}

MISSION_GOALS_TEST(FMissionGoalsBlockedSequenceTest, "ZoneClear.Sequence")
bool FMissionGoalsBlockedSequenceTest::RunTest(const FString&)
{
	using namespace MissionGoalsTestLocal;
	FMissionGoalsFile File = ParseOk(*this, ThreeGoals);
	if (!TestEqual(TEXT("three goals"), File.Goals.Num(), 3))
	{
		return false;
	}
	File.Order = EMissionGoalOrder::Sequence;
	const FVector AtGoal1(1000.0, 0.0, 0.0);
	const FVector AtGoal2(5000.0, 5000.0, 0.0);
	TArray<bool> Reached;
	TArray<int32> Blocked;
	TArray<FMissionGoalHit> Hits = MissionGoalsRules::UpdateReached(File, Reached, { AtGoal1, AtGoal2 }, { AtGoal1 + FVector(100.0, 0.0, 0.0) }, &Blocked);
	TestEqual(TEXT("sequence: a contested goal 1 holds the chain (goal 2 not counted either)"), Hits.Num(), 0);
	TestTrue(TEXT("only goal 1 is blocked (goal 2 is not countable yet)"), Blocked.Num() == 1 && Blocked[0] == 0);
	Hits = MissionGoalsRules::UpdateReached(File, Reached, { AtGoal1, AtGoal2 }, {}, &Blocked);
	TestTrue(TEXT("cleared: goal 1 then goal 2 in the same check"), Hits.Num() == 2 && Hits[0].GoalIndex == 0 && Hits[1].GoalIndex == 1);

	Reached.Reset();
	Hits = MissionGoalsRules::UpdateReached(File, Reached, { AtGoal1, AtGoal2 }, { FVector(5000.0, 5100.0, 0.0) }, &Blocked);
	TestTrue(TEXT("sequence: goal 1 clear counts, contested goal 2 waits"), Hits.Num() == 1 && Hits[0].GoalIndex == 0);
	TestTrue(TEXT("goal 2 blocked"), Blocked.Num() == 1 && Blocked[0] == 1);
	return true;
}

MISSION_GOALS_TEST(FMissionGoalsZoneOutlineTest, "ZoneClear.Outline")
bool FMissionGoalsZoneOutlineTest::RunTest(const FString&)
{
	using namespace MissionGoalsTestLocal;
	const FMissionGoalsFile File = ParseOk(*this, ThreeGoals);
	if (!TestEqual(TEXT("three goals"), File.Goals.Num(), 3))
	{
		return false;
	}
	// A street goal: a circle of its real radius (data: radius_cm 300) at its floor height.
	const TArray<FVector> Circle = MissionGoalsRules::BuildZoneOutline(File.Goals[0], 36);
	TestEqual(TEXT("36 segments"), Circle.Num(), 36);
	bool bOnRadius = true;
	for (const FVector& Point : Circle)
	{
		bOnRadius &= FMath::IsNearlyEqual(FVector2D::Distance(FVector2D(Point), FVector2D(1000.0, 0.0)), 300.0, 0.01) && Point.Z == 0.0;
	}
	TestTrue(TEXT("every point on the 300 cm radius, at the goal's height"), bOnRadius);
	TestEqual(TEXT("segments clamped up"), MissionGoalsRules::BuildZoneOutline(File.Goals[0], 2).Num(), 8);
	TestEqual(TEXT("segments clamped down"), MissionGoalsRules::BuildZoneOutline(File.Goals[0], 1000).Num(), 128);

	// The roof yard goal takes the file default radius and its roof height.
	const TArray<FVector> Roof = MissionGoalsRules::BuildZoneOutline(File.Goals[2], 16);
	TestTrue(TEXT("roof goal outline at z 600 on the default radius"), Roof.Num() == 16 && Roof[0].Z == 600.0
		&& FMath::IsNearlyEqual(FVector2D::Distance(FVector2D(Roof[5]), FVector2D(-3000.0, 0.0)), static_cast<double>(File.DefaultRadiusCm), 0.01));

	// A room goal: its room polygon, not a circle.
	const TArray<FVector> Room = MissionGoalsRules::BuildZoneOutline(File.Goals[1], 36);
	TestTrue(TEXT("room: the four room corners"), Room.Num() == 4 && Room[0] == FVector(4000.0, 4000.0, 0.0) && Room[2] == FVector(6000.0, 5500.0, 0.0));
	return true;
}

MISSION_GOALS_TEST(FMissionGoalsCommittedFilesTest, "CommittedFiles")
bool FMissionGoalsCommittedFilesTest::RunTest(const FString&)
{
	// The three generated maps' files (Tools/TrainingGround scripts/export-mission-goals.ts) parse with every goal.
	struct FExpected { const TCHAR* Map; int32 Goals; };
	for (const FExpected& Expected : { FExpected{ TEXT("L_Gen_Test_X2"), 2 }, FExpected{ TEXT("L_Gen_Depot_X4"), 3 }, FExpected{ TEXT("L_Gen_Depot_1_X6"), 3 } })
	{
		FString Json;
		if (!TestTrue(FString::Printf(TEXT("%s file readable"), Expected.Map), FFileHelper::LoadFileToString(Json, *MissionGoalsRules::GetDefaultPathForMap(Expected.Map))))
		{
			continue;
		}
		FMissionGoalsFile File;
		TArray<FString> Errors;
		TestTrue(FString::Printf(TEXT("%s parses"), Expected.Map), MissionGoalsRules::Parse(Json, File, Errors));
		TestEqual(FString::Printf(TEXT("%s no errors"), Expected.Map), Errors.Num(), 0);
		TestEqual(FString::Printf(TEXT("%s goals"), Expected.Map), File.Goals.Num(), Expected.Goals);
		TestEqual(FString::Printf(TEXT("%s map name"), Expected.Map), File.Map, FString(Expected.Map));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
