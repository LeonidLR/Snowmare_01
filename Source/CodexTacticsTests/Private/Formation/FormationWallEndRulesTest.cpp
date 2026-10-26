// CodexTactics.Formation.WallEnd.* - the squad at a wall's end (Formation/FormationWallEndRules; the game designer's bug report 2026-10-26,
// docs/design/FORMATION_SECTORS.md "v2.4"): the corner stack at a free wall end (bug A: the point man at the edge peeks round the corner, K
// second, the rear man back along the wall) and the wall layout fitted to the world (bug B: no place on the far side of a wall from K's
// place). UE-only (no golden: the Training Ground simulator does not model it yet).

#include "Misc/AutomationTest.h"
#include "Formation/FormationWallEndRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define FORMATION_WALL_END_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Formation.WallEnd." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace FormationWallEndTestUtil
{
	FFormationWall WallEndSeg(double AX, double AY, double BX, double BY, float Thickness = 40.f)
	{
		FFormationWall Out;
		Out.A = FVector2D(AX, AY);
		Out.B = FVector2D(BX, BY);
		Out.Thickness = Thickness;
		return Out;
	}

	FFormationSlot WallEndSlot(EFormationRole Role, double X, double Y, float Yaw)
	{
		FFormationSlot Slot;
		Slot.Role = Role;
		Slot.Position = FVector2D(X, Y);
		Slot.Yaw = Yaw;
		return Slot;
	}

	const FFormationSlot* WallEndSlotOf(const TArray<FFormationSlot>& Slots, EFormationRole Role)
	{
		return Slots.FindByPredicate([Role](const FFormationSlot& Slot) { return Slot.Role == Role; });
	}

	/** The long wall along X (-600..600, 40 thick), the squad in front of it at y -65 (the outward normal yaw -90): K at 100, P at 0, Z at 200. */
	constexpr float WallEndNormalYaw = -90.f;

	TArray<FFormationSlot> WallEndLayoutOfThree()
	{
		return { WallEndSlot(EFormationRole::Commander, 100.0, -65.0, -90.f), WallEndSlot(EFormationRole::Right, 0.0, -65.0, -150.f),
			WallEndSlot(EFormationRole::Rear, 200.0, -65.0, -30.f) };
	}
}

