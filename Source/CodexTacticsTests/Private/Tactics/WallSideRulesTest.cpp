// CodexTactics.Tactics.WallSide.* - the wall side a cursor click means and the tight operative pick at a wall (the game designer's play-test
// 2026-11-01 on L_FormationLab, approved fixes a / b; WallSideRules / ClickPickRules). Godot reference: none (UE-only).

#include "Tactics/WallSideRules.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#define WALL_SIDE_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Tactics.WallSide." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

WALL_SIDE_TEST(FWallSideTopSplitTest, "TopSplitAtTheMiddle")
bool FWallSideTopSplitTest::RunTest(const FString&)
{
	using namespace WallSideRules;
	// A 40 cm wall seen from the south: its near face at 1000 along the view, the far face at 1040; the middle at 1020.
	TestTrue(TEXT("the top's near edge -> the near face"), SideOfTopHit(1001.f, 1000.f, 1040.f) == EWallClickSide::Near);
	TestTrue(TEXT("just short of the middle -> near"), SideOfTopHit(1019.f, 1000.f, 1040.f) == EWallClickSide::Near);
	TestTrue(TEXT("exactly the middle -> near (the face the player sees wins a tie)"), SideOfTopHit(1020.f, 1000.f, 1040.f) == EWallClickSide::Near);
	TestTrue(TEXT("just past the middle -> the far face"), SideOfTopHit(1021.f, 1000.f, 1040.f) == EWallClickSide::Far);
	TestTrue(TEXT("the top's far edge -> far"), SideOfTopHit(1039.f, 1000.f, 1040.f) == EWallClickSide::Far);
	// A thick wall (a 3 m block): the same split.
	TestTrue(TEXT("a 3 m block: past its middle -> far"), SideOfTopHit(1200.f, 1000.f, 1300.f) == EWallClickSide::Far);
	TestTrue(TEXT("a 3 m block: before its middle -> near"), SideOfTopHit(1100.f, 1000.f, 1300.f) == EWallClickSide::Near);
	// Not a wall: the faces in the wrong order, or farther apart than a wall is thick (a building's two sides, a floor slab).
	TestTrue(TEXT("faces swapped -> none"), SideOfTopHit(1020.f, 1040.f, 1000.f) == EWallClickSide::None);
	TestTrue(TEXT("a 5 m deep top -> none (no wall)"), SideOfTopHit(1300.f, 1000.f, 1500.f) == EWallClickSide::None);
	TestTrue(TEXT("zero thickness -> none"), SideOfTopHit(1000.f, 1000.f, 1000.f) == EWallClickSide::None);
	return true;
}

WALL_SIDE_TEST(FWallSideFarGroundBandTest, "FarGroundBand")
bool FWallSideFarGroundBandTest::RunTest(const FString&)
{
	using namespace WallSideRules;
	// The ground a wall hides from the camera: height / tan(pitch), + 1 m, clamped 1.5 .. 4.5 m.
	TestTrue(TEXT("3 m wall, camera 45 deg down: 3 m hidden + 1 m = 4 m"), FMath::IsNearlyEqual(FarGroundReachCm(300.f, -45.f), 400.f, 0.5f));
	TestTrue(TEXT("3 m wall, camera 60 deg down: 1.73 m + 1 m"), FMath::IsNearlyEqual(FarGroundReachCm(300.f, -60.f), 273.2f, 0.5f));
	TestTrue(TEXT("3 m wall, the iso ray 30 deg down: 5.2 m hidden + 1 m"), FMath::IsNearlyEqual(FarGroundReachCm(300.f, -30.f), 619.6f, 0.5f));
	TestEqual(TEXT("a steep camera never below the minimum"), FarGroundReachCm(100.f, -85.f), FarGroundReachMinCm);
	TestEqual(TEXT("a flat camera: the maximum"), FarGroundReachCm(300.f, -2.f), FarGroundReachMaxCm);
	TestEqual(TEXT("a very high wall: capped at the maximum"), FarGroundReachCm(1200.f, -45.f), FarGroundReachMaxCm);
	TestEqual(TEXT("an unknown height: the minimum"), FarGroundReachCm(0.f, -50.f), FarGroundReachMinCm);
	TestEqual(TEXT("an upward pitch counts as its size"), FarGroundReachCm(300.f, 45.f), FarGroundReachCm(300.f, -45.f));
	return true;
}

WALL_SIDE_TEST(FWallSideGroundBehindTest, "GroundBehindWall")
bool FWallSideGroundBehindTest::RunTest(const FString&)
{
	using namespace WallSideRules;
	// The face 1.2 m back towards the camera, pointing away from it (the far face), a high wall, the leader beyond it: the far face.
	TestTrue(TEXT("the ground right behind a high wall, leader on the other side -> far"), IsGroundBehindWall(120.f, 300.f, 0.95f, true, true));
	TestTrue(TEXT("at the band's edge"), IsGroundBehindWall(300.f, 300.f, 0.95f, true, true));
	TestTrue(TEXT("a wall at 50 deg to the view still counts (cos 50 = 0.64)"), IsGroundBehindWall(120.f, 300.f, 0.64f, true, true));
	TestFalse(TEXT("past the band: a plain move"), IsGroundBehindWall(320.f, 300.f, 0.95f, true, true));
	TestFalse(TEXT("the face points at the camera (the near face of the next wall): no"), IsGroundBehindWall(120.f, 300.f, -0.95f, true, true));
	TestFalse(TEXT("an end face along the view (cos 80 deg): no"), IsGroundBehindWall(120.f, 300.f, 0.17f, true, true));
	TestFalse(TEXT("a low wall (seen over): a plain move / vault"), IsGroundBehindWall(120.f, 300.f, 0.95f, false, true));
	TestFalse(TEXT("the leader on the same side: a move / slide along his wall"), IsGroundBehindWall(120.f, 300.f, 0.95f, true, false));
	TestFalse(TEXT("a negative distance is no hit"), IsGroundBehindWall(-1.f, 300.f, 0.95f, true, true));
	return true;
}

WALL_SIDE_TEST(FWallSideTightPickTest, "TightPickAtAWall")
bool FWallSideTightPickTest::RunTest(const FString&)
{
	using namespace ClickPickRules;
	// Not at a wall: the old 120 cm proximity pick, ground or not.
	TestEqual(TEXT("in the open: the select radius on the ground"), PickRadius(false, true, 120.f), 120.f);
	TestEqual(TEXT("in the open: the select radius on a wall top too (the old rule)"), PickRadius(false, false, 120.f), 120.f);
	// At a wall (b): only the body - the ground at his feet within 45 cm, never a wall's top / face.
	TestEqual(TEXT("at a wall: the ground within 45 cm of a body"), PickRadius(true, true, 120.f), TightPickRadiusCm);
	TestEqual(TEXT("at a wall: a wall top / face never picks"), PickRadius(true, false, 120.f), 0.f);
	TestEqual(TEXT("a smaller select radius stays the cap"), PickRadius(true, true, 30.f), 30.f);
	TestTrue(TEXT("the tight radius is a body, well inside the old one"), TightPickRadiusCm < 120.f * 0.5f);
	return true;
}

#undef WALL_SIDE_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
