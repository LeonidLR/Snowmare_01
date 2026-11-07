// CodexTactics.Formation.CornerHold.* - the two-corner hold at a wall (#57, user request 2026-11-07; Formation/FormationCornerHoldRules): which
// squad peek order is the second corner / an end already held, the middle men's eased ping-pong sweep (slices for several men, never the same
// point), the sweep's arc through the outward side, and when the held corner man still holds. UE-only (no Godot reference).

#include "Misc/AutomationTest.h"
#include "Formation/FormationCornerHoldRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define CORNER_HOLD_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Formation.CornerHold." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

CORNER_HOLD_TEST(FCornerHoldPeekOrderTest, "PeekOrder")
bool FCornerHoldPeekOrderTest::RunTest(const FString& Parameters)
{
	using namespace FormationCornerHold;
	FPeekOrderFacts Facts;
	Facts.bPeekerAtCorner = true;
	Facts.bSameWall = true;
	Facts.ToPeekEdgeCm = 2500.0;
	TestTrue(TEXT("the right man peeks, a peek at the left end of the same 25 m wall: the second corner"), DecidePeekOrder(Facts) == EPeekOrder::SecondCorner);
	FPeekOrderFacts Same = Facts;
	Same.ToPeekEdgeCm = 40.0;
	TestTrue(TEXT("the same end again: nobody swaps"), DecidePeekOrder(Same) == EPeekOrder::AlreadyHeld);
	FPeekOrderFacts Both = Facts;
	Both.bHeldCorner = true;
	Both.ToHeldEdgeCm = 30.0;
	TestTrue(TEXT("both held, the held corner's end again: nobody swaps"), DecidePeekOrder(Both) == EPeekOrder::AlreadyHeld);
	Both.ToPeekEdgeCm = 20.0;
	Both.ToHeldEdgeCm = 2500.0;
	TestTrue(TEXT("both held, the second corner's end again: nobody swaps"), DecidePeekOrder(Both) == EPeekOrder::AlreadyHeld);
	Both.ToPeekEdgeCm = 900.0;
	Both.ToHeldEdgeCm = 900.0;
	TestTrue(TEXT("both held, a third end: an ordinary peek"), DecidePeekOrder(Both) == EPeekOrder::Ordinary);
	FPeekOrderFacts Off = Facts;
	Off.bEnabled = false;
	TestTrue(TEXT("Codex.Formation.TwoCornerHold 0: the ordinary peek (the stack moves)"), DecidePeekOrder(Off) == EPeekOrder::Ordinary);
	FPeekOrderFacts Walking = Facts;
	Walking.bPeekerAtCorner = false;
	TestTrue(TEXT("the first peeker still walking to his corner: the ordinary peek"), DecidePeekOrder(Walking) == EPeekOrder::Ordinary);
	FPeekOrderFacts Other = Facts;
	Other.bSameWall = false;
	TestTrue(TEXT("another wall: the ordinary peek"), DecidePeekOrder(Other) == EPeekOrder::Ordinary);
	return true;
}

