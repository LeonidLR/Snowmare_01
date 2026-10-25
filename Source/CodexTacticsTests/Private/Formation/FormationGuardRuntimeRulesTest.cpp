// CodexTactics.Formation.GuardRuntime.* - the rules v2.1 runtime glue (Formation/FormationGuardRuntimeRules): the leader's trail and his pose
// ahead on his path, the world's box collision as walls, the bypass leg / waypoints, the HUD line (labels.ts), who is exempt from ClearLook,
// the bot's check 7 (squad split) and check 9 geometry the smokes share. User decisions 2026-10-25 (docs/design/FORMATION_SECTORS.md v2.1,
// contract docs/design/FORMATION_CONTRACT.md 10; reference Tools/TrainingGround/src/movement/sim.ts / labels.ts / checks.ts).
// UE-only (no Godot reference). UE axes: X forward, +Y right.

#include "Misc/AutomationTest.h"
#include "Formation/FormationBypassRules.h"
#include "Formation/FormationGuardRules.h"
#include "Formation/FormationGuardRuntimeRules.h"
#include "Formation/FormationShapeRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define FORMATION_GUARD_RUNTIME_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Formation.GuardRuntime." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace FormationGuardRuntimeTestUtil
{
	FFormationWall GuardRuntimeWall(double AX, double AY, double BX, double BY, float Thickness = 40.f)
	{
		FFormationWall Out;
		Out.A = FVector2D(AX, AY);
		Out.B = FVector2D(BX, BY);
		Out.Thickness = Thickness;
		return Out;
	}

	/** A lab wall as the map builder places it (an engine cube along a -> b, length x thickness) read back through WallsFromBox. */
	FFormationWall LabWallFromBox(double AX, double AY, double BX, double BY, double Thickness)
	{
		const FVector2D A(AX, AY);
		const FVector2D B(BX, BY);
		const FVector2D Along = (B - A).GetSafeNormal();
		const FVector2D Across(-Along.Y, Along.X);
		TArray<FFormationWall> Out;
		FormationGuardRuntime::WallsFromBox((A + B) * 0.5, Along * (0.5 * FVector2D::Distance(A, B)), Across * (0.5 * Thickness), Out);
		return Out.Num() == 1 ? Out[0] : FFormationWall();
	}

	bool NearlyPoint(const FVector2D& A, const FVector2D& B, double Tolerance = 0.5)
	{
		return FVector2D::Distance(A, B) <= Tolerance;
	}
}

FORMATION_GUARD_RUNTIME_TEST(FFormationGuardRuntimeTrailTest, "Trail")
bool FFormationGuardRuntimeTrailTest::RunTest(const FString&)
{
	TArray<FVector2D> Trail;
	FormationGuardRuntime::PushTrail(Trail, FVector2D(0.0, 0.0));
	FormationGuardRuntime::PushTrail(Trail, FVector2D(10.0, 0.0));
	TestEqual(TEXT("a point closer than the step is not kept"), Trail.Num(), 1);
	FormationGuardRuntime::PushTrail(Trail, FVector2D(25.0, 0.0));
	TestEqual(TEXT("a point a step away is kept"), Trail.Num(), 2);
	for (int32 Step = 2; Step <= 100; ++Step)
	{
		FormationGuardRuntime::PushTrail(Trail, FVector2D(25.0 * Step, 0.0));
	}
	// 25 m walked: only 15 m kept (the point where the kept length reaches 15 m stays).
	const double Kept = Trail.Last().X - Trail[0].X;
	TestTrue(FString::Printf(TEXT("15 m of path kept (%.0f cm)"), Kept), Kept >= 1500.0 && Kept < 1525.0);
	TestTrue(TEXT("the newest point is the leader's"), FormationGuardRuntime::TrailWithLeader(Trail, FVector2D(2500.0, 0.0)).Num() == Trail.Num());
	TestTrue(TEXT("the leader is appended past the last point"), FormationGuardRuntime::TrailWithLeader(Trail, FVector2D(2510.0, 0.0)).Last() == FVector2D(2510.0, 0.0));
	return true;
}

