// CodexTactics.Formation.Runtime.* - the runtime glue of the formation look sectors (Formation/FormationRuntimeRules; stage 6 of
// docs/design/FORMATION_SECTORS.md, contract docs/design/FORMATION_CONTRACT.md 7, reference Tools/TrainingGround/src/movement/sim.ts).
// UE-only (no Godot reference).

#include "Misc/AutomationTest.h"
#include "Formation/FormationRuntimeRules.h"
#include "Formation/FormationShapeRules.h"
#include "Formation/FormationTerrainRules.h"
#include "Formation/FormationWallRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define FORMATION_RUNTIME_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Formation.Runtime." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace FormationRuntimeTestUtil
{
	FFormationWall MakeRuntimeTestWall(double AX, double AY, double BX, double BY, float Thickness = 40.f)
	{
		FFormationWall Wall;
		Wall.A = FVector2D(AX, AY);
		Wall.B = FVector2D(BX, BY);
		Wall.Thickness = Thickness;
		return Wall;
	}

	/**
	 * The runtime probe without a world: each probe ray "traced" against the true walls (FormationTerrainRules::RayHit), a hit turned
	 * into a thin wall by FormationRuntime::WallFromHit with the face normal towards the probe origin - what ProbeFormationWalls feeds
	 * the classifier.
	 */
	TArray<FFormationWall> ProbeWalls(const TArray<FFormationWall>& Truth, const FVector2D& Pos, float Heading, const FFormationConfig& Config)
	{
		TArray<FFormationWall> Out;
		for (const FormationRuntime::FProbeRay& Ray : FormationRuntime::ProbeRays(Heading, Config))
		{
			const FVector2D Dir = FormationMath::Forward(Ray.Yaw);
			double BestT = TNumericLimits<double>::Max();
			const FFormationWall* BestWall = nullptr;
			for (const FFormationWall& Wall : Truth)
			{
				const TOptional<float> Hit = FormationTerrainRules::RayHit({ Wall }, Pos, Dir, Ray.Length);
				if (Hit.IsSet() && Hit.GetValue() < BestT)
				{
					BestT = Hit.GetValue();
					BestWall = &Wall;
				}
			}
			if (BestWall)
			{
				const FVector2D Edge = (BestWall->B - BestWall->A).GetSafeNormal();
				FVector2D Normal(-Edge.Y, Edge.X);
				if (FVector2D::DotProduct(Normal, Dir) > 0.0)
				{
					Normal = -Normal; // the face looks back at the probe
				}
				Out.Add(FormationRuntime::WallFromHit(Pos + Dir * BestT, Normal));
			}
		}
		return Out;
	}
}

FORMATION_RUNTIME_TEST(FFormationRuntimeRolesTest, "RoleForFollowerSlot")
bool FFormationRuntimeRolesTest::RunTest(const FString&)
{
	using FormationRuntime::RoleForFollowerSlot;
	TestFalse(TEXT("1 member: no follower role"), RoleForFollowerSlot(0, 1).IsSet());
	TestTrue(TEXT("pair: slot 0 is the partner Z"), RoleForFollowerSlot(0, 2) == EFormationRole::Rear);
	TestFalse(TEXT("pair: no slot 1"), RoleForFollowerSlot(1, 2).IsSet());
	TestTrue(TEXT("wedge: slot 0 (left) is L"), RoleForFollowerSlot(0, 3) == EFormationRole::Left);
	TestTrue(TEXT("wedge: slot 1 (right) is P"), RoleForFollowerSlot(1, 3) == EFormationRole::Right);
	TestFalse(TEXT("wedge: no slot 2"), RoleForFollowerSlot(2, 3).IsSet());
	TestTrue(TEXT("diamond: slot 2 (old 2nd row left) is the rear man Z"), RoleForFollowerSlot(2, 4) == EFormationRole::Rear);
	TestFalse(TEXT("5 members: slot 3 keeps the old slot math"), RoleForFollowerSlot(3, 5).IsSet());
	TestFalse(TEXT("negative slot"), RoleForFollowerSlot(-1, 4).IsSet());
	return true;
}

