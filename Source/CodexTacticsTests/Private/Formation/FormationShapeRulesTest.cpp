// CodexTactics.Formation.Shape.* - formation slots (Formation/FormationShapeRules; design docs/design/FORMATION_SECTORS.md, mirrors
// Tools/TrainingGround/src/movement/rules.test.ts). UE-only (no Godot reference). UE yaw: 3 o'clock = heading + 90 = right (+Y at yaw 0).

#include "Misc/AutomationTest.h"
#include "Formation/FormationShapeRules.h"
#include "Formation/FormationSectorRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define FORMATION_SHAPE_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Formation.Shape." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace FormationShapeTestUtil
{
	bool Near(const FVector2D& A, const FVector2D& B, double Tolerance = 0.01)
	{
		return FVector2D::Distance(A, B) <= Tolerance;
	}

	const FFormationSlot* Find(const TArray<FFormationSlot>& Slots, EFormationRole Role)
	{
		return Slots.FindByPredicate([Role](const FFormationSlot& Slot) { return Slot.Role == Role; });
	}

	FVector2D PosOf(const TArray<FFormationSlot>& Slots, EFormationRole Role)
	{
		const FFormationSlot* Slot = Find(Slots, Role);
		return Slot ? Slot->Position : FVector2D(1e9, 1e9);
	}
}

FORMATION_SHAPE_TEST(FFormationShapeConventionsTest, "Conventions")
bool FFormationShapeConventionsTest::RunTest(const FString&)
{
	TestTrue(TEXT("3 o'clock = heading + 90 (right)"), FMath::IsNearlyEqual(FormationMath::ClockToYaw(3.f, 0.f), 90.f));
	TestTrue(TEXT("9 o'clock = heading - 90 (left)"), FMath::IsNearlyEqual(FormationMath::ClockToYaw(9.f, 0.f), -90.f));
	TestTrue(TEXT("6 o'clock of 30 = -150"), FMath::IsNearlyEqual(FormationMath::ClockToYaw(6.f, 30.f), -150.f));
	TestTrue(TEXT("Right of yaw 0 is +Y"), FormationShapeTestUtil::Near(FormationMath::Right(0.f), FVector2D(0.0, 1.0), 1e-6));
	TestTrue(TEXT("Heading +Y: right is -X"), FormationShapeTestUtil::Near(FormationMath::Right(90.f), FVector2D(-1.0, 0.0), 1e-6));
	TestTrue(TEXT("Yaw of +Y is 90"), FMath::IsNearlyEqual(FormationMath::YawOf(FVector2D(0.0, 1.0)), 90.f));
	TestTrue(TEXT("Clock of 90 from 0 = 3"), FMath::IsNearlyEqual(FormationMath::YawToClock(90.f, 0.f), 3.f, 1e-4f));
	TestEqual(TEXT("Roles of 1"), FormationShapeRules::Roles(1).Num(), 1);
	TestTrue(TEXT("Roles of 2: K, Z (the pair's partner)"), FormationShapeRules::Roles(2) == TArray<EFormationRole>({ EFormationRole::Commander, EFormationRole::Rear }));
	TestTrue(TEXT("Roles of 3: K, P, L"), FormationShapeRules::Roles(3) == TArray<EFormationRole>({ EFormationRole::Commander, EFormationRole::Right, EFormationRole::Left }));
	TestEqual(TEXT("Roles of 6 clamp to 4"), FormationShapeRules::Roles(6).Num(), 4);
	return true;
}

FORMATION_SHAPE_TEST(FFormationShapeDiamondTest, "DiamondFromTheDesignTable")
bool FFormationShapeDiamondTest::RunTest(const FString&)
{
	using namespace FormationShapeTestUtil;
	const FFormationConfig Config;
	// P 2.6 right / 2.8 back, L 2.6 left / 2.8 back, Z 5.6 straight back (not the old 2nd-row-left slot 2).
	const TArray<FFormationSlot> Slots = FormationShapeRules::Slots(EFormationKind::Diamond, 4, FVector2D(1000.0, 500.0), 0.f, Config);
	TestEqual(TEXT("Four slots"), Slots.Num(), 4);
	TestTrue(TEXT("Slot order K, P, L, Z"), Slots.Num() == 4 && Slots[0].Role == EFormationRole::Commander && Slots[1].Role == EFormationRole::Right
		&& Slots[2].Role == EFormationRole::Left && Slots[3].Role == EFormationRole::Rear);
	TestTrue(TEXT("K on the leader"), Near(PosOf(Slots, EFormationRole::Commander), FVector2D(1000.0, 500.0)));
	TestTrue(TEXT("P 2.6 m right, 2.8 m back"), Near(PosOf(Slots, EFormationRole::Right), FVector2D(720.0, 760.0)));
	TestTrue(TEXT("L 2.6 m left, 2.8 m back"), Near(PosOf(Slots, EFormationRole::Left), FVector2D(720.0, 240.0)));
	TestTrue(TEXT("Z 5.6 m straight back"), Near(PosOf(Slots, EFormationRole::Rear), FVector2D(440.0, 500.0)));
	// Heading +Y: right is -X.
	const TArray<FFormationSlot> Turned = FormationShapeRules::Slots(EFormationKind::Diamond, 4, FVector2D::ZeroVector, 90.f, Config);
	TestTrue(TEXT("Heading +Y: P at (-260, -280)"), Near(PosOf(Turned, EFormationRole::Right), FVector2D(-260.0, -280.0)));
	// The slot yaws are the halt sector centres (12 / 2 / 10 / 6).
	TestTrue(TEXT("Halt yaw P = 2 o'clock"), FMath::IsNearlyEqual(Slots[1].Yaw, 60.f) && FMath::IsNearlyEqual(Slots[2].Yaw, -60.f)
		&& FMath::IsNearlyEqual(FMath::Abs(Slots[3].Yaw), 180.f));
	return true;
}

