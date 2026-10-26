// CodexTactics.Formation.V23.* - rules v2.3, the red-team fixes (contract docs/design/FORMATION_CONTRACT.md 13; mirrors
// Tools/TrainingGround/src/movement/rulesV23.test.ts): ClearLook's minimum free arc, held look, narrow tier and the longest clear ray when
// enclosed; the trail column's stagger in a narrow passage; the user-decided looks of the narrow column (Decided). UE-only.

#include "Misc/AutomationTest.h"
#include "Formation/FormationCoverRules.h"
#include "Formation/FormationSectorRules.h"
#include "Formation/FormationShapeRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define FORMATION_V23_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Formation.V23." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace FormationV23TestUtil
{
	FFormationWall V23Seg(double AX, double AY, double BX, double BY, float Thickness)
	{
		FFormationWall Out;
		Out.A = FVector2D(AX, AY);
		Out.B = FVector2D(BX, BY);
		Out.Thickness = Thickness;
		return Out;
	}

	/** Red team 2_8 (zigzag corridor): walls left-ahead (-45 deg, 1.9 m) and left-behind (-160 deg, 1.5 m) leave a sliver near -100. */
	TArray<FFormationWall> Sliver()
	{
		return { V23Seg(-290.0, -558.0, 558.0, 290.0, 40.f), V23Seg(-278.0, 325.0, -4.0, -427.0, 40.f) };
	}

	float V23Look(const TArray<FFormationWall>& Walls, float Yaw, float Current, const FFormationConfig& Config, const TOptional<float>& Held = TOptional<float>())
	{
		return FormationSectorRules::ClearLook(Walls, FVector2D::ZeroVector, Yaw, Current, Config, Held);
	}
}

FORMATION_V23_TEST(FFormationV23SliverTest, "ClearLookMinimumFreeArc")
bool FFormationV23SliverTest::RunTest(const FString&)
{
	using namespace FormationV23TestUtil;
	const FFormationConfig C;
	for (const float Want : { -100.f, -95.f })
	{
		const float Got = V23Look(Sliver(), Want, 10.f, C);
		TestTrue(FString::Printf(TEXT("Wanted %.0f in the sliver -> the edge of the wide front arc (%.1f)"), Want, Got), FMath::IsNearlyEqual(Got, 10.f, 1e-3f));
	}
	TestEqual(TEXT("A look in the wide arc stays"), V23Look(Sliver(), 30.f, 10.f, C), 30.f);
	FFormationConfig Off = C;
	Off.LookMinFreeDeg = 0.f;
	TestEqual(TEXT("min_free_deg 0: the sliver counts again (v2.2)"), V23Look(Sliver(), -100.f, 10.f, Off), -100.f);
	// A free wanted yaw is kept even when the turn sweeps a side wall (the rear guard in a corridor keeps 6 o'clock).
	const TArray<FFormationWall> Corridor = { V23Seg(-2000.0, -60.0, 2000.0, -60.0, 20.f), V23Seg(-2000.0, 60.0, 2000.0, 60.0, 20.f) };
	for (const float Current : { 0.f, 60.f, -60.f })
	{
		TestEqual(TEXT("Corridor: 6 o'clock kept"), FMath::Abs(V23Look(Corridor, 180.f, Current, C)), 180.f);
	}
	TestEqual(TEXT("Corridor: 12 kept"), V23Look(Corridor, 0.f, 180.f, C), 0.f);
	return true;
}

FORMATION_V23_TEST(FFormationV23EnclosedTest, "ClearLookEnclosedLongestRay")
bool FFormationV23EnclosedTest::RunTest(const FString&)
{
	using namespace FormationV23TestUtil;
	const FFormationConfig C;
	const TArray<FFormationWall> Pocket = { V23Seg(-100.0, -100.0, 100.0, -100.0, 20.f), V23Seg(100.0, -100.0, 100.0, 100.0, 20.f),
		V23Seg(100.0, 100.0, -100.0, 100.0, 20.f), V23Seg(-100.0, 100.0, -100.0, -100.0, 20.f) };
	TestEqual(TEXT("The diagonals tie; +45 first from 0 (+ before -)"), V23Look(Pocket, 0.f, 0.f, C), 45.f);
	TestEqual(TEXT("From 30: 45"), V23Look(Pocket, 30.f, 30.f, C), 45.f);
	TestEqual(TEXT("From 100: 135"), V23Look(Pocket, 100.f, 100.f, C), 135.f);
	TestEqual(TEXT("From -170: -135"), V23Look(Pocket, -170.f, -170.f, C), -135.f);
	return true;
}

