// CodexTactics.Formation.Wall.* - the wall layout (Formation/FormationWallRules; design docs/design/FORMATION_SECTORS.md "Wall", mirrors
// Tools/TrainingGround/src/movement/rules.test.ts). UE-only (no Godot reference).
// The wall runs along X at y = 200 (40 cm thick), the squad on the y < 200 side: outward normal -Y (yaw -90); facing the wall (+Y)
// the right hand is -X.

#include "Misc/AutomationTest.h"
#include "Formation/FormationWallRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define FORMATION_WALL_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Formation.Wall." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace FormationWallTestUtil
{
	FFormationWall TestWall()
	{
		FFormationWall Wall;
		Wall.A = FVector2D(-1000.0, 200.0);
		Wall.B = FVector2D(1000.0, 200.0);
		Wall.Thickness = 40.f;
		return Wall;
	}

	const FFormationSlot& Get(const TArray<FFormationSlot>& Slots, EFormationRole Role)
	{
		static const FFormationSlot Missing;
		const FFormationSlot* Slot = Slots.FindByPredicate([Role](const FFormationSlot& Candidate) { return Candidate.Role == Role; });
		return Slot ? *Slot : Missing;
	}

	/** Clock of a yaw relative to "facing the wall" (12 = +Y). */
	float WallClock(float Yaw)
	{
		return FormationMath::YawToClock(Yaw, 90.f);
	}

	/** Nobody looks into the wall: every look within 90 deg of the outward normal. */
	void CheckNobodyFacesTheWall(FAutomationTestBase& Test, const TArray<FFormationSlot>& Slots, const TCHAR* Case)
	{
		for (const FFormationSlot& Slot : Slots)
		{
			Test.TestTrue(FString::Printf(TEXT("%s: %s does not look into the wall (yaw %.1f)"), Case, FormationNames::ToString(Slot.Role), Slot.Yaw),
				FormationMath::AngleBetween(Slot.Yaw, -90.f) < 90.f);
			Test.TestTrue(FString::Printf(TEXT("%s: %s stands on the open side"), Case, FormationNames::ToString(Slot.Role)), Slot.Position.Y < 180.0);
		}
	}
}

FORMATION_WALL_TEST(FFormationWallThreeTest, "ThreeNobodyFacesTheWall")
bool FFormationWallThreeTest::RunTest(const FString&)
{
	using namespace FormationWallTestUtil;
	const FFormationConfig Config;
	const TArray<FFormationSlot> Slots = FormationWallRules::Layout(TestWall(), FVector2D::ZeroVector, 3, 1000.f, 1000.f, Config);
	TestEqual(TEXT("Three slots"), Slots.Num(), 3);
	const FFormationSlot& K = Get(Slots, EFormationRole::Commander);
	const FFormationSlot& P = Get(Slots, EFormationRole::Right);
	// v2.2: the triad is K, P, Z - the man on the LEFT end (Z) looks 8 (the wall rule is by position).
	const FFormationSlot& L = Get(Slots, EFormationRole::Rear);
	TestTrue(TEXT("K on the clicked slot, face offset off the wall face"), FVector2D::Distance(K.Position, FVector2D(0.0, 200.0 - 20.0 - 45.0)) < 0.01);
	TestTrue(TEXT("K looks 6 (away from the wall)"), FMath::IsNearlyEqual(WallClock(K.Yaw), 6.f, 1e-3f));
	TestTrue(TEXT("The left end (Z) looks 8"), FMath::IsNearlyEqual(WallClock(L.Yaw), 8.f, 1e-3f));
	TestTrue(TEXT("P looks 4"), FMath::IsNearlyEqual(WallClock(P.Yaw), 4.f, 1e-3f));
	TestTrue(TEXT("Facing the wall: P on the right (-X), Z on the left (+X)"), P.Position.X < 0.0 && L.Position.X > 0.0);
	TestTrue(TEXT("1 m apart along the wall"), FMath::IsNearlyEqual(FVector2D::Distance(K.Position, P.Position), 100.0, 0.01)
		&& FMath::IsNearlyEqual(FVector2D::Distance(K.Position, L.Position), 100.0, 0.01));
	CheckNobodyFacesTheWall(*this, Slots, TEXT("3"));
	return true;
}