FORMATION_GUARD_RUNTIME_TEST(FFormationGuardRuntimeAheadTest, "AheadPose")
bool FFormationGuardRuntimeAheadTest::RunTest(const FString&)
{
	using FormationGuardRuntimeTestUtil::NearlyPoint;
	// Round a corner: 300 cm east, then north.
	const TArray<FVector2D> Path = { FVector2D(0.0, 0.0), FVector2D(300.0, 0.0), FVector2D(300.0, 1000.0) };
	const FFormationPose Pose = FormationGuardRuntime::AheadPose(Path, 0.f, 500.f);
	TestTrue(TEXT("500 cm on: 200 cm past the corner"), NearlyPoint(Pose.Pos, FVector2D(300.0, 200.0)));
	TestEqual(TEXT("its yaw is the second piece's (north = +90)"), Pose.Yaw, 90.f, 0.01f);
	const FFormationPose Before = FormationGuardRuntime::AheadPose(Path, 0.f, 100.f);
	TestTrue(TEXT("100 cm on: still on the first piece"), NearlyPoint(Before.Pos, FVector2D(100.0, 0.0)) && FMath::IsNearlyZero(Before.Yaw, 0.01f));
	const FFormationPose Past = FormationGuardRuntime::AheadPose(Path, 0.f, 5000.f);
	TestTrue(TEXT("a short path ends at its last point"), NearlyPoint(Past.Pos, FVector2D(300.0, 1000.0)) && FMath::IsNearlyEqual(Past.Yaw, 90.f, 0.01f));
	const FFormationPose Alone = FormationGuardRuntime::AheadPose({ FVector2D(5.0, 5.0) }, 33.f, 500.f);
	TestTrue(TEXT("one point: there, with the heading"), NearlyPoint(Alone.Pos, FVector2D(5.0, 5.0)) && FMath::IsNearlyEqual(Alone.Yaw, 33.f));
	// The turn this path makes within turn.lookahead folds a wide formation (FormationGuardRules::TurnAhead > turn.fold_deg).
	TestTrue(TEXT("the 90 deg corner is a sharp turn"), FormationGuardRules::TurnAhead(Path, 0.f) > FFormationConfig().TurnFoldDeg);
	return true;
}

FORMATION_GUARD_RUNTIME_TEST(FFormationGuardRuntimePathAheadTest, "PathAheadFrom")
bool FFormationGuardRuntimePathAheadTest::RunTest(const FString&)
{
	const TArray<FVector2D> Points = { FVector2D(0.0, 0.0), FVector2D(1000.0, 0.0), FVector2D(1000.0, 800.0), FVector2D(2000.0, 800.0) };
	const TArray<FVector2D> OnFirst = FormationGuardRuntime::PathAheadFrom(Points, FVector2D(400.0, 10.0));
	TestEqual(TEXT("on the first piece: him + 3 points ahead"), OnFirst.Num(), 4);
	TestTrue(TEXT("the next point is the first corner"), OnFirst.Num() > 1 && OnFirst[1] == FVector2D(1000.0, 0.0));
	const TArray<FVector2D> OnSecond = FormationGuardRuntime::PathAheadFrom(Points, FVector2D(990.0, 500.0));
	TestTrue(TEXT("on the second piece: the passed corner is not ahead"), OnSecond.Num() == 3 && OnSecond[1] == FVector2D(1000.0, 800.0));
	TestEqual(TEXT("no path: just him"), FormationGuardRuntime::PathAheadFrom({ FVector2D(1.0, 1.0) }, FVector2D(5.0, 5.0)).Num(), 1);
	return true;
}