FORMATION_RUNTIME_TEST(FFormationRuntimeModeTest, "ResolveMode")
bool FFormationRuntimeModeTest::RunTest(const FString&)
{
	using FormationRuntime::ResolveMode;
	TestTrue(TEXT("walking = patrol"), ResolveMode(true, false, false) == EFormationMode::Walk);
	TestTrue(TEXT("sprinting = run"), ResolveMode(true, true, false) == EFormationMode::Run);
	TestTrue(TEXT("standing = halt"), ResolveMode(false, false, false) == EFormationMode::Halt);
	TestTrue(TEXT("the wall layout wins (walking in too)"), ResolveMode(true, true, true) == EFormationMode::Wall);
	TestTrue(TEXT("standing at the wall"), ResolveMode(false, false, true) == EFormationMode::Wall);
	return true;
}

FORMATION_RUNTIME_TEST(FFormationRuntimeChooseTest, "WallLayoutOnlyOnArrival")
bool FFormationRuntimeChooseTest::RunTest(const FString&)
{
	using FormationRuntime::ChooseFormation;
	TestTrue(TEXT("a wall ahead while walking is a corner: the column he walked in stays"),
		ChooseFormation(EFormationTerrainClass::AtWall, 4, false, EFormationKind::Column) == EFormationKind::Column);
	TestTrue(TEXT("stopped at the wall: the wall layout"), ChooseFormation(EFormationTerrainClass::AtWall, 4, true, EFormationKind::Column) == EFormationKind::Wall);
	TestTrue(TEXT("walking off the wall layout while the class still says at_wall: the open formation"),
		ChooseFormation(EFormationTerrainClass::AtWall, 3, false, EFormationKind::Wall) == EFormationKind::Wedge);
	TestTrue(TEXT("open, 4: diamond"), ChooseFormation(EFormationTerrainClass::Open, 4, false, EFormationKind::Column) == EFormationKind::Diamond);
	TestTrue(TEXT("wall on the left: echelon right"), ChooseFormation(EFormationTerrainClass::WallLeft, 4, true, EFormationKind::Diamond) == EFormationKind::EchelonRight);
	TestTrue(TEXT("corridor: column"), ChooseFormation(EFormationTerrainClass::Corridor, 3, false, EFormationKind::Wedge) == EFormationKind::Column);
	TestTrue(TEXT("v1.2: open in a blizzard: the blizzard tight diamond"),
		ChooseFormation(EFormationTerrainClass::Open, 4, false, EFormationKind::Column, true) == EFormationKind::Blizzard);
	TestTrue(TEXT("v1.2: a corridor in a blizzard stays a column"),
		ChooseFormation(EFormationTerrainClass::Corridor, 4, false, EFormationKind::Diamond, true) == EFormationKind::Column);
	return true;
}

FORMATION_RUNTIME_TEST(FFormationRuntimeProbeRaysTest, "ProbeRaysMatchTheClassifier")
bool FFormationRuntimeProbeRaysTest::RunTest(const FString&)
{
	const FFormationConfig Config;
	const TArray<FormationRuntime::FProbeRay> Rays = FormationRuntime::ProbeRays(30.f, Config);
	TestEqual(TEXT("9 rays (3 ahead, 3 per side)"), Rays.Num(), 9);
	if (Rays.Num() != 9)
	{
		return false;
	}
	TestEqual(TEXT("ahead 0: along the heading"), Rays[0].Yaw, 30.f, 0.01f);
	TestEqual(TEXT("ahead 0 reaches the wall layout's wall (300 cm)"), Rays[0].Length, FMath::Max(Config.ClassifyAtWallDist, FormationRuntime::WallAheadCm), 0.01f);
	TestEqual(TEXT("ahead -20"), Rays[1].Yaw, 10.f, 0.01f);
	TestEqual(TEXT("ahead +20"), Rays[2].Yaw, 50.f, 0.01f);
	TestEqual(TEXT("ahead fan length = at_wall distance"), Rays[1].Length, Config.ClassifyAtWallDist, 0.01f);
	TestEqual(TEXT("right 90"), Rays[4].Yaw, 120.f, 0.01f);
	TestEqual(TEXT("right 90 length = probe_side"), Rays[4].Length, Config.ClassifyProbeSide, 0.01f);
	TestEqual(TEXT("right 60 reaches probe_side across the heading"), Rays[3].Length * FMath::Sin(FMath::DegreesToRadians(60.f)), Config.ClassifyProbeSide, 0.1f);
	TestEqual(TEXT("left 120"), Rays[8].Yaw, -90.f, 0.01f);
	return true;
}

