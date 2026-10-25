// CodexTactics.Formation.Terrain.* - the terrain classifier and the class -> formation mapping (Formation/FormationTerrainRules; design
// docs/design/FORMATION_SECTORS.md "Selection", mirrors Tools/TrainingGround/src/movement/rules.test.ts). UE-only (no Godot reference).
// The leader stands at the origin heading +X: left = -Y, right = +Y.

#include "Misc/AutomationTest.h"
#include "Formation/FormationTerrainRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define FORMATION_TERRAIN_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Formation.Terrain." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace FormationTerrainTestUtil
{
	FFormationWall MakeWall(double AX, double AY, double BX, double BY, float Thickness = 40.f)
	{
		FFormationWall Out;
		Out.A = FVector2D(AX, AY);
		Out.B = FVector2D(BX, BY);
		Out.Thickness = Thickness;
		return Out;
	}

	EFormationTerrainClass Raw(const TArray<FFormationWall>& Walls, const FVector2D& Pos = FVector2D::ZeroVector, float Yaw = 0.f)
	{
		return FormationTerrainRules::ClassifyRaw(Walls, Pos, Yaw, FFormationConfig());
	}
}

FORMATION_TERRAIN_TEST(FFormationTerrainClassesTest, "Classes")
bool FFormationTerrainClassesTest::RunTest(const FString&)
{
	using namespace FormationTerrainTestUtil;
	TestEqual(TEXT("No walls: open"), Raw({}), EFormationTerrainClass::Open);
	const FFormationWall Left = MakeWall(-2000.0, -300.0, 2000.0, -300.0);
	const FFormationWall Right = MakeWall(-2000.0, 300.0, 2000.0, 300.0);
	TestEqual(TEXT("A wall 3 m to the left: wall_left"), Raw({ Left }), EFormationTerrainClass::WallLeft);
	TestEqual(TEXT("A wall 3 m to the right: wall_right"), Raw({ Right }), EFormationTerrainClass::WallRight);
	TestEqual(TEXT("Both, 6 m apart (5.6 m free): corridor"), Raw({ Left, Right }), EFormationTerrainClass::Corridor);
	TestEqual(TEXT("A 7.6 m street fits the diamond: open"), Raw({ MakeWall(-2000.0, -380.0, 2000.0, -380.0), MakeWall(-2000.0, 380.0, 2000.0, 380.0) }),
		EFormationTerrainClass::Open);
	TestEqual(TEXT("A wall 1.2 m ahead: at_wall"), Raw({ MakeWall(120.0, -500.0, 120.0, 500.0, 20.f) }), EFormationTerrainClass::AtWall);
	TestEqual(TEXT("A wall 5 m ahead: not at_wall yet"), Raw({ MakeWall(500.0, -100.0, 500.0, 100.0, 20.f) }), EFormationTerrainClass::Open);
	TestEqual(TEXT("A wall 6 m to the left: out of the probe, open"), Raw({ MakeWall(-2000.0, -600.0, 2000.0, -600.0) }), EFormationTerrainClass::Open);
	// Rotated + shifted scene: the same left wall seen from a leader heading +Y (left = +X).
	TestEqual(TEXT("Heading +Y, wall at +X: wall_left"), Raw({ MakeWall(1300.0, -2000.0, 1300.0, 2000.0) }, FVector2D(1000.0, 0.0), 90.f),
		EFormationTerrainClass::WallLeft);
	// Lateral distance, not ray length: the side clearance of a wall 3 m off is 3 m minus half its thickness.
	const TOptional<float> Clearance = FormationTerrainRules::SideClearance({ Left }, FVector2D::ZeroVector, 0.f, -1, 400.f);
	TestTrue(TEXT("Left clearance 280 cm"), Clearance.IsSet() && FMath::IsNearlyEqual(Clearance.GetValue(), 280.f, 0.01f));
	TestFalse(TEXT("No right clearance"), FormationTerrainRules::SideClearance({ Left }, FVector2D::ZeroVector, 0.f, 1, 400.f).IsSet());
	return true;
}

FORMATION_TERRAIN_TEST(FFormationTerrainGapTest, "WallGapIsBridged")
bool FFormationTerrainGapTest::RunTest(const FString&)
{
	using namespace FormationTerrainTestUtil;
	// Wall - 2 m gap - wall on the left, the leader right at the gap: the slanted side rays still see the wall (no flicker to open).
	TestEqual(TEXT("2 m gap: still wall_left"), Raw({ MakeWall(-2000.0, -300.0, -100.0, -300.0), MakeWall(100.0, -300.0, 2000.0, -300.0) }),
		EFormationTerrainClass::WallLeft);
	return true;
}

FORMATION_TERRAIN_TEST(FFormationTerrainRayTest, "RayMeetsTheWallFace")
bool FFormationTerrainRayTest::RunTest(const FString&)
{
	using namespace FormationTerrainTestUtil;
	const TArray<FFormationWall> Walls = { MakeWall(500.0, -100.0, 500.0, 100.0, 40.f) };
	const TOptional<float> Hit = FormationTerrainRules::RayHit(Walls, FVector2D::ZeroVector, FVector2D(1.0, 0.0), 1000.f);
	TestTrue(TEXT("Hit at the near face (480 cm)"), Hit.IsSet() && FMath::IsNearlyEqual(Hit.GetValue(), 480.f, 0.01f));
	TestFalse(TEXT("Out of range"), FormationTerrainRules::RayHit(Walls, FVector2D::ZeroVector, FVector2D(1.0, 0.0), 400.f).IsSet());
	TestFalse(TEXT("A ray along a thin wall (parallel): no hit"), FormationTerrainRules::RayHit({ MakeWall(100.0, 0.0, 900.0, 0.0, 0.f) }, FVector2D::ZeroVector,
		FVector2D(1.0, 0.0), 1000.f).IsSet());
	const TOptional<float> Inside = FormationTerrainRules::RayHit(Walls, FVector2D(505.0, 0.0), FVector2D(1.0, 0.0), 1000.f);
	TestTrue(TEXT("Inside the wall body: 0"), Inside.IsSet() && Inside.GetValue() == 0.f);
	return true;
}