FORMATION_GUARD_RUNTIME_TEST(FFormationGuardRuntimeBoxWallsTest, "WallsFromBox")
bool FFormationGuardRuntimeBoxWallsTest::RunTest(const FString&)
{
	using namespace FormationGuardRuntimeTestUtil;
	// The lab's W_G_19 (an oblique 40 cm wall): the box reads back as the layout wall exactly.
	const FFormationWall G19 = LabWallFromBox(1700.0, 2800.0, 3100.0, 3100.0, 40.0);
	const bool bForward = NearlyPoint(G19.A, FVector2D(1700.0, 2800.0)) && NearlyPoint(G19.B, FVector2D(3100.0, 3100.0));
	const bool bReverse = NearlyPoint(G19.B, FVector2D(1700.0, 2800.0)) && NearlyPoint(G19.A, FVector2D(3100.0, 3100.0));
	TestTrue(TEXT("a 40 cm lab wall = its layout segment (either way round)"), bForward || bReverse);
	TestEqual(TEXT("... and thickness"), G19.Thickness, 40.f, 0.01f);
	// A thick box: the round caps reach at most LayoutCapCm past the box ends.
	TArray<FFormationWall> Thick;
	FormationGuardRuntime::WallsFromBox(FVector2D::ZeroVector, FVector2D(300.0, 0.0), FVector2D(0.0, 100.0), Thick);
	TestTrue(TEXT("a 2 m thick box is one wall"), Thick.Num() == 1);
	if (Thick.Num() == 1)
	{
		const double Reach = FMath::Max(FMath::Abs(Thick[0].A.X), FMath::Abs(Thick[0].B.X)) + 0.5 * Thick[0].Thickness;
		TestEqual(TEXT("its body ends LayoutCapCm past the box"), Reach, 300.0 + FormationGuardRuntime::LayoutCapCm, 0.5);
	}
	// A bulky box (a building): its four sides, thin, corner to corner.
	TArray<FFormationWall> Bulky;
	FormationGuardRuntime::WallsFromBox(FVector2D(1000.0, 1000.0), FVector2D(500.0, 0.0), FVector2D(0.0, 400.0), Bulky);
	TestEqual(TEXT("a bulky box = four sides"), Bulky.Num(), 4);
	TestTrue(TEXT("... thin, and every corner is a joint (no free end)"), Bulky.Num() == 4 && Bulky[0].Thickness == 0.f
		&& !FormationGuardRuntime::IsFreeStanding(Bulky, 0) && !FormationGuardRuntime::IsFreeStanding(Bulky, 3));
	TArray<FFormationWall> None;
	FormationGuardRuntime::WallsFromBox(FVector2D::ZeroVector, FVector2D(0.2, 0.0), FVector2D(0.0, 100.0), None);
	TestEqual(TEXT("a degenerate box adds nothing"), None.Num(), 0);
	return true;
}

FORMATION_GUARD_RUNTIME_TEST(FFormationGuardRuntimeBypassLegTest, "BypassLeg")
bool FFormationGuardRuntimeBypassLegTest::RunTest(const FString&)
{
	using namespace FormationGuardRuntimeTestUtil;
	// Lab lane G: W_G_19 free-standing, the L-shaped W_E_13 / W_E_14 joined at (300, 2500).
	const TArray<FFormationWall> Walls = { GuardRuntimeWall(1700.0, 2800.0, 3100.0, 3100.0), GuardRuntimeWall(-4000.0, 2500.0, 300.0, 2500.0),
		GuardRuntimeWall(300.0, 2500.0, 300.0, 5300.0), GuardRuntimeWall(4800.0, 2700.0, 4800.0, 4000.0) };
	TestTrue(TEXT("W_G_19 stands free"), FormationGuardRuntime::IsFreeStanding(Walls, 0));
	TestFalse(TEXT("W_E_13 is joined to W_E_14"), FormationGuardRuntime::IsFreeStanding(Walls, 1));
	const TOptional<int32> Blocking = FormationGuardRuntime::FirstBlockingWall(Walls, FVector2D(1100.0, 2900.0), FVector2D(4300.0, 2900.0),
		FormationGuardRuntime::AgentRadiusCm, true);
	TestTrue(TEXT("R_G_free's leg is blocked by W_G_19 first"), Blocking.IsSet() && Blocking.GetValue() == 0);
	const TOptional<int32> Far = FormationGuardRuntime::FirstBlockingWall(Walls, FVector2D(1100.0, 2900.0), FVector2D(5900.0, 2900.0),
		FormationGuardRuntime::AgentRadiusCm, true);
	TestTrue(TEXT("a longer leg: still the nearer wall first"), Far.IsSet() && Far.GetValue() == 0);
	TestFalse(TEXT("the joined L wall never plans a bypass"), FormationGuardRuntime::FirstBlockingWall(Walls, FVector2D(0.0, 2000.0), FVector2D(0.0, 3000.0),
		FormationGuardRuntime::AgentRadiusCm, true).IsSet());
	TestFalse(TEXT("an open leg: none"), FormationGuardRuntime::FirstBlockingWall(Walls, FVector2D(1100.0, 2000.0), FVector2D(4300.0, 2000.0),
		FormationGuardRuntime::AgentRadiusCm, true).IsSet());

	// The waypoints are the chosen path's corners; they advance once the leader is near.
	const FFormationBypassChoice Choice = FormationBypassRules::ChooseSide(Walls[0], FVector2D(1100.0, 2900.0), FVector2D(4300.0, 2900.0), {});
	const TArray<FVector2D> Waypoints = FormationGuardRuntime::BypassWaypoints(Choice);
	TestEqual(TEXT("the waypoints = the path without its ends"), Waypoints.Num(), Choice.Path.Num() - 2);
	if (Waypoints.Num() > 0)
	{
		TestEqual(TEXT("far from the first corner: still it"), FormationGuardRuntime::AdvanceWaypoint(Waypoints, 0, FVector2D(1100.0, 2900.0)), 0);
		TestTrue(TEXT("at the first corner: the next"), FormationGuardRuntime::AdvanceWaypoint(Waypoints, 0, Waypoints[0] + FVector2D(50.0, 0.0)) >= 1);
	}
	TestEqual(TEXT("past every corner = done"), FormationGuardRuntime::AdvanceWaypoint(Waypoints, Waypoints.Num(), FVector2D::ZeroVector), Waypoints.Num());
	return true;
}

