// CodexTactics.Formation.WallRoute.* - one route round an obstacle for the group move into the wall layout (the game designer's play-test
// 2026-11-01 on L_FormationLab: "the group must not split that much"; approved fixes d / e; FormationWallRoute). Godot reference: none
// (UE-only).

#include "Formation/FormationWallRouteRules.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#define WALL_ROUTE_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Formation.WallRoute." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WallRouteTest
{
	/** The lab's W_C_8: 15 m along X at y -1500, 40 cm thick (its faces at y -1520 / -1480). */
	FFormationWall LabWall()
	{
		FFormationWall Wall;
		Wall.A = FVector2D(3000.0, -1500.0);
		Wall.B = FVector2D(4500.0, -1500.0);
		Wall.Thickness = 40.f;
		return Wall;
	}
}

WALL_ROUTE_TEST(FWallRouteNeedsRouteTest, "NeedsRoute")
bool FWallRouteNeedsRouteTest::RunTest(const FString&)
{
	using namespace FormationWallRoute;
	const TArray<FFormationWall> Walls{ WallRouteTest::LabWall() };
	// The split of screenshots 3-5: men on the near side, their wall slots on the far face - each straight way crosses the wall.
	TestTrue(TEXT("near side -> far-face slot: routed"), NeedsRoute(FVector2D(3650.0, -2635.0), FVector2D(3650.0, -1435.0), Walls));
	TestTrue(TEXT("diagonal across the wall: routed"), NeedsRoute(FVector2D(3850.0, -2635.0), FVector2D(3650.0, -1435.0), Walls));
	// Along the same face (a man 45 cm off it walking to the next slot): clear.
	TestFalse(TEXT("along the near face: no route"), NeedsRoute(FVector2D(3650.0, -1565.0), FVector2D(3850.0, -1565.0), Walls));
	TestFalse(TEXT("along the far face: no route"), NeedsRoute(FVector2D(3650.0, -1435.0), FVector2D(3850.0, -1435.0), Walls));
	// Round the end: once past it the way is clear - he is released into his slot.
	TestFalse(TEXT("past the west end -> the far face: clear"), NeedsRoute(FVector2D(2900.0, -1400.0), FVector2D(3650.0, -1435.0), Walls));
	TestTrue(TEXT("still at the corner's near side: routed"), NeedsRoute(FVector2D(3050.0, -1580.0), FVector2D(3650.0, -1435.0), Walls));
	TestFalse(TEXT("no walls: never"), NeedsRoute(FVector2D(3650.0, -2635.0), FVector2D(3650.0, -1435.0), TArray<FFormationWall>()));
	return true;
}

WALL_ROUTE_TEST(FWallRoutePlacesAlongTrailTest, "PlacesAlongTrail")
bool FWallRoutePlacesAlongTrailTest::RunTest(const FString&)
{
	using namespace FormationWallRoute;
	// The leader walked from (0, 0) east 600 cm, then north 400 cm (a corner round a wall's end): places 180 / 330 / 480 back.
	const TArray<FVector2D> Trail{ FVector2D(0.0, 0.0), FVector2D(600.0, 0.0), FVector2D(600.0, 400.0) };
	const TArray<FPlace> Places = PlacesAlongTrail(Trail, 3, 180.f, 150.f);
	TestEqual(TEXT("three places"), Places.Num(), 3);
	if (Places.Num() == 3)
	{
		TestTrue(TEXT("1st 180 cm back on the north leg"), Places[0].Pos.Equals(FVector2D(600.0, 220.0), 0.5));
		TestTrue(TEXT("1st looks north (his travel)"), FMath::IsNearlyEqual(Places[0].Yaw, 90.f, 0.5f));
		TestTrue(TEXT("2nd 330 cm back on the north leg"), Places[1].Pos.Equals(FVector2D(600.0, 70.0), 0.5));
		TestTrue(TEXT("3rd 480 cm back: round the corner, on the east leg"), Places[2].Pos.Equals(FVector2D(520.0, 0.0), 0.5));
		TestTrue(TEXT("3rd looks east (the leg he is on)"), FMath::IsNearlyEqual(Places[2].Yaw, 0.f, 0.5f));
	}
	// A short path (the order just began): clamped to where the leader started - never extrapolated behind it (a wall may stand there).
	const TArray<FVector2D> Short{ FVector2D(100.0, 100.0), FVector2D(150.0, 100.0) };
	const TArray<FPlace> Clamped = PlacesAlongTrail(Short, 2, 180.f, 150.f);
	TestEqual(TEXT("two places on a 50 cm path"), Clamped.Num(), 2);
	if (Clamped.Num() == 2)
	{
		TestTrue(TEXT("both at the path's start"), Clamped[0].Pos.Equals(FVector2D(100.0, 100.0), 0.5) && Clamped[1].Pos.Equals(FVector2D(100.0, 100.0), 0.5));
		TestTrue(TEXT("looking the way he set off"), FMath::IsNearlyEqual(Clamped[0].Yaw, 0.f, 0.5f));
	}
	TestEqual(TEXT("one point: at it"), PlacesAlongTrail({ FVector2D(5.0, 6.0) }, 1, 180.f, 150.f).Num(), 1);
	TestEqual(TEXT("no path: none"), PlacesAlongTrail(TArray<FVector2D>(), 2, 180.f, 150.f).Num(), 0);
	TestEqual(TEXT("no places asked: none"), PlacesAlongTrail(Trail, 0, 180.f, 150.f).Num(), 0);
	return true;
}

WALL_ROUTE_TEST(FWallRouteAssignTest, "AssignColumnPlaces")
bool FWallRouteAssignTest::RunTest(const FString&)
{
	using namespace FormationWallRoute;
	// Two routed men; the column's two nearest places (the 3rd is not used): the nearer man takes the place nearer to him.
	const TArray<FVector2D> Places{ FVector2D(600.0, 220.0), FVector2D(600.0, 70.0), FVector2D(520.0, 0.0) };
	const TArray<int32> Two = AssignColumnPlaces({ FVector2D(610.0, 60.0), FVector2D(605.0, 240.0) }, Places);
	TestEqual(TEXT("two assignments"), Two.Num(), 2);
	if (Two.Num() == 2)
	{
		TestEqual(TEXT("the man at the 2nd place keeps it"), Two[0], 1);
		TestEqual(TEXT("the man at the 1st place keeps it"), Two[1], 0);
	}
	const TArray<int32> One = AssignColumnPlaces({ FVector2D(520.0, 0.0) }, Places);
	TestTrue(TEXT("one routed man: the 1st place (right behind the leader), not the one he stands at"), One.Num() == 1 && One[0] == 0);
	TestEqual(TEXT("more men than places: none"), AssignColumnPlaces({ FVector2D::ZeroVector, FVector2D::ZeroVector }, { FVector2D::ZeroVector }).Num(), 0);
	TestEqual(TEXT("nobody routed: none"), AssignColumnPlaces(TArray<FVector2D>(), Places).Num(), 0);
	return true;
}

#undef WALL_ROUTE_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