FORMATION_SHAPE_TEST(FFormationShapeWedgePairWallTest, "WedgePairAndWall")
bool FFormationShapeWedgePairWallTest::RunTest(const FString&)
{
	using namespace FormationShapeTestUtil;
	const FFormationConfig Config;
	// 3 men = wedge (the diamond's first three: the triad keeps its rear gap); no 2x2 square.
	const TArray<FFormationSlot> Wedge = FormationShapeRules::Slots(EFormationKind::Wedge, 3, FVector2D::ZeroVector, 0.f, Config);
	TestTrue(TEXT("Wedge K, P, L"), Wedge.Num() == 3 && Wedge[1].Role == EFormationRole::Right && Wedge[2].Role == EFormationRole::Left);
	TestTrue(TEXT("Diamond with 3 = the wedge"), Near(PosOf(FormationShapeRules::Slots(EFormationKind::Diamond, 3, FVector2D::ZeroVector, 0.f, Config),
		EFormationRole::Left), PosOf(Wedge, EFormationRole::Left)));
	// Pair: the partner behind the right shoulder.
	const TArray<FFormationSlot> Pair = FormationShapeRules::Slots(EFormationKind::Diamond, 2, FVector2D::ZeroVector, 0.f, Config);
	TestTrue(TEXT("Pair K, Z"), Pair.Num() == 2 && Pair[1].Role == EFormationRole::Rear);
	TestTrue(TEXT("Partner 2.2 m back, 1.8 m right"), Pair.Num() == 2 && Near(Pair[1].Position, FVector2D(-220.0, 180.0)));
	// The wall belongs to FormationWallRules::Layout.
	TestEqual(TEXT("Slots(wall) = []"), FormationShapeRules::Slots(EFormationKind::Wall, 4, FVector2D::ZeroVector, 0.f, Config).Num(), 0);
	return true;
}

FORMATION_SHAPE_TEST(FFormationShapeEchelonTest, "EchelonRightPutsTheLeftManSecond")
bool FFormationShapeEchelonTest::RunTest(const FString&)
{
	using namespace FormationShapeTestUtil;
	const FFormationConfig Config;
	// Obstacle on the left, guns to the right: from the diamond the LEFT man goes second (minimal travel).
	const TArray<FFormationSlot> Right = FormationShapeRules::Slots(EFormationKind::EchelonRight, 4, FVector2D::ZeroVector, 0.f, Config);
	TestTrue(TEXT("L second: 1.8 m back, 1.8 m right"), Near(PosOf(Right, EFormationRole::Left), FVector2D(-180.0, 180.0)));
	TestTrue(TEXT("P third"), Near(PosOf(Right, EFormationRole::Right), FVector2D(-360.0, 360.0)));
	TestTrue(TEXT("Z last"), Near(PosOf(Right, EFormationRole::Rear), FVector2D(-540.0, 540.0)));
	TestEqual(TEXT("Echelon right index of L"), FormationShapeRules::EchelonIndex(EFormationKind::EchelonRight, EFormationRole::Left, 4), 1);
	// Mirror: the RIGHT man second.
	const TArray<FFormationSlot> Left = FormationShapeRules::Slots(EFormationKind::EchelonLeft, 4, FVector2D::ZeroVector, 0.f, Config);
	TestTrue(TEXT("Echelon left: P second, to the left"), Near(PosOf(Left, EFormationRole::Right), FVector2D(-180.0, -180.0)));
	TestTrue(TEXT("Echelon left: L third"), Near(PosOf(Left, EFormationRole::Left), FVector2D(-360.0, -360.0)));
	// Minimal travel: the diamond -> echelon right reshuffle is cheaper with L second than with P second.
	const TArray<FFormationSlot> Diamond = FormationShapeRules::Slots(EFormationKind::Diamond, 4, FVector2D::ZeroVector, 0.f, Config);
	const double Chosen = FVector2D::Distance(PosOf(Diamond, EFormationRole::Left), FVector2D(-180.0, 180.0))
		+ FVector2D::Distance(PosOf(Diamond, EFormationRole::Right), FVector2D(-360.0, 360.0));
	const double Swapped = FVector2D::Distance(PosOf(Diamond, EFormationRole::Right), FVector2D(-180.0, 180.0))
		+ FVector2D::Distance(PosOf(Diamond, EFormationRole::Left), FVector2D(-360.0, 360.0));
	TestTrue(TEXT("L second is the shorter total travel"), Chosen < Swapped);
	return true;
}

