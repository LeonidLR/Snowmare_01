// CodexTactics.Formation.V26.* - formation rules v2.6 (the game designer's answers 2026-11-01 to the red team v2.5 questions;
// docs/design/FORMATION_CONTRACT.md 18, FORMATION_SECTORS.md "v2.6"): Q1 = b - a post / a short stub beside the path is no side wall
// (FormationTerrainRules::SideWalls / ObstacleGroups / IsShortSideObstacleAt), Q2 = b - the lagging rear guard turns round and catches up
// facing forwards (FormationSectorRules::RearCatchUp). Reference: Tools/TrainingGround/src/movement/rules.ts sideWalls / obstacleGroups /
// RearCatchUp, rulesV26.test.ts; parity CodexTactics.Formation.Parity.Classify / .PassageWidth / .RearCatchUp. UE-only (no Godot reference).

#include "Misc/AutomationTest.h"
#include "Formation/FormationGuardRuntimeRules.h"
#include "Formation/FormationRuntimeRules.h"
#include "Formation/FormationSectorRules.h"
#include "Formation/FormationTerrainRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define FORMATION_V26_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Formation.V26." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace FormationV26TestUtil
{
	FFormationWall MakeV26Wall(double AX, double AY, double BX, double BY, float Thickness)
	{
		FFormationWall Wall;
		Wall.A = FVector2D(AX, AY);
		Wall.B = FVector2D(BX, BY);
		Wall.Thickness = Thickness;
		return Wall;
	}
}

FORMATION_V26_TEST(FFormationV26ShortSideTest, "ShortSideObstacleIsNoSideWall")
bool FFormationV26ShortSideTest::RunTest(const FString&)
{
	using namespace FormationV26TestUtil;
	const FFormationConfig Config;
	TestEqual(TEXT("the designer's threshold: 1.5 m along the path"), Config.ClassifySideMinLength, 150.f);
	// Red team 14_1: a 60 x 40 cm pillar 2.4 m to the left of a leader walking +X (100 cm along the heading).
	const TArray<FFormationWall> Pillar = { MakeV26Wall(-30.0, -240.0, 30.0, -240.0, 40.f) };
	TestEqual(TEXT("the pillar is left out of the side walls"), FormationTerrainRules::SideWalls(Pillar, 0.f, Config).Num(), 0);
	TestEqual(TEXT("a pillar beside the path: open ground (no echelon)"), FormationTerrainRules::ClassifyRaw(Pillar, FVector2D::ZeroVector, 0.f, Config),
		EFormationTerrainClass::Open);
	FFormationConfig Off = Config;
	Off.ClassifySideMinLength = 0.f;
	TestEqual(TEXT("side_min_length 0 = off: the pillar is a wall on the left (v2.5.1)"), FormationTerrainRules::ClassifyRaw(Pillar, FVector2D::ZeroVector, 0.f, Off),
		EFormationTerrainClass::WallLeft);
	// The same pillar seen walking along Y: its extent along that heading is its 40 cm thickness - still short.
	TestEqual(TEXT("any heading: the extent is measured along it"), FormationTerrainRules::SideWalls(Pillar, 90.f, Config).Num(), 0);
	// A 2 m wall is long enough.
	const TArray<FFormationWall> Short200 = { MakeV26Wall(-80.0, -250.0, 80.0, -250.0, 40.f) };
	TestEqual(TEXT("a 2 m wall beside the path: wall_left"), FormationTerrainRules::ClassifyRaw(Short200, FVector2D::ZeroVector, 0.f, Config),
		EFormationTerrainClass::WallLeft);
	// A wall of touching 1 m pieces is one 5.2 m obstacle.
	TArray<FFormationWall> Pieces;
	for (int32 Index = 0; Index < 5; ++Index)
	{
		Pieces.Add(MakeV26Wall(-250.0 + 100.0 * Index, -260.0, -150.0 + 100.0 * Index, -260.0, 20.f));
	}
	const TArray<int32> Groups = FormationTerrainRules::ObstacleGroups(Pieces, Config.ClassifySideJoinGap);
	TestTrue(TEXT("the pieces form one obstacle (group 0)"), Groups.Num() == 5 && Groups.FindByPredicate([](int32 Group) { return Group != 0; }) == nullptr);
	TestEqual(TEXT("touching pieces: wall_left"), FormationTerrainRules::ClassifyRaw(Pieces, FVector2D::ZeroVector, 0.f, Config), EFormationTerrainClass::WallLeft);
	// Posts 1.4 m apart (gaps 1.1 m > the join gap): each its own short obstacle.
	TArray<FFormationWall> Posts;
	for (const double X : { -200.0, -60.0, 80.0, 220.0 })
	{
		Posts.Add(MakeV26Wall(X, 260.0, X, 261.0, 30.f));
	}
	TestEqual(TEXT("a row of posts: open"), FormationTerrainRules::ClassifyRaw(Posts, FVector2D::ZeroVector, 0.f, Config), EFormationTerrainClass::Open);
	// A long wall on the left, a pillar on the right: no corridor, and the passage width reads the open right side.
	const TArray<FFormationWall> WallAndPillar = { MakeV26Wall(-2000.0, -250.0, 2000.0, -250.0, 40.f), MakeV26Wall(-30.0, 240.0, 30.0, 240.0, 40.f) };
	TestEqual(TEXT("wall left + pillar right: wall_left"), FormationTerrainRules::ClassifyRaw(WallAndPillar, FVector2D::ZeroVector, 0.f, Config),
		EFormationTerrainClass::WallLeft);
	TestFalse(TEXT("no passage width (the right side is open)"), FormationTerrainRules::PassageWidth(WallAndPillar, FVector2D::ZeroVector, 0.f, Config).IsSet());
	// A runtime probe hit (a 20 cm trace piece) is always kept: the runtime filters short obstacles itself.
	FFormationWall Probe = FormationRuntime::WallFromHit(FVector2D(0.0, -240.0), FVector2D(0.0, 1.0));
	TestTrue(TEXT("WallFromHit marks a probe hit"), Probe.bProbeHit);
	TestEqual(TEXT("a probe hit stays a side wall"), FormationTerrainRules::ClassifyRaw({ Probe }, FVector2D::ZeroVector, 0.f, Config), EFormationTerrainClass::WallLeft);
	return true;
}