FORMATION_RUNTIME_TEST(FFormationRuntimeProbeClassifyTest, "ProbeWallsClassifyLikeTheLayout")
bool FFormationRuntimeProbeClassifyTest::RunTest(const FString&)
{
	using namespace FormationRuntimeTestUtil;
	const FFormationConfig Config;
	// A lab-like set: a narrow corridor (y = +-200), a single wall on the left further on, a dead end ahead.
	const TArray<FFormationWall> Truth = {
		MakeRuntimeTestWall(0, -200, 2000, -200), MakeRuntimeTestWall(0, 200, 2000, 200),        // corridor 400 cm (minus 40 cm walls)
		MakeRuntimeTestWall(3000, -300, 6000, -300),                                   // wall on the left (negative y = left of +X)
		MakeRuntimeTestWall(8000, -1000, 8000, 1000),                                  // dead end across the heading
	};
	struct FCase
	{
		const TCHAR* Name;
		FVector2D Pos;
		float Heading;
	};
	const FCase Cases[] = {
		{ TEXT("corridor"), FVector2D(1000, 0), 0.f },
		{ TEXT("wall on the left"), FVector2D(4500, 0), 0.f },
		{ TEXT("open"), FVector2D(7000, 2500), 0.f },
		{ TEXT("at the dead end"), FVector2D(7900, 0), 0.f },
		{ TEXT("before the dead end (beside it the walls are across)"), FVector2D(7600, 0), 0.f },
		{ TEXT("corridor walked backwards"), FVector2D(1000, 0), 180.f },
		{ TEXT("slanted in the corridor"), FVector2D(1000, 50), 20.f },
	};
	for (const FCase& Case : Cases)
	{
		const EFormationTerrainClass Expected = FormationTerrainRules::ClassifyRaw(Truth, Case.Pos, Case.Heading, Config, TOptional<FVector2D>(Case.Pos));
		const EFormationTerrainClass Probed = FormationTerrainRules::ClassifyRaw(ProbeWalls(Truth, Case.Pos, Case.Heading, Config), Case.Pos, Case.Heading, Config,
			TOptional<FVector2D>(Case.Pos)); // v1.2: standing on his destination (at_wall only on arrival)
		TestEqual(FString::Printf(TEXT("%s: probe class %s = layout class %s"), Case.Name, FormationNames::ToString(Probed), FormationNames::ToString(Expected)),
			static_cast<int32>(Probed), static_cast<int32>(Expected));
		const TOptional<float> WidthTruth = FormationTerrainRules::PassageWidth(Truth, Case.Pos, Case.Heading, Config);
		const TOptional<float> WidthProbed = FormationTerrainRules::PassageWidth(ProbeWalls(Truth, Case.Pos, Case.Heading, Config), Case.Pos, Case.Heading, Config);
		TestEqual(FString::Printf(TEXT("%s: passage width set"), Case.Name), WidthProbed.IsSet(), WidthTruth.IsSet());
		if (WidthTruth.IsSet() && WidthProbed.IsSet())
		{
			TestEqual(FString::Printf(TEXT("%s: passage width"), Case.Name), WidthProbed.GetValue(), WidthTruth.GetValue(), 1.f);
		}
	}
	TestEqual(TEXT("the corridor reads as a corridor"), static_cast<int32>(FormationTerrainRules::ClassifyRaw(ProbeWalls(Truth, FVector2D(1000, 0), 0.f, Config),
		FVector2D(1000, 0), 0.f, Config)), static_cast<int32>(EFormationTerrainClass::Corridor));
	TestEqual(TEXT("the dead end reads as at_wall"), static_cast<int32>(FormationTerrainRules::ClassifyRaw(ProbeWalls(Truth, FVector2D(7900, 0), 0.f, Config),
		FVector2D(7900, 0), 0.f, Config, TOptional<FVector2D>(FVector2D(7900, 0)))), static_cast<int32>(EFormationTerrainClass::AtWall));
	TestEqual(TEXT("v1.2: walking past it (no destination there) it is no at_wall"), static_cast<int32>(FormationTerrainRules::ClassifyRaw(
		ProbeWalls(Truth, FVector2D(7900, 0), 0.f, Config), FVector2D(7900, 0), 0.f, Config, TOptional<FVector2D>(FVector2D(7900, 3000)))) != static_cast<int32>(EFormationTerrainClass::AtWall), true);
	return true;
}