FORMATION_SHAPE_TEST(FFormationShapeColumnLineTest, "ColumnHerringboneAndLine")
bool FFormationShapeColumnLineTest::RunTest(const FString&)
{
	using namespace FormationShapeTestUtil;
	const FFormationConfig Config;
	const TArray<FFormationSlot> Column = FormationShapeRules::Slots(EFormationKind::Column, 4, FVector2D::ZeroVector, 0.f, Config);
	TestTrue(TEXT("Column P right of the axis"), Near(PosOf(Column, EFormationRole::Right), FVector2D(-180.0, 60.0)));
	TestTrue(TEXT("Column L left of the axis"), Near(PosOf(Column, EFormationRole::Left), FVector2D(-330.0, -60.0)));
	TestTrue(TEXT("Column Z on the axis"), Near(PosOf(Column, EFormationRole::Rear), FVector2D(-480.0, 0.0)));
	const TArray<FFormationSlot> Line = FormationShapeRules::Slots(EFormationKind::Line, 4, FVector2D::ZeroVector, 0.f, Config);
	TestTrue(TEXT("Line L, K, P, Z abreast"), Near(PosOf(Line, EFormationRole::Left), FVector2D(0.0, -260.0))
		&& Near(PosOf(Line, EFormationRole::Right), FVector2D(0.0, 260.0)) && Near(PosOf(Line, EFormationRole::Rear), FVector2D(0.0, 520.0)));
	return true;
}

FORMATION_SHAPE_TEST(FFormationShapeBlizzardHedgehogTest, "BlizzardTightAndHedgehogFacesOut")
bool FFormationShapeBlizzardHedgehogTest::RunTest(const FString&)
{
	using namespace FormationShapeTestUtil;
	const FFormationConfig Config;
	// Blizzard: the diamond shrinks to 1.1 - 1.2 m, the sectors stay.
	const TArray<FFormationSlot> Blizzard = FormationShapeRules::Slots(EFormationKind::Blizzard, 4, FVector2D::ZeroVector, 0.f, Config);
	TestTrue(TEXT("Blizzard P 1.1 right 1.2 back"), Near(PosOf(Blizzard, EFormationRole::Right), FVector2D(-120.0, 110.0)));
	TestTrue(TEXT("Blizzard Z 2.4 back"), Near(PosOf(Blizzard, EFormationRole::Rear), FVector2D(-240.0, 0.0)));
	for (const FFormationSlot& Slot : Blizzard)
	{
		TestTrue(TEXT("Blizzard: within 2.5 m of the leader"), Slot.Position.Size() <= 250.0);
	}
	TestTrue(TEXT("Blizzard keeps the diamond's halt sectors"), FMath::IsNearlyEqual(Blizzard[1].Yaw, 60.f));

	// Hedgehog: backs to the centre, faces out - every member's halt yaw points away from the ring's centre.
	for (int32 Count = 2; Count <= 4; ++Count)
	{
		const TArray<FFormationSlot> Ring = FormationShapeRules::Slots(EFormationKind::Hedgehog, Count, FVector2D(300.0, -200.0), 30.f, Config);
		FVector2D Centre = FVector2D::ZeroVector;
		for (const FFormationSlot& Slot : Ring)
		{
			Centre += Slot.Position / static_cast<double>(Ring.Num());
		}
		for (const FFormationSlot& Slot : Ring)
		{
			const FVector2D Out = (Slot.Position - Centre).GetSafeNormal();
			const double Facing = FVector2D::DotProduct(Out, FormationMath::Forward(Slot.Yaw));
			TestTrue(FString::Printf(TEXT("Hedgehog %d: %s faces out (dot %.2f)"), Count, FormationNames::ToString(Slot.Role), Facing), Facing > 0.99);
			TestTrue(FString::Printf(TEXT("Hedgehog %d: %s on the ring"), Count, FormationNames::ToString(Slot.Role)),
				FMath::IsNearlyEqual(FVector2D::Distance(Slot.Position, Centre), static_cast<double>(Config.HedgehogRadius), 0.5));
		}
	}
	return true;
}

#undef FORMATION_SHAPE_TEST

#endif