FORMATION_V26_TEST(FFormationV26ShortSideRuntimeTest, "ProbeHitOnBoxWalls")
bool FFormationV26ShortSideRuntimeTest::RunTest(const FString&)
{
	using namespace FormationV26TestUtil;
	const FFormationConfig Config;
	// The level's box collision as the runtime reads it (FormationGuardRuntime::WallsFromBox): a 60 x 40 pillar at (0, -240), a 10 m x 40 cm
	// wall at y +300, a 4 x 4 m bulky crate at (1000, -400) (its four sides) and a 1 m stub touching the long wall (a T joint).
	TArray<FFormationWall> Box;
	FormationGuardRuntime::WallsFromBox(FVector2D(0.0, -240.0), FVector2D(30.0, 0.0), FVector2D(0.0, 20.0), Box);
	FormationGuardRuntime::WallsFromBox(FVector2D(0.0, 300.0), FVector2D(500.0, 0.0), FVector2D(0.0, 20.0), Box);
	FormationGuardRuntime::WallsFromBox(FVector2D(1000.0, -400.0), FVector2D(200.0, 0.0), FVector2D(0.0, 200.0), Box);
	FormationGuardRuntime::WallsFromBox(FVector2D(300.0, 230.0), FVector2D(0.0, 50.0), FVector2D(20.0, 0.0), Box);
	// A side ray from the leader at the origin heading +X hits the pillar's face at y -220.
	TestTrue(TEXT("a hit on the pillar's face: a short obstacle (dropped)"),
		FormationTerrainRules::IsShortSideObstacleAt(Box, FVector2D(0.0, -220.0), 0.f, Config));
	TestFalse(TEXT("a hit on the long wall: kept"), FormationTerrainRules::IsShortSideObstacleAt(Box, FVector2D(0.0, 280.0), 0.f, Config));
	TestFalse(TEXT("a hit on the stub joined to the long wall: kept (one long obstacle)"),
		FormationTerrainRules::IsShortSideObstacleAt(Box, FVector2D(280.0, 210.0), 0.f, Config));
	TestFalse(TEXT("a hit on the bulky crate's side (its four sides joined: 4 m): kept"),
		FormationTerrainRules::IsShortSideObstacleAt(Box, FVector2D(1000.0, -200.0), 0.f, Config));
	TestFalse(TEXT("a hit on nothing the box walls know (a complex-collision mesh): kept"),
		FormationTerrainRules::IsShortSideObstacleAt(Box, FVector2D(-600.0, -600.0), 0.f, Config));
	FFormationConfig Off = Config;
	Off.ClassifySideMinLength = 0.f;
	TestFalse(TEXT("side_min_length 0 = off: nothing is dropped"), FormationTerrainRules::IsShortSideObstacleAt(Box, FVector2D(0.0, -220.0), 0.f, Off));
	return true;
}