FORMATION_RUNTIME_TEST(FFormationRuntimeWallFromHitTest, "WallFromHit")
bool FFormationRuntimeWallFromHitTest::RunTest(const FString&)
{
	const FFormationWall Wall = FormationRuntime::WallFromHit(FVector2D(300, 0), FVector2D(-1, 0), 10.f);
	TestEqual(TEXT("thin"), Wall.Thickness, 0.f);
	TestEqual(TEXT("10 cm each side"), FVector2D::Distance(Wall.A, Wall.B), 20.0, 0.01);
	const TOptional<float> Hit = FormationTerrainRules::RayHit({ Wall }, FVector2D::ZeroVector, FVector2D(1, 0), 500.f);
	TestTrue(TEXT("the ray that made it hits it"), Hit.IsSet());
	TestEqual(TEXT("at the hit point"), Hit.Get(0.f), 300.f, 0.01f);
	TestTrue(TEXT("along the surface: across the heading of the ray"), FormationTerrainRules::IsAcrossHeading(Wall, 0.f, 30.f));
	return true;
}

FORMATION_RUNTIME_TEST(FFormationRuntimeWallAlongTest, "WallAlongFeedsTheLayout")
bool FFormationRuntimeWallAlongTest::RunTest(const FString&)
{
	// The face line x = 500 seen from the origin: outward normal -X (yaw 180); facing the wall (+X) the right hand is +Y.
	const FFormationConfig Config;
	const FFormationWall Wall = FormationRuntime::WallAlong(FVector2D(500, 0), 180.f, 150.f, 300.f);
	float Left = 0.f;
	float Right = 0.f;
	FormationWallRules::WallRoom(Wall, FVector2D(400, 0), Left, Right);
	TestEqual(TEXT("room left back"), Left, 150.f, 0.5f);
	TestEqual(TEXT("room right back"), Right, 300.f, 0.5f);
	TestEqual(TEXT("outward yaw from the clicked slot"), FormationWallRules::OutwardYaw(Wall, FVector2D(400, 0)), 180.f, 0.01f);
	const TArray<FFormationSlot> Slots = FormationWallRules::Layout(Wall, FVector2D(400, 0), 4, Left, Right, Config);
	TestEqual(TEXT("4 slots"), Slots.Num(), 4);
	if (Slots.Num() == 4)
	{
		TestEqual(TEXT("K wall.face_offset off the face"), Slots[0].Position.X, 500.0 - Config.WallFaceOffset, 0.5);
		const FFormationSlot* Z = Slots.FindByPredicate([](const FFormationSlot& S) { return S.Role == EFormationRole::Rear; });
		TestTrue(TEXT("the 4th at the roomier (right = +Y) end"), Z && Z->Position.Y > 150.0);
		for (const FFormationSlot& Slot : Slots)
		{
			TestTrue(FString::Printf(TEXT("%s does not look into the wall"), FormationNames::ToString(Slot.Role)), FormationMath::AngleBetween(Slot.Yaw, 180.f) < 90.f);
		}
	}
	return true;
}

FORMATION_RUNTIME_TEST(FFormationRuntimeRoomTest, "MeasureRoom")
bool FFormationRuntimeRoomTest::RunTest(const FString&)
{
	using FormationRuntime::MeasureRoom;
	TestEqual(TEXT("no samples"), MeasureRoom({}, 50.f), 0.f);
	TestEqual(TEXT("all wall"), MeasureRoom({ true, true, true, true }, 50.f), 200.f);
	TestEqual(TEXT("ends at the first gap"), MeasureRoom({ true, true, false, true }, 50.f), 100.f);
	TestEqual(TEXT("gap right away"), MeasureRoom({ false, true }, 50.f), 0.f);
	return true;
}