FORMATION_WALL_END_TEST(FFormationWallEndFindStackEndTest, "FindStackEnd")
bool FFormationWallEndFindStackEndTest::RunTest(const FString&)
{
	using namespace FormationWallEndTestUtil;
	const TArray<FFormationWall> Walls = { WallEndSeg(-600.0, 0.0, 600.0, 0.0) };
	const FVector2D K(100.0, -65.0);
	TestEqual(TEXT("The squad's wall is the long wall"), FormationWallEnd::FindSquadWall(Walls, K, WallEndNormalYaw), 0);
	TestEqual(TEXT("No squad wall from its far side"), FormationWallEnd::FindSquadWall(Walls, FVector2D(100.0, 65.0), WallEndNormalYaw), INDEX_NONE);
	// The user's click: 20 cm past the free end, 90 cm off the face.
	const TOptional<FFormationStackEnd> Stack = FormationWallEnd::FindStackEnd(Walls, K, WallEndNormalYaw, FVector2D(-620.0, -110.0));
	if (TestTrue(TEXT("A move to the wall's free end is the corner stack"), Stack.IsSet()))
	{
		TestTrue(TEXT("... at the end the move runs to"), Stack->End.Equals(FVector2D(-600.0, 0.0), 0.01));
		TestTrue(TEXT("... the travel along the wall towards it"), Stack->Along.Equals(FVector2D(-1.0, 0.0), 0.001));
		TestEqual(TEXT("... the wall's thickness"), Stack->Thickness, 40.f);
	}
	TestTrue(TEXT("1 m short of the end still asks for the edge"), FormationWallEnd::FindStackEnd(Walls, K, WallEndNormalYaw, FVector2D(-500.0, -110.0)).IsSet());
	TestFalse(TEXT("3 m short of the end: a plain slide"), FormationWallEnd::FindStackEnd(Walls, K, WallEndNormalYaw, FVector2D(-300.0, -110.0)).IsSet());
	TestFalse(TEXT("Round the corner on the far side: no stack (a move round the end)"), FormationWallEnd::FindStackEnd(Walls, K, WallEndNormalYaw, FVector2D(-620.0, 110.0)).IsSet());
	TestFalse(TEXT("4 m out from the wall: no stack (a plain move)"), FormationWallEnd::FindStackEnd(Walls, K, WallEndNormalYaw, FVector2D(-620.0, -500.0)).IsSet());
	TestFalse(TEXT("A few cm along: no move at all"), FormationWallEnd::FindStackEnd(Walls, K, WallEndNormalYaw, FVector2D(80.0, -110.0)).IsSet());
	// The other end, to the right of the squad (seen facing the wall: -X is right).
	const TOptional<FFormationStackEnd> Other = FormationWallEnd::FindStackEnd(Walls, K, WallEndNormalYaw, FVector2D(610.0, -100.0));
	TestTrue(TEXT("The other free end too"), Other.IsSet() && Other->End.Equals(FVector2D(600.0, 0.0), 0.01));
	// A joint: a cross wall at the end (an L corner) - nothing to peek round.
	const TArray<FFormationWall> Corner = { WallEndSeg(-600.0, 0.0, 600.0, 0.0), WallEndSeg(-620.0, 0.0, -620.0, 800.0) };
	TestFalse(TEXT("An L corner's end is a joint: no stack"), FormationWallEnd::FindStackEnd(Corner, K, WallEndNormalYaw, FVector2D(-560.0, -110.0)).IsSet());
	return true;
}