FORMATION_V23_TEST(FFormationV23HeldTest, "ClearLookHeldLook")
bool FFormationV23HeldTest::RunTest(const FString&)
{
	using namespace FormationV23TestUtil;
	const FFormationConfig C;
	// The rear man: wanted 180 blocked by a wall behind (1.5 m); he held -120 (free) -> keeps it rather than jump to the far edge.
	const TArray<FFormationWall> Behind = { V23Seg(-170.0, -2000.0, -170.0, 2000.0, 20.f) };
	const float Half = C.LookWallAngle;
	TestTrue(TEXT("No held look: the nearest edge"), FMath::IsNearlyEqual(FMath::Abs(V23Look(Behind, 180.f, -150.f, C)), 180.f - Half, 1e-3f));
	TestEqual(TEXT("Held -120 (55 deg off the wall) is kept"), V23Look(Behind, 180.f, -150.f, C, -120.f), -120.f);
	TestTrue(TEXT("A held look too close to the wall is dropped"), FMath::IsNearlyEqual(FMath::Abs(V23Look(Behind, 180.f, -150.f, C, -140.f)), 180.f - Half, 1e-3f));
	TestEqual(TEXT("A free wanted yaw beats the held one"), V23Look(Behind, 0.f, -150.f, C, -120.f), 0.f);
	FFormationConfig NoHold = C;
	NoHold.LookHoldDeg = 0.f;
	TestTrue(TEXT("hold_deg 0: memoryless (v2.2)"), FMath::IsNearlyEqual(FMath::Abs(V23Look(Behind, 180.f, -150.f, NoHold, -120.f)), 180.f - Half, 1e-3f));
	// The narrow tier: entering a corridor, the narrow arc he reaches without a sweep beats the wide one across the wall end.
	const TArray<FFormationWall> Entrance = { V23Seg(84.0, 140.0, 1484.0, 140.0, 40.f), V23Seg(84.0, -260.0, 2484.0, -260.0, 40.f) };
	const float Got = V23Look(Entrance, 15.f, -25.f, C);
	TestTrue(FString::Printf(TEXT("Narrow tier: the near edge (%.1f)"), Got), Got < 10.f && Got > -20.f);
	FFormationConfig NoTier = C;
	NoTier.LookNarrowPenalty = 0.f;
	TestTrue(TEXT("Without the tier: the far edge"), V23Look(Entrance, 15.f, -25.f, NoTier) > 100.f);
	TestTrue(TEXT("A wide arc without a sweep still beats a narrow one"), FMath::IsNearlyEqual(V23Look(Sliver(), -100.f, 10.f, C), 10.f, 1e-3f));
	return true;
}

FORMATION_V23_TEST(FFormationV23TrailStaggerTest, "TrailStaggerShrinksInANarrowPassage")
bool FFormationV23TrailStaggerTest::RunTest(const FString&)
{
	using namespace FormationV23TestUtil;
	const FFormationConfig C;
	const TArray<FVector2D> Trail = { FVector2D(-3000.0, 0.0), FVector2D(0.0, 0.0) };
	const TArray<FFormationSlot> Free = FormationShapeRules::TrailSlots(Trail, 0.f, 4, C);
	const TArray<FFormationWall> Corridor = { V23Seg(-4000.0, -100.0, 1000.0, -100.0, 20.f), V23Seg(-4000.0, 100.0, 1000.0, 100.0, 20.f) };
	const TArray<FFormationSlot> Tight = FormationShapeRules::TrailSlots(Trail, 0.f, 4, C, Corridor);
	for (int32 Index = 0; Index < Tight.Num(); ++Index)
	{
		for (const FFormationWall& Wall : Corridor)
		{
			TestTrue(FString::Printf(TEXT("%s keeps column.stagger_clear"), FormationNames::ToString(Tight[Index].Role)),
				FormationMath::DistToWall(Tight[Index].Position, Wall) >= C.ColumnStaggerClear - 1e-6);
		}
		TestTrue(TEXT("Only the stagger changes"), Tight[Index].Position.X == Free[Index].Position.X
			&& FMath::Abs(Tight[Index].Position.Y) <= FMath::Abs(Free[Index].Position.Y) + 1e-9 && FMath::Sign(Tight[Index].Position.Y) * FMath::Sign(Free[Index].Position.Y) >= 0.0);
	}
	const FFormationSlot* P = Tight.FindByPredicate([](const FFormationSlot& Slot) { return Slot.Role == EFormationRole::Right; });
	TestTrue(TEXT("90 cm surface room - 45 clearance: P's stagger 45"), P && FMath::IsNearlyEqual(FMath::Abs(P->Position.Y), 45.0, 1e-6));
	const TArray<FFormationWall> Wide = { V23Seg(-4000.0, -300.0, 1000.0, -300.0, 20.f), V23Seg(-4000.0, 300.0, 1000.0, 300.0, 20.f) };
	const TArray<FFormationSlot> WideSlots = FormationShapeRules::TrailSlots(Trail, 0.f, 4, C, Wide);
	bool bSame = WideSlots.Num() == Free.Num();
	for (int32 Index = 0; bSame && Index < Free.Num(); ++Index)
	{
		bSame &= FVector2D::Distance(WideSlots[Index].Position, Free[Index].Position) < 1e-9;
	}
	TestTrue(TEXT("A wide corridor: unchanged"), bSame);
	return true;
}