FORMATION_RUNTIME_TEST(FFormationRuntimeFollowerMoveTest, "FollowerSpeedLeadsWithTheSlot")
bool FFormationRuntimeFollowerMoveTest::RunTest(const FString&)
{
	using FormationRuntime::FollowerMove;
	const FFormationConfig Config;
	const float Walk = 220.f;
	const float Top = Walk * Config.MoveCatchupMult;
	// At a still slot: he stands.
	TestTrue(TEXT("at a still slot: stop"), FollowerMove(FVector2D(10, 0), 0.f, FVector2D::ZeroVector, FVector2D::ZeroVector, 1.f, Walk, Walk, false, Config).bStop);
	// On his slot while it walks (patrol 176 cm/s, facing the way): he moves with it - no trailing (contract 7).
	const FVector2D Patrol(176.0, 0.0);
	const FormationRuntime::FFollowerMove OnSlot = FollowerMove(FVector2D::ZeroVector, 0.f, FVector2D::ZeroVector, Patrol, 1.f, Walk, Walk, false, Config);
	TestFalse(TEXT("on a moving slot: no stop"), OnSlot.bStop);
	TestEqual(TEXT("on a moving slot: the slot's speed"), OnSlot.Speed, 176.f, 0.5f);
	// 50 cm behind it: the slot's speed + 50 x FOLLOW_GAIN, capped by top x catchup.
	const FormationRuntime::FFollowerMove Behind = FollowerMove(FVector2D(-50, 0), 0.f, FVector2D::ZeroVector, Patrol, 1.f, Walk, Walk, false, Config);
	TestEqual(TEXT("50 cm behind: + 100 cm/s, capped at top x catchup"), Behind.Speed, FMath::Min(276.f, Top), 0.5f);
	// Far away from a still slot: the catch-up speed.
	const FormationRuntime::FFollowerMove Far = FollowerMove(FVector2D(-2000, 0), 0.f, FVector2D::ZeroVector, FVector2D::ZeroVector, 1.f, Walk, Walk, false, Config);
	TestEqual(TEXT("far: top x catchup"), Far.Speed, Top, 0.5f);
	// The rear man looks at 6 while the squad walks to 12: backwards - capped at walk x backward_mult.
	const FormationRuntime::FFollowerMove Backwards = FollowerMove(FVector2D(-100, 0), 180.f, FVector2D::ZeroVector, Patrol, 1.f, Walk, Walk, false, Config);
	TestTrue(TEXT("walking backwards"), Backwards.bBackward);
	TestEqual(TEXT("backwards: walk x backward_mult"), Backwards.Speed, Walk * Config.MoveBackwardMult, 0.5f);
	// Running: no backward cap.
	const FormationRuntime::FFollowerMove Running = FollowerMove(FVector2D(-100, 0), 180.f, FVector2D::ZeroVector, FVector2D(480.0, 0.0), 1.f, 480.f, Walk, true, Config);
	TestTrue(TEXT("running backwards keeps the full speed"), Running.Speed > Walk * Config.MoveBackwardMult + 1.f);
	// The jitter scales the feed-forward.
	const FormationRuntime::FFollowerMove Jittered = FollowerMove(FVector2D::ZeroVector, 0.f, FVector2D::ZeroVector, Patrol, 1.1f, Walk, Walk, false, Config);
	TestEqual(TEXT("jitter x feed-forward"), Jittered.Speed, 176.f * 1.1f, 0.5f);
	// A slot faster than the top speed is capped.
	const FormationRuntime::FFollowerMove Fast = FollowerMove(FVector2D::ZeroVector, 0.f, FVector2D::ZeroVector, FVector2D(1000.0, 0.0), 1.f, Walk, Walk, false, Config);
	TestEqual(TEXT("capped at top x catchup"), Fast.Speed, Top, 0.5f);
	return true;
}