FORMATION_WALL_END_TEST(FFormationWallEndStackSlotsTest, "StackSlots")
bool FFormationWallEndStackSlotsTest::RunTest(const FString&)
{
	using namespace FormationWallEndTestUtil;
	FFormationStackEnd End;
	End.End = FVector2D(-600.0, 0.0);
	End.Along = FVector2D(-1.0, 0.0);
	End.Thickness = 40.f;
	End.Wall = 0;
	const TArray<FFormationSlot> Start = WallEndLayoutOfThree();
	// P leads (x 0): he moves to 40 cm short of the end; the line keeps its order and spacing.
	TestEqual(TEXT("The shift along the wall"), FormationWallEnd::StackShift(Start, End), 560.f);
	const TArray<FFormationSlot> Stack = FormationWallEnd::StackSlots(Start, End, WallEndNormalYaw);
	TestEqual(TEXT("K first, in the start order"), Stack[0].Role, EFormationRole::Commander);
	const FFormationSlot* P = WallEndSlotOf(Stack, EFormationRole::Right);
	const FFormationSlot* K = WallEndSlotOf(Stack, EFormationRole::Commander);
	const FFormationSlot* Z = WallEndSlotOf(Stack, EFormationRole::Rear);
	if (!TestTrue(TEXT("Every place"), P && K && Z))
	{
		return false;
	}
	TestEqual(TEXT("The point man is the place nearest the end"), FormationWallEnd::PointRole(Stack, End), EFormationRole::Right);
	TestTrue(TEXT("The point man at the edge (40 cm short of the end)"), P->Position.Equals(FVector2D(-560.0, -65.0), 0.01));
	TestTrue(TEXT("K second, a step behind him"), K->Position.Equals(FVector2D(-460.0, -65.0), 0.01));
	TestTrue(TEXT("The rear man last"), Z->Position.Equals(FVector2D(-360.0, -65.0), 0.01));
	// The point man looks round the corner: at the point 50 cm beyond the wall body at its end -> turned from along the wall (180) towards
	// the wall's far side (+Y).
	TestEqual(TEXT("The point man peeks round the corner"), P->Yaw, static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(65.0, -110.0))), 0.01f);
	TestTrue(TEXT("... turned 10-60 deg from along the wall towards its far side"), FMath::FindDeltaAngleDegrees(180.f, P->Yaw) < -10.f
		&& FMath::FindDeltaAngleDegrees(180.f, P->Yaw) > -60.f);
	TestEqual(TEXT("K away from the wall"), K->Yaw, -90.f, 0.01f);
	TestEqual(TEXT("The rear man back along the wall, turned out (slide.lead_out_deg)"), Z->Yaw, -20.f, 0.01f);
	// Moving the other way, Z leads.
	FFormationStackEnd Right = End;
	Right.End = FVector2D(600.0, 0.0);
	Right.Along = FVector2D(1.0, 0.0);
	const TArray<FFormationSlot> Mirror = FormationWallEnd::StackSlots(Start, Right, WallEndNormalYaw);
	TestEqual(TEXT("The other end: Z is the point man"), FormationWallEnd::PointRole(Mirror, Right), EFormationRole::Rear);
	TestTrue(TEXT("... at its edge"), WallEndSlotOf(Mirror, EFormationRole::Rear)->Position.Equals(FVector2D(560.0, -65.0), 0.01));
	TestEqual(TEXT("... P the rear man looks back along the wall, turned out"), WallEndSlotOf(Mirror, EFormationRole::Right)->Yaw, -160.f, 0.01f);
	// Four: the middle member who is not K looks out, turned towards the rear.
	TArray<FFormationSlot> Four = Start;
	Four.Add(WallEndSlot(EFormationRole::Left, 300.0, -65.0, 0.f));
	const TArray<FFormationSlot> Stack4 = FormationWallEnd::StackSlots(Four, End, WallEndNormalYaw);
	TestEqual(TEXT("Four: the last man (L) looks back"), WallEndSlotOf(Stack4, EFormationRole::Left)->Yaw, -20.f, 0.01f);
	TestEqual(TEXT("Four: Z between K and L looks out, turned towards the rear (wall.neighbour_deg)"), WallEndSlotOf(Stack4, EFormationRole::Rear)->Yaw, -60.f, 0.01f);
	return true;
}

FORMATION_WALL_END_TEST(FFormationWallEndFitLayoutTest, "FitLayout")
bool FFormationWallEndFitLayoutTest::RunTest(const FString&)
{
	using namespace FormationWallEndTestUtil;
	const TArray<FFormationSlot> Three = WallEndLayoutOfThree();
	const auto Open = [](const FVector2D&, const FVector2D&) { return true; };
	const TArray<FFormationSlot> Same = FormationWallEnd::FitLayout(Three, WallEndNormalYaw, Open);
	TestTrue(TEXT("Every place usable: the layout unchanged"), Same[1].Position.Equals(Three[1].Position) && Same[1].Yaw == Three[1].Yaw);
	// A cross wall at x 50 (a T junction next to K): P's place (x 0) lies beyond it.
	const auto Junction = [](const FVector2D&, const FVector2D& Place) { return Place.X > 50.0; };
	const TArray<FFormationSlot> Fitted = FormationWallEnd::FitLayout(Three, WallEndNormalYaw, Junction);
	const FFormationSlot* P = WallEndSlotOf(Fitted, EFormationRole::Right);
	const FFormationSlot* Z = WallEndSlotOf(Fitted, EFormationRole::Rear);
	TestTrue(TEXT("K keeps the clicked place"), Fitted[0].Position.Equals(Three[0].Position) && Fitted[0].Yaw == Three[0].Yaw);
	TestTrue(TEXT("P moves to K's other side, beyond Z"), P->Position.Equals(FVector2D(300.0, -65.0), 0.01));
	TestTrue(TEXT("Z keeps his place"), Z->Position.Equals(FVector2D(200.0, -65.0), 0.01));
	TestEqual(TEXT("The outer end (P) looks along the wall (wall.end_deg)"), P->Yaw, -15.f, 0.01f);
	TestEqual(TEXT("His neighbour (Z) half way (wall.neighbour_deg)"), Z->Yaw, -60.f, 0.01f);
	// Nowhere to go (both sides cut): stays (the runtime's stand-in takes over).
	const auto Boxed = [](const FVector2D&, const FVector2D& Place) { return FMath::Abs(Place.X - 100.0) < 1.0; };
	const TArray<FFormationSlot> Kept = FormationWallEnd::FitLayout(Three, WallEndNormalYaw, Boxed);
	TestTrue(TEXT("Boxed in: the places stay"), Kept[1].Position.Equals(Three[1].Position) && Kept[2].Position.Equals(Three[2].Position));
	// The pair: the partner on the cut side moves over, the pair's looks mirror (K 7:30 / 4:30 the other way).
	const TArray<FFormationSlot> Pair = { WallEndSlot(EFormationRole::Commander, 100.0, -65.0, -45.f), WallEndSlot(EFormationRole::Rear, 0.0, -65.0, -135.f) };
	const TArray<FFormationSlot> PairFitted = FormationWallEnd::FitLayout(Pair, WallEndNormalYaw, Junction);
	TestTrue(TEXT("Pair: the partner on K's other side"), PairFitted[1].Position.Equals(FVector2D(200.0, -65.0), 0.01));
	TestEqual(TEXT("Pair: K's look mirrored"), PairFitted[0].Yaw, -135.f, 0.01f);
	TestEqual(TEXT("Pair: the partner's look mirrored"), PairFitted[1].Yaw, -45.f, 0.01f);
	return true;
}