FORMATION_V26_TEST(FFormationV26RearCatchUpTest, "RearGuardTurnsRoundToCatchUp")
bool FFormationV26RearCatchUpTest::RunTest(const FString&)
{
	const FFormationConfig Config;
	TestEqual(TEXT("turns round beyond 1.5 m"), Config.MoveRearTurnLag, 150.f);
	TestEqual(TEXT("walks backwards again within 0.5 m"), Config.MoveRearTurnBack, 50.f);
	// A lag profile after a bypass: he falls 2 m behind, catches up, then wobbles round 1 m (the slot's jitter) - one turn out and one back.
	const float Lags[] = { 40.f, 120.f, 151.f, 200.f, 160.f, 120.f, 80.f, 140.f, 100.f, 60.f, 50.f, 45.f, 70.f, 110.f, 149.f, 40.f };
	bool bTurned = false;
	int32 Changes = 0;
	TArray<bool> Seen;
	for (const float Lag : Lags)
	{
		const bool bNext = FormationSectorRules::RearCatchUp(Lag, bTurned, /*bWayBlocked*/ false, Config);
		Changes += bNext != bTurned ? 1 : 0;
		bTurned = bNext;
		Seen.Add(bTurned);
	}
	TestFalse(TEXT("1.2 m behind: still walking backwards"), Seen[1]);
	TestTrue(TEXT("1.51 m behind: he turns round"), Seen[2]);
	TestTrue(TEXT("on the way back he stays turned until 0.5 m (no flicker at 0.8 - 1.4 m)"), Seen[6] && Seen[7] && Seen[8] && Seen[9]);
	TestFalse(TEXT("at 0.5 m he walks backwards again"), Seen[10]);
	TestFalse(TEXT("1.1 - 1.49 m behind again: he keeps the rear (the band)"), Seen[13] || Seen[14]);
	TestEqual(TEXT("one turn out, one back"), Changes, 2);
	// A team-mate on his way delays the turn (no head-on overtaking at the catch-up speed); once turned he stays turned.
	TestTrue(TEXT("a mate 2 m ahead on his way: blocked"), FormationSectorRules::RearWayBlocked(FVector2D::ZeroVector, FVector2D(400.0, 0.0), { FVector2D(200.0, 0.0) }, Config));
	TestFalse(TEXT("1.15 m beside the way: clear"), FormationSectorRules::RearWayBlocked(FVector2D::ZeroVector, FVector2D(400.0, 0.0), { FVector2D(200.0, 115.0) }, Config));
	TestFalse(TEXT("behind him: clear"), FormationSectorRules::RearWayBlocked(FVector2D::ZeroVector, FVector2D(400.0, 0.0), { FVector2D(-60.0, 0.0) }, Config));
	TestFalse(TEXT("blocked: he does not start the turn"), FormationSectorRules::RearCatchUp(250.f, false, true, Config));
	TestTrue(TEXT("already turned: he keeps facing forwards"), FormationSectorRules::RearCatchUp(250.f, true, true, Config));
	return true;
}

#undef FORMATION_V26_TEST

#endif