FORMATION_WALL_TEST(FFormationWallFourTest, "FourthAtTheRoomierEnd")
bool FFormationWallFourTest::RunTest(const FString&)
{
	using namespace FormationWallTestUtil;
	const FFormationConfig Config;
	// More room on the right: Z two spacings right, takes the flank along the wall (3-4), his neighbour P turns to the centre (5).
	const TArray<FFormationSlot> Right = FormationWallRules::Layout(TestWall(), FVector2D::ZeroVector, 4, 100.f, 900.f, Config);
	TestTrue(TEXT("Right: Z two spacings out on the right"), FMath::IsNearlyEqual(Get(Right, EFormationRole::Rear).Position.X, -200.0, 0.01));
	TestTrue(TEXT("Right: Z looks 3-4"), FMath::IsWithinInclusive(WallClock(Get(Right, EFormationRole::Rear).Yaw), 3.f, 4.f));
	TestTrue(TEXT("Right: P turns 4 -> 5"), FMath::IsNearlyEqual(WallClock(Get(Right, EFormationRole::Right).Yaw), 5.f, 1e-3f));
	TestTrue(TEXT("Right: K and L unchanged (6 / 8)"), FMath::IsNearlyEqual(WallClock(Get(Right, EFormationRole::Commander).Yaw), 6.f, 1e-3f)
		&& FMath::IsNearlyEqual(WallClock(Get(Right, EFormationRole::Left).Yaw), 8.f, 1e-3f));
	CheckNobodyFacesTheWall(*this, Right, TEXT("4 right"));
	// More room on the left: mirror (Z 8-9, L 8 -> 7).
	const TArray<FFormationSlot> Left = FormationWallRules::Layout(TestWall(), FVector2D::ZeroVector, 4, 900.f, 100.f, Config);
	TestTrue(TEXT("Left: Z two spacings out on the left"), FMath::IsNearlyEqual(Get(Left, EFormationRole::Rear).Position.X, 200.0, 0.01));
	TestTrue(TEXT("Left: Z looks 8-9"), FMath::IsWithinInclusive(WallClock(Get(Left, EFormationRole::Rear).Yaw), 8.f, 9.f));
	TestTrue(TEXT("Left: L turns 8 -> 7"), FMath::IsNearlyEqual(WallClock(Get(Left, EFormationRole::Left).Yaw), 7.f, 1e-3f));
	TestTrue(TEXT("Left: P unchanged (4)"), FMath::IsNearlyEqual(WallClock(Get(Left, EFormationRole::Right).Yaw), 4.f, 1e-3f));
	CheckNobodyFacesTheWall(*this, Left, TEXT("4 left"));
	// A tie goes right; the caller breaks it by the 4th man's travel.
	TestTrue(TEXT("Tie: right"), Get(FormationWallRules::Layout(TestWall(), FVector2D::ZeroVector, 4, 500.f, 500.f, Config), EFormationRole::Rear).Position.X < 0.0);
	float RoomLeft = 500.f;
	float RoomRight = 500.f;
	FormationWallRules::BreakRoomTie(RoomLeft, RoomRight, 150.f, 400.f);
	TestTrue(TEXT("Tie, less travel to the left: left"),
		Get(FormationWallRules::Layout(TestWall(), FVector2D::ZeroVector, 4, RoomLeft, RoomRight, Config), EFormationRole::Rear).Position.X > 0.0);
	RoomLeft = 300.f;
	FormationWallRules::BreakRoomTie(RoomLeft, RoomRight, 150.f, 400.f);
	TestEqual(TEXT("No tie: rooms untouched"), RoomLeft, 300.f);
	return true;
}

FORMATION_WALL_TEST(FFormationWallPairSingleTest, "PairAndSingle")
bool FFormationWallPairSingleTest::RunTest(const FString&)
{
	using namespace FormationWallTestUtil;
	const FFormationConfig Config;
	const TArray<FFormationSlot> Pair = FormationWallRules::Layout(TestWall(), FVector2D::ZeroVector, 2, 100.f, 900.f, Config);
	TestTrue(TEXT("Pair: K 7:30"), FMath::IsNearlyEqual(WallClock(Get(Pair, EFormationRole::Commander).Yaw), 7.5f, 1e-3f));
	TestTrue(TEXT("Pair: partner 4:30, on the roomier (right) side"), FMath::IsNearlyEqual(WallClock(Get(Pair, EFormationRole::Rear).Yaw), 4.5f, 1e-3f)
		&& Get(Pair, EFormationRole::Rear).Position.X < 0.0);
	CheckNobodyFacesTheWall(*this, Pair, TEXT("2"));
	const TArray<FFormationSlot> PairLeft = FormationWallRules::Layout(TestWall(), FVector2D::ZeroVector, 2, 900.f, 100.f, Config);
	TestTrue(TEXT("Pair, more room left: partner left at 7:30, K 4:30"), Get(PairLeft, EFormationRole::Rear).Position.X > 0.0
		&& FMath::IsNearlyEqual(WallClock(Get(PairLeft, EFormationRole::Rear).Yaw), 7.5f, 1e-3f)
		&& FMath::IsNearlyEqual(WallClock(Get(PairLeft, EFormationRole::Commander).Yaw), 4.5f, 1e-3f));
	const TArray<FFormationSlot> One = FormationWallRules::Layout(TestWall(), FVector2D(50.0, -400.0), 1, 0.f, 0.f, Config);
	TestTrue(TEXT("Single: K at the wall, looks 6"), One.Num() == 1 && FMath::IsNearlyEqual(WallClock(One[0].Yaw), 6.f, 1e-3f)
		&& FVector2D::Distance(One[0].Position, FVector2D(50.0, 135.0)) < 0.01);
	return true;
}

FORMATION_WALL_TEST(FFormationWallRoomTest, "RoomAndNormal")
bool FFormationWallRoomTest::RunTest(const FString&)
{
	using namespace FormationWallTestUtil;
	float RoomLeft = 0.f;
	float RoomRight = 0.f;
	FormationWallRules::WallRoom(TestWall(), FVector2D(300.0, 0.0), RoomLeft, RoomRight);
	TestTrue(TEXT("Room right 13 m, left 7 m"), FMath::IsNearlyEqual(RoomRight, 1300.f, 0.01f) && FMath::IsNearlyEqual(RoomLeft, 700.f, 0.01f));
	TestTrue(TEXT("Outward normal towards the clicked side"), FMath::IsNearlyEqual(FormationWallRules::OutwardYaw(TestWall(), FVector2D(0.0, 0.0)), -90.f, 1e-3f));
	TestTrue(TEXT("From the other side: +90"), FMath::IsNearlyEqual(FormationWallRules::OutwardYaw(TestWall(), FVector2D(0.0, 600.0)), 90.f, 1e-3f));
	return true;
}

#undef FORMATION_WALL_TEST

#endif