FORMATION_WALL_END_TEST(FFormationWallEndStepRoundTest, "StepRound")
bool FFormationWallEndStepRoundTest::RunTest(const FString&)
{
	using namespace FormationWallEndTestUtil;
	// The re-form after a leader switch (FormationLeaderSwapSlideSmoke): the new leader crouched at x 208 in the line (y -65), the old one at
	// x 128 must reach his place at x 308 - the leader stands in his way.
	const FVector2D Here(128.0, -65.0);
	const FVector2D Place(308.0, -65.0);
	const TOptional<FVector2D> Step = FormationWallEnd::StepRound(Here, Place, { FVector2D(208.0, -65.0), FVector2D(38.0, -65.0) }, WallEndNormalYaw);
	if (TestTrue(TEXT("A team-mate in the line between him and his place is stepped round"), Step.IsSet()))
	{
		TestTrue(TEXT("... 110 cm out from him on the open side, 55 cm past him"), Step->Equals(FVector2D(263.0, -175.0), 0.01));
		TestFalse(TEXT("... and from there the way to the place is clear"),
			FormationWallEnd::StepRound(Step.GetValue(), Place, { FVector2D(208.0, -65.0), FVector2D(38.0, -65.0) }, WallEndNormalYaw).IsSet());
	}
	TestFalse(TEXT("A team-mate behind him is no obstacle"), FormationWallEnd::StepRound(Here, Place, { FVector2D(38.0, -65.0) }, WallEndNormalYaw).IsSet());
	TestFalse(TEXT("One standing on his place is no obstacle to step round (he waits for him)"),
		FormationWallEnd::StepRound(Here, Place, { FVector2D(290.0, -65.0) }, WallEndNormalYaw).IsSet());
	TestFalse(TEXT("One a metre out from the line leaves the way clear"), FormationWallEnd::StepRound(Here, Place, { FVector2D(208.0, -165.0) }, WallEndNormalYaw).IsSet());
	TestFalse(TEXT("A short way: nothing to step round"), FormationWallEnd::StepRound(Here, FVector2D(200.0, -65.0), { FVector2D(170.0, -65.0) }, WallEndNormalYaw).IsSet());
	return true;
}

#undef FORMATION_WALL_END_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