FORMATION_TERRAIN_TEST(FFormationTerrainHysteresisTest, "HysteresisHoldsTheClassForTheHoldTime")
bool FFormationTerrainHysteresisTest::RunTest(const FString&)
{
	using namespace FormationTerrainTestUtil;
	const FFormationConfig Config;
	const float Hold = Config.ClassifyHoldTime;
	const TArray<FFormationWall> None;
	TestEqual(TEXT("Within the hold time the previous class stays"), FormationTerrainRules::Classify(None, FVector2D::ZeroVector, 0.f,
		EFormationTerrainClass::WallLeft, 0.5f * Hold, Config), EFormationTerrainClass::WallLeft);
	TestEqual(TEXT("After it the raw class wins"), FormationTerrainRules::Classify(None, FVector2D::ZeroVector, 0.f,
		EFormationTerrainClass::WallLeft, Hold, Config), EFormationTerrainClass::Open);
	TestEqual(TEXT("No history: the raw class at once"), FormationTerrainRules::Classify(None, FVector2D::ZeroVector, 0.f,
		TOptional<EFormationTerrainClass>(), 0.f, Config), EFormationTerrainClass::Open);

	// Tracker over frames: walk along a left wall, then into the open. The class holds for the hold time after entering it.
	FFormationTerrainTracker Tracker;
	const TArray<FFormationWall> LeftWall = { MakeWall(-2000.0, -300.0, 2000.0, -300.0) };
	TestEqual(TEXT("Tracker starts with the raw class"), Tracker.Step(LeftWall, FVector2D::ZeroVector, 0.f, 0.1f, Config), EFormationTerrainClass::WallLeft);
	// Out of the wall's reach after 0.3 s: still wall_left until the hold time since entering it has passed.
	float Elapsed = 0.f;
	EFormationTerrainClass Seen = EFormationTerrainClass::WallLeft;
	float SwitchTime = -1.f;
	for (int32 Frame = 0; Frame < 40; ++Frame)
	{
		const float Dt = 0.05f;
		Elapsed += Dt;
		Seen = Tracker.Step(None, FVector2D::ZeroVector, 0.f, Dt, Config);
		if (Seen == EFormationTerrainClass::Open && SwitchTime < 0.f)
		{
			SwitchTime = Elapsed;
		}
	}
	TestTrue(FString::Printf(TEXT("Switched to open once the hold time had passed (at %.2f s, hold %.2f s)"), SwitchTime, Hold),
		SwitchTime >= Hold - 1e-3f && SwitchTime <= Hold + 0.051f);
	TestEqual(TEXT("Open in the end"), Seen, EFormationTerrainClass::Open);
	// Flicker: open just entered, a wall shows up for a moment - the class holds.
	FFormationTerrainTracker Fresh;
	Fresh.Step(None, FVector2D::ZeroVector, 0.f, 0.05f, Config);
	TestEqual(TEXT("A wall right after entering open: held open"), Fresh.Step(LeftWall, FVector2D::ZeroVector, 0.f, 0.05f, Config), EFormationTerrainClass::Open);
	TestTrue(TEXT("Its time runs on"), FMath::IsNearlyEqual(Fresh.TimeInClass, 0.05f));
	return true;
}

FORMATION_TERRAIN_TEST(FFormationTerrainMappingTest, "ClassToFormation")
bool FFormationTerrainMappingTest::RunTest(const FString&)
{
	TestEqual(TEXT("Open, 4: diamond"), FormationTerrainRules::FormationFor(EFormationTerrainClass::Open, 4), EFormationKind::Diamond);
	TestEqual(TEXT("Open, 3: wedge"), FormationTerrainRules::FormationFor(EFormationTerrainClass::Open, 3), EFormationKind::Wedge);
	TestEqual(TEXT("Open, 2: wedge (= the pair)"), FormationTerrainRules::FormationFor(EFormationTerrainClass::Open, 2), EFormationKind::Wedge);
	TestEqual(TEXT("Wall on the left: echelon AWAY from it (right)"), FormationTerrainRules::FormationFor(EFormationTerrainClass::WallLeft, 4),
		EFormationKind::EchelonRight);
	TestEqual(TEXT("Wall on the right: echelon left"), FormationTerrainRules::FormationFor(EFormationTerrainClass::WallRight, 3), EFormationKind::EchelonLeft);
	TestEqual(TEXT("Narrow: column"), FormationTerrainRules::FormationFor(EFormationTerrainClass::Corridor, 4), EFormationKind::Column);
	TestEqual(TEXT("Arrival at a wall: the wall layout"), FormationTerrainRules::FormationFor(EFormationTerrainClass::AtWall, 4), EFormationKind::Wall);
	return true;
}

#undef FORMATION_TERRAIN_TEST

#endif