FORMATION_GUARD_RUNTIME_TEST(FFormationGuardRuntimeHudTest, "HudText")
bool FFormationGuardRuntimeHudTest::RunTest(const FString&)
{
	using EK = EFormationKind;
	using EL = EFormationGuardLevel;
	// labels.ts guardHudText.
	TestEqual(TEXT("the trail column"), FormationGuardRuntime::GuardHudText(EK::EchelonLeft, EK::Column, EL::Trail, 4), FString(TEXT("Echelon left -> Column (wall)")));
	TestEqual(TEXT("the mirror"), FormationGuardRuntime::GuardHudText(EK::EchelonLeft, EK::EchelonRight, EL::Mirror, 4),
		FString(TEXT("Echelon left -> Echelon right (wall)")));
	TestEqual(TEXT("level 0: the name"), FormationGuardRuntime::GuardHudText(EK::EchelonLeft, EK::EchelonLeft, EL::Requested, 4), FString(TEXT("Echelon left")));
	TestEqual(TEXT("a cut column stays a column: the name"), FormationGuardRuntime::GuardHudText(EK::Column, EK::Column, EL::Trail, 4), FString(TEXT("Column")));
	TestEqual(TEXT("the 2-man diamond is the pair"), FormationGuardRuntime::GuardHudText(EK::Diamond, EK::Column, EL::Trail, 2), FString(TEXT("Pair -> Column (wall)")));
	TestEqual(TEXT("blizzard"), FormationGuardRuntime::FormationLabel(EK::Blizzard, 4), FString(TEXT("Blizzard diamond")));
	TestEqual(TEXT("wedge"), FormationGuardRuntime::FormationLabel(EK::Wedge, 3), FString(TEXT("Wedge")));
	TestEqual(TEXT("bypass reason"), FString(FormationGuardRuntime::BypassReasonText(EFormationBypassReason::TooLong)), FString(TEXT("too_long")));
	return true;
}

FORMATION_GUARD_RUNTIME_TEST(FFormationGuardRuntimeClearLookTest, "ShouldClearLook")
bool FFormationGuardRuntimeClearLookTest::RunTest(const FString&)
{
	using EK = EFormationKind;
	using EM = EFormationMode;
	const TOptional<float> NoTravel;
	TestTrue(TEXT("a follower on patrol"), FormationGuardRuntime::ShouldClearLook(EK::EchelonLeft, EM::Walk, false, false, NoTravel, 90.f));
	TestTrue(TEXT("halted, manual line"), FormationGuardRuntime::ShouldClearLook(EK::Line, EM::Halt, false, false, NoTravel, 90.f));
	TestTrue(TEXT("all forward"), FormationGuardRuntime::ShouldClearLook(EK::Diamond, EM::Forward, false, false, NoTravel, 0.f));
	TestFalse(TEXT("not the wall layout"), FormationGuardRuntime::ShouldClearLook(EK::Wall, EM::Wall, false, false, NoTravel, 0.f));
	TestFalse(TEXT("not running"), FormationGuardRuntime::ShouldClearLook(EK::Diamond, EM::Run, false, false, NoTravel, 0.f));
	TestFalse(TEXT("not the corner watcher"), FormationGuardRuntime::ShouldClearLook(EK::Diamond, EM::Walk, true, false, NoTravel, 0.f));
	TestFalse(TEXT("not the leader looking where he walks"), FormationGuardRuntime::ShouldClearLook(EK::Diamond, EM::Walk, false, true, TOptional<float>(10.f), 40.f));
	TestTrue(TEXT("the leader looking aside"), FormationGuardRuntime::ShouldClearLook(EK::Diamond, EM::Walk, false, true, TOptional<float>(10.f), 60.f));
	TestTrue(TEXT("the leader standing"), FormationGuardRuntime::ShouldClearLook(EK::Diamond, EM::Halt, false, true, NoTravel, 0.f));
	return true;
}