FORMATION_RUNTIME_TEST(FFormationRuntimeLeadTest, "LeadDestinationAndWallTurn")
bool FFormationRuntimeLeadTest::RunTest(const FString&)
{
	using namespace FormationRuntime;
	TestTrue(TEXT("still slot: the slot"), LeadDestination(FVector2D(100, 100), FVector2D::ZeroVector).Equals(FVector2D(100, 100)));
	TestTrue(TEXT("moving slot: ahead by velocity x lead"), LeadDestination(FVector2D(100, 100), FVector2D(200, 0), 0.5f).Equals(FVector2D(200, 100)));
	TestFalse(TEXT("walking in, 3 m off his wall slot: his path"), UseWallLayoutYaw(300.f, true));
	TestTrue(TEXT("walking in, within 1 m: the layout yaw"), UseWallLayoutYaw(90.f, true));
	TestTrue(TEXT("standing: the layout yaw"), UseWallLayoutYaw(300.f, false));
	TestFalse(TEXT("heading: too slow"), HeadingFromVelocity(FVector2D(10, 0)).IsSet());
	TestEqual(TEXT("heading: the travel yaw"), HeadingFromVelocity(FVector2D(0, 200)).Get(0.f), 90.f, 0.01f);
	return true;
}

FORMATION_RUNTIME_TEST(FFormationRuntimeAssignTest, "AssignByTravel")
bool FFormationRuntimeAssignTest::RunTest(const FString&)
{
	using FormationRuntime::AssignByTravel;
	const TArray<FVector2D> Places = { FVector2D(0, 100), FVector2D(0, -100), FVector2D(0, 200) };
	const TArray<int32> Swapped = AssignByTravel({ FVector2D(0, -90), FVector2D(0, 110) }, Places);
	TestEqual(TEXT("two members"), Swapped.Num(), 2);
	if (Swapped.Num() == 2)
	{
		TestEqual(TEXT("the one on the -Y side takes -Y"), Swapped[0], 1);
		TestEqual(TEXT("the one on the +Y side takes +Y"), Swapped[1], 0);
	}
	const TArray<int32> Tie = AssignByTravel({ FVector2D::ZeroVector, FVector2D::ZeroVector }, { FVector2D(0, 100), FVector2D(0, -100) });
	TestTrue(TEXT("a tie keeps the member order"), Tie.Num() == 2 && Tie[0] == 0 && Tie[1] == 1);
	TestEqual(TEXT("more members than places: none"), AssignByTravel({ FVector2D::ZeroVector, FVector2D::ZeroVector }, { FVector2D::ZeroVector }).Num(), 0);
	const TArray<int32> Three = AssignByTravel({ FVector2D(0, 210), FVector2D(0, -95), FVector2D(0, 95) }, Places);
	TestTrue(TEXT("three: each to the nearest"), Three.Num() == 3 && Three[0] == 2 && Three[1] == 1 && Three[2] == 0);
	return true;
}

FORMATION_RUNTIME_TEST(FFormationRuntimeAcceptProbeTest, "ProbeDebounce")
bool FFormationRuntimeAcceptProbeTest::RunTest(const FString&)
{
	using FormationRuntime::AcceptProbe;
	using EC = EFormationTerrainClass;
	const TOptional<EC> None;
	TestTrue(TEXT("nothing in force: the first probe is taken"), AcceptProbe(EC::Corridor, None, None));
	TestTrue(TEXT("agrees with the class in force"), AcceptProbe(EC::Corridor, TOptional<EC>(EC::Open), TOptional<EC>(EC::Corridor)));
	TestFalse(TEXT("one probe alone does not change the class (a grazed wall end)"), AcceptProbe(EC::WallLeft, TOptional<EC>(EC::Open), TOptional<EC>(EC::Open)));
	TestTrue(TEXT("two probes in a row do"), AcceptProbe(EC::WallLeft, TOptional<EC>(EC::WallLeft), TOptional<EC>(EC::Open)));
	TestFalse(TEXT("no previous probe: still one alone"), AcceptProbe(EC::Corridor, None, TOptional<EC>(EC::Open)));
	return true;
}

#undef FORMATION_RUNTIME_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