CORNER_HOLD_TEST(FCornerHoldSweepTest, "Sweep")
bool FCornerHoldSweepTest::RunTest(const FString& Parameters)
{
	using namespace FormationCornerHold;
	const float Sweep = 6.f;
	TestTrue(TEXT("one man: starts at the first corner"), FMath::IsNearlyEqual(SweepFraction(0.f, Sweep, 0, 1), 0.f, 1e-4f));
	TestTrue(TEXT("... the middle after half a sweep"), FMath::IsNearlyEqual(SweepFraction(3.f, Sweep, 0, 1), 0.5f, 1e-4f));
	TestTrue(TEXT("... the second corner after one sweep"), FMath::IsNearlyEqual(SweepFraction(6.f, Sweep, 0, 1), 1.f, 1e-4f));
	TestTrue(TEXT("... back at the first after two (ping-pong)"), FMath::IsNearlyEqual(SweepFraction(12.f, Sweep, 0, 1), 0.f, 1e-4f));
	// Eased at the ends: slow near a corner, fastest mid-arc.
	const float NearEnd = SweepFraction(0.3f, Sweep, 0, 1) - SweepFraction(0.f, Sweep, 0, 1);
	const float MidArc = SweepFraction(3.15f, Sweep, 0, 1) - SweepFraction(2.85f, Sweep, 0, 1);
	TestTrue(TEXT("eased: the turn near a corner is slower than mid-arc"), NearEnd < MidArc * 0.5f);
	// Two / three middle men: each his own slice, in step - never the same point, the whole arc covered between them.
	bool bApart = true;
	float MinSeen = 1.f;
	float MaxSeen = 0.f;
	for (float T = 0.f; T < 24.f; T += 0.05f)
	{
		const float A = SweepFraction(T, Sweep, 0, 3);
		const float B = SweepFraction(T, Sweep, 1, 3);
		const float C = SweepFraction(T, Sweep, 2, 3);
		bApart &= B - A > 0.3f && C - B > 0.3f;
		MinSeen = FMath::Min(MinSeen, A);
		MaxSeen = FMath::Max(MaxSeen, C);
	}
	TestTrue(TEXT("three middle men never look at the same point (a third of the arc apart)"), bApart);
	TestTrue(TEXT("... and between them the whole arc is swept"), MinSeen <= 0.001f && MaxSeen >= 0.999f);
	TestTrue(TEXT("an index past the count is clamped"), SweepFraction(0.f, Sweep, 5, 2) >= 0.5f);
	TestTrue(TEXT("no sweep time: the middle of his slice"), FMath::IsNearlyEqual(SweepFraction(4.f, 0.f, 0, 1), 0.5f, 1e-4f));

	// The arc through the outward side (the wall's normal looks 90): from the left corner's look (170) to the right one's (10).
	TestTrue(TEXT("fraction 0: the first corner's look"), FMath::IsNearlyEqual(SweepYaw(170.f, 10.f, 90.f, 0.f), 170.f, 0.01f));
	TestTrue(TEXT("fraction 1: the second corner's look"), FMath::IsNearlyEqual(SweepYaw(170.f, 10.f, 90.f, 1.f), 10.f, 0.01f));
	TestTrue(TEXT("halfway: straight out, never through the wall (270)"), FMath::IsNearlyEqual(SweepYaw(170.f, 10.f, 90.f, 0.5f), 90.f, 0.01f));
	// Across the +-180 seam: outward 180, corners at 100 and -100 (260).
	TestTrue(TEXT("across the seam: halfway = outward 180"), FMath::IsNearlyEqual(FMath::Abs(SweepYaw(100.f, -100.f, 180.f, 0.5f)), 180.f, 0.01f));
	TestTrue(TEXT("across the seam: a quarter = 140"), FMath::IsNearlyEqual(SweepYaw(100.f, -100.f, 180.f, 0.25f), 140.f, 0.01f));

	// The ends never turn him into the wall (outward -90: the wall north of him): a corner along the wall stays, past it is clamped to 85.
	TestTrue(TEXT("a corner straight along the wall (east, 0): 85 off outward at most"), FMath::IsNearlyEqual(ClampOffOutward(0.f, -90.f), -5.f, 0.01f));
	TestTrue(TEXT("a corner round behind the wall (100): clamped to -5 or -175"), FMath::IsNearlyEqual(FMath::Abs(FMath::FindDeltaAngleDegrees(-90.f, ClampOffOutward(100.f, -90.f))), 85.f, 0.01f));
	TestTrue(TEXT("a corner already in front (-60): unchanged"), FMath::IsNearlyEqual(ClampOffOutward(-60.f, -90.f), -60.f, 0.01f));
	return true;
}

CORNER_HOLD_TEST(FCornerHoldStillHoldsTest, "StillHolds")
bool FCornerHoldStillHoldsTest::RunTest(const FString& Parameters)
{
	using namespace FormationCornerHold;
	TestTrue(TEXT("alive, up, in cover at that corner, peeking: holds"), StillHolds(true, false, true, true));
	TestFalse(TEXT("dead: his corner is free"), StillHolds(false, false, true, true));
	TestFalse(TEXT("knocked down: his corner is free"), StillHolds(true, true, true, true));
	TestFalse(TEXT("out of his cover"), StillHolds(true, false, false, true));
	TestFalse(TEXT("not peeking any more"), StillHolds(true, false, true, false));
	return true;
}

#undef CORNER_HOLD_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