FORMATION_GUARD_RUNTIME_TEST(FFormationGuardRuntimeSplitTest, "SplitCuts")
bool FFormationGuardRuntimeSplitTest::RunTest(const FString&)
{
	using namespace FormationGuardRuntimeTestUtil;
	using ER = EFormationRole;
	// The user's case (lab lane B): W_B_2 3 m to the leader's left, heading +X, manual echelon left (its men to the left).
	const TArray<FFormationWall> Walls = { GuardRuntimeWall(-4000.0, -3300.0, -1500.0, -3300.0) };
	const FVector2D Leader(-2500.0, -3000.0);
	const auto Members = [&Leader](EFormationKind Formation)
	{
		TArray<FormationGuardRuntime::FSplitMember> Out;
		for (const FFormationSlot& Slot : FormationShapeRules::Slots(Formation, 4, Leader, 0.f))
		{
			FormationGuardRuntime::FSplitMember Member;
			Member.Place = Slot.Role;
			Member.Pos = Slot.Position;
			Member.Slot = Slot.Position;
			Out.Add(Member);
		}
		return Out;
	};
	const TArray<FString> Cut = FormationGuardRuntime::SplitCuts(Walls, EFormationKind::EchelonLeft, false, 4, Members(EFormationKind::EchelonLeft));
	TestTrue(FString::Printf(TEXT("echelon left through the wall is cut (%s)"), *FString::Join(Cut, TEXT(", "))), Cut.Num() > 0);
	TestEqual(TEXT("the mirror echelon right is whole"), FormationGuardRuntime::SplitCuts(Walls, EFormationKind::EchelonRight, false, 4,
		Members(EFormationKind::EchelonRight)).Num(), 0);
	// The guard picks that mirror here (FormationGuardRules::Resolve level 1).
	const FFormationGuardState Guard = FormationGuardRules::Resolve(Walls, EFormationKind::EchelonLeft, 4, Leader, 0.f, TOptional<FFormationPose>(), 0.f,
		FFormationGuardState(), 0.f);
	TestTrue(TEXT("the guard takes the mirror"), Guard.Level == EFormationGuardLevel::Mirror && Guard.Effective == EFormationKind::EchelonRight);

	// The trail column round a wall end (a bent trail) is whole by its chain.
	const TArray<FFormationWall> Corner = { GuardRuntimeWall(0.0, 0.0, 0.0, 1000.0) };
	const TArray<FVector2D> Trail = { FVector2D(-300.0, 1300.0), FVector2D(-100.0, 1300.0), FVector2D(100.0, 1300.0), FVector2D(300.0, 1250.0),
		FVector2D(300.0, 900.0) };
	TArray<FormationGuardRuntime::FSplitMember> Column;
	for (const FFormationSlot& Slot : FormationShapeRules::TrailSlots(Trail, -90.f, 4))
	{
		FormationGuardRuntime::FSplitMember Member;
		Member.Place = Slot.Role;
		Member.Pos = Slot.Position;
		Member.Slot = Slot.Position;
		Column.Add(Member);
	}
	TestEqual(TEXT("the trail column wraps the wall end whole"), FormationGuardRuntime::SplitCuts(Corner, EFormationKind::Column, true, 4, Column).Num(), 0);
	return true;
}

FORMATION_GUARD_RUNTIME_TEST(FFormationGuardRuntimeCornerPointsTest, "CornerCheckPoints")
bool FFormationGuardRuntimeCornerPointsTest::RunTest(const FString&)
{
	using namespace FormationGuardRuntimeTestUtil;
	// W_E_14's free north end (300, 5300); the leader walks north at x = 0 (the wall on his right... seen from the west side).
	const FFormationWall Wall = GuardRuntimeWall(300.0, 2500.0, 300.0, 5300.0);
	FVector2D Graze;
	FVector2D Hidden;
	FormationGuardRuntime::CornerCheckPoints(Wall, 1, FVector2D(0.0, 5000.0), Graze, Hidden);
	TestTrue(TEXT("G = 20 + 50 cm past the end"), NearlyPoint(Graze, FVector2D(300.0, 5370.0)));
	TestTrue(TEXT("H = 200 cm behind the far face (east)"), NearlyPoint(Hidden, FVector2D(500.0, 5370.0)));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