FORMATION_V23_TEST(FFormationV23DecidedTest, "NarrowColumnKeepsDecidedLooks")
bool FFormationV23DecidedTest::RunTest(const FString&)
{
	using namespace FormationV23TestUtil;
	using EV = EFormationRole;
	const FFormationConfig C;
	const TOptional<float> Narrow(C.ColumnNarrowWidth - 1.f);
	const auto D = [&C](EV Role, EFormationKind Formation, int32 Count, const TOptional<float>& Width) { return FormationSectorRules::Decided(Role, Formation, Count, Width, C); };
	TestTrue(TEXT("Narrow column of 4: K, P, L decided, Z not"), D(EV::Commander, EFormationKind::Column, 4, Narrow) && D(EV::Right, EFormationKind::Column, 4, Narrow)
		&& D(EV::Left, EFormationKind::Column, 4, Narrow) && !D(EV::Rear, EFormationKind::Column, 4, Narrow));
	TestTrue(TEXT("Narrow column of 3: K, P decided, Z not"), D(EV::Commander, EFormationKind::Column, 3, Narrow) && D(EV::Right, EFormationKind::Column, 3, Narrow)
		&& !D(EV::Rear, EFormationKind::Column, 3, Narrow));
	TestFalse(TEXT("A wide column: not decided"), D(EV::Right, EFormationKind::Column, 4, TOptional<float>(C.ColumnNarrowWidth + 1.f)));
	TestFalse(TEXT("No passage = wide"), D(EV::Right, EFormationKind::Column, 4, TOptional<float>()));
	TestFalse(TEXT("The pair keeps LookYaw anyway"), D(EV::Right, EFormationKind::Column, 2, Narrow));
	for (const EFormationKind Formation : { EFormationKind::Line, EFormationKind::Diamond, EFormationKind::EchelonRight, EFormationKind::Wedge, EFormationKind::Wall })
	{
		TestFalse(FString::Printf(TEXT("%s: not decided"), FormationNames::ToString(Formation)), D(EV::Right, Formation, 4, Narrow));
	}
	// The allocator with those fixed looks in a 2 m corridor: P / L as LookYaw, Z the rear guard at 6 (halt).
	const TArray<FFormationWall> Corridor = { V23Seg(-3000.0, -100.0, 1000.0, -100.0, 20.f), V23Seg(-3000.0, 100.0, 1000.0, 100.0, 20.f) };
	const TArray<FFormationSlot> Slots = FormationShapeRules::Slots(EFormationKind::Column, 4, FVector2D::ZeroVector, 0.f, C);
	TArray<FFormationRoleYaw> Fixed;
	TArray<FFormationCoverMember> Members;
	for (const FFormationSlot& Slot : Slots)
	{
		if (D(Slot.Role, EFormationKind::Column, 4, TOptional<float>(200.f)))
		{
			Fixed.Add({ Slot.Role, FormationSectorRules::LookYaw(Slot.Role, EFormationKind::Column, EFormationMode::Halt, 4, 0.f, 0.f, TOptional<float>(200.f), C) });
		}
		FFormationCoverMember Member;
		Member.Role = Slot.Role;
		Member.Pos = Slot.Position;
		Member.Back = static_cast<float>(-Slot.Position.X);
		Members.Add(Member);
	}
	const TOptional<FFormationCoverResult> Result = FormationCoverRules::Allocate(Corridor, FVector2D::ZeroVector, 0.f, Members, EFormationMode::Halt, 0.f, C, Fixed);
	TestTrue(TEXT("Z is the rear guard"), Result.IsSet() && Result->RearGuard == TOptional<EV>(EV::Rear));
	for (const FFormationRoleYaw& Look : Fixed)
	{
		const FFormationRoleYaw* Got = Result.IsSet() ? Result->Looks.FindByPredicate([&Look](const FFormationRoleYaw& L) { return L.Role == Look.Role; }) : nullptr;
		TestTrue(FString::Printf(TEXT("%s keeps the decided look"), FormationNames::ToString(Look.Role)), Got && FMath::IsNearlyEqual(Got->Yaw, Look.Yaw, 1e-3f));
	}
	const FFormationRoleYaw* Z = Result.IsSet() ? Result->Looks.FindByPredicate([](const FFormationRoleYaw& L) { return L.Role == EV::Rear; }) : nullptr;
	TestTrue(TEXT("Z at 6 o'clock"), Z && FMath::IsNearlyEqual(FMath::Abs(Z->Yaw), 180.f, 1e-3f));
	return true;
}

#undef FORMATION_V23_TEST

#endif
