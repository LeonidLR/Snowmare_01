// CodexTactics.Formation.CornerHold.* - the two-corner hold at a wall (#57, user request 2026-11-07; Formation/FormationCornerHoldRules): which
// squad peek order is the second corner / an end already held, the middle men's eased ping-pong sweep (slices for several men, never the same
// point), the sweep's arc through the outward side, when the held corner man still holds, and the game designer's answers 2026-11-08: the
// middle men's places between the two corners (answer 1), the second corner man leads (2), the leader before him leads again (5). UE-only.

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

// The game designer's answer 1 (2026-11-08): both corners held, the middle men back to the middle of the wall, spread evenly between the corner
// men. The lab wall W_B_3: its south face y -2720 (outward yaw -90), the face-offset line y -2765, the corner men at x 3960 (east) and 1540 (west).
CORNER_HOLD_TEST(FCornerHoldMiddlePlacesTest, "MiddlePlaces")
bool FCornerHoldMiddlePlacesTest::RunTest(const FString& Parameters)
{
	using namespace FormationCornerHold;
	const FVector2D East(3960.0, -2770.0);
	const FVector2D West(1540.0, -2770.0);
	const FVector2D Line(3850.0, -2765.0); // the middle man's stack slot ~1 m from the east corner
	const TArray<FVector2D> One = MiddlePlaces(East, West, Line, -90.f, 1, 100.0);
	TestTrue(TEXT("one middle man: exactly in the middle between the corner men (x 2750)"), One.Num() == 1 && One[0].Equals(FVector2D(2750.0, -2765.0), 0.5));
	const TArray<FVector2D> Two = MiddlePlaces(East, West, Line, -90.f, 2, 100.0);
	TestTrue(TEXT("two: a third and two thirds of the way from the first corner, on the face-offset line"), Two.Num() == 2
		&& Two[0].Equals(FVector2D(3960.0 - 2420.0 / 3.0, -2765.0), 0.5) && Two[1].Equals(FVector2D(3960.0 - 2 * 2420.0 / 3.0, -2765.0), 0.5));
	const TArray<FVector2D> Reverse = MiddlePlaces(West, East, Line, -90.f, 2, 100.0);
	TestTrue(TEXT("ordered from corner A: the first place nearest A"), Reverse.Num() == 2 && Reverse[0].X < Reverse[1].X && FMath::IsNearlyEqual(Reverse[0].X, 1540.0 + 2420.0 / 3.0, 0.5));
	TestTrue(TEXT("the line's normal flipped (yaw 90) gives the same places"), MiddlePlaces(East, West, Line, 90.f, 1, 100.0)[0].Equals(One[0], 0.5));
	// A corner man standing a little off the line: only his place along the wall counts.
	TestTrue(TEXT("a corner man off the line: the places stay on the line"), MiddlePlaces(FVector2D(3960.0, -2700.0), West, Line, -90.f, 1, 100.0)[0].Equals(FVector2D(2750.0, -2765.0), 0.5));
	// Too short for everyone (every neighbour, corner men included, at least the spacing apart): the caller keeps today's places.
	TestTrue(TEXT("2 m between the corners, one man 1 m apart: fits (x 3860)"), MiddlePlaces(East, FVector2D(3760.0, -2770.0), Line, -90.f, 1, 100.0).Num() == 1);
	TestTrue(TEXT("2 m between the corners, two men 1 m apart: too short (no places)"), MiddlePlaces(East, FVector2D(3760.0, -2770.0), Line, -90.f, 2, 100.0).IsEmpty());
	TestTrue(TEXT("nobody in the middle: no places"), MiddlePlaces(East, West, Line, -90.f, 0, 100.0).IsEmpty());
	TestTrue(TEXT("the corners on the same spot: no places"), MiddlePlaces(East, East, Line, -90.f, 1, 0.0).IsEmpty());

	// Who takes which place: by their place along the wall from corner A (nobody crosses another).
	const TArray<int32> Order = OrderAlong({ FVector2D(3700.0, -2765.0), FVector2D(3800.0, -2770.0) }, East, West);
	TestTrue(TEXT("the man nearer the east corner takes the place nearer it"), Order.Num() == 2 && Order[0] == 1 && Order[1] == 0);
	const TArray<int32> Tie = OrderAlong({ FVector2D(3700.0, -2765.0), FVector2D(3700.0, -2700.0) }, East, West);
	TestTrue(TEXT("a tie keeps the input order"), Tie.Num() == 2 && Tie[0] == 0 && Tie[1] == 1);
	return true;
}

// Answers 2 and 5 (2026-11-08): the second corner man stays the formation leader; he goes down -> the man who led before him leads again.
CORNER_HOLD_TEST(FCornerHoldLeaderTest, "Leader")
bool FCornerHoldLeaderTest::RunTest(const FString& Parameters)
{
	using namespace FormationCornerHold;
	// Roster ids: 0 the east corner man (led after the first peek), 2 the west end man (takes the second corner).
	const FLeaderChain Chain = AfterSecondCorner(0, 2);
	TestEqual(TEXT("answer 2: the second corner man leads"), Chain.Leader, 2);
	TestEqual(TEXT("... the first corner man is remembered as the leader before him"), Chain.Previous, 0);
	const FLeaderChain Same = AfterSecondCorner(2, 2);
	TestTrue(TEXT("the leader takes the corner himself: nothing to remember"), Same.Leader == 2 && Same.Previous == INDEX_NONE);

	FLeaderFallFacts Facts;
	Facts.bFallenLeadsTheHold = true;
	Facts.bPreviousKnown = true;
	Facts.bPreviousAble = true;
	Facts.bPreviousHoldsOtherCorner = true;
	FLeaderAfterFall After = DecideLeaderAfterFall(Facts);
	TestTrue(TEXT("answer 5: the leader corner man down -> the leader before him leads again and keeps his corner"),
		After.Pick == ELeaderAfterFall::PreviousLeader && After.bKeepOtherCorner);
	FLeaderFallFacts OutOfCorner = Facts;
	OutOfCorner.bPreviousHoldsOtherCorner = false;
	After = DecideLeaderAfterFall(OutOfCorner);
	TestTrue(TEXT("... he left his corner: he still leads, no corner to keep"), After.Pick == ELeaderAfterFall::PreviousLeader && !After.bKeepOtherCorner);
	FLeaderFallFacts BothDown = Facts;
	BothDown.bPreviousAble = false;
	After = DecideLeaderAfterFall(BothDown);
	TestTrue(TEXT("the leader before him is down too: the existing leader-pick rule"), After.Pick == ELeaderAfterFall::Fallback && !After.bKeepOtherCorner);
	FLeaderFallFacts Unknown = Facts;
	Unknown.bPreviousKnown = false;
	TestTrue(TEXT("no leader before him remembered (left the squad): the existing rule"), DecideLeaderAfterFall(Unknown).Pick == ELeaderAfterFall::Fallback);
	FLeaderFallFacts Himself = Facts;
	Himself.bPreviousIsFallen = true;
	TestTrue(TEXT("the remembered leader is the fallen man himself: the existing rule"), DecideLeaderAfterFall(Himself).Pick == ELeaderAfterFall::Fallback);
	FLeaderFallFacts NotHold = Facts;
	NotHold.bFallenLeadsTheHold = false;
	TestTrue(TEXT("a leader down outside a two-corner hold: as before"), DecideLeaderAfterFall(NotHold).Pick == ELeaderAfterFall::NotTheHold);
	FLeaderFallFacts Off = Facts;
	Off.bEnabled = false;
	TestTrue(TEXT("Codex.Formation.TwoCornerHold 0: as before"), DecideLeaderAfterFall(Off).Pick == ELeaderAfterFall::NotTheHold);
	return true;
}

// Answer 3 (2026-11-08, "6 s, option V"): slow out of combat (corner_sweep_calm_s 12), 6 s one way in combat / alert (corner_sweep_s).
CORNER_HOLD_TEST(FCornerHoldSweepPaceTest, "SweepPace")
bool FCornerHoldSweepPaceTest::RunTest(const FString& Parameters)
{
	using namespace FormationCornerHold;
	FSweepAlertFacts Calm;
	TestFalse(TEXT("nothing seen, heard or shooting: calm"), IsSweepAlert(Calm));
	TestTrue(TEXT("calm: the slow sweep, 12 s one way"), FMath::IsNearlyEqual(SweepSecondsFor(Calm, 12.f, 6.f), 12.f));
	FSweepAlertFacts Seen;
	Seen.bThreatKnown = true;
	TestTrue(TEXT("an enemy seen / heard by the squad: 6 s"), IsSweepAlert(Seen) && FMath::IsNearlyEqual(SweepSecondsFor(Seen, 12.f, 6.f), 6.f));
	FSweepAlertFacts Fire;
	Fire.bUnderFire = true;
	TestTrue(TEXT("a member under fire: 6 s"), FMath::IsNearlyEqual(SweepSecondsFor(Fire, 12.f, 6.f), 6.f));
	FSweepAlertFacts Pinned;
	Pinned.bSuppressed = true;
	TestTrue(TEXT("a member suppressed: 6 s"), FMath::IsNearlyEqual(SweepSecondsFor(Pinned, 12.f, 6.f), 6.f));
	TestTrue(TEXT("no calm duration set: the alert one"), FMath::IsNearlyEqual(SweepSecondsFor(Calm, 0.f, 6.f), 6.f));
	TestTrue(TEXT("no alert duration set: the calm one"), FMath::IsNearlyEqual(SweepSecondsFor(Seen, 12.f, 0.f), 12.f));
	TestTrue(TEXT("neither: 0 (the middle of his slice)"), FMath::IsNearlyEqual(SweepSecondsFor(Seen, 0.f, 0.f), 0.f));
	// The runtime advances the phase by DeltaTime / duration: a calm -> alert switch mid-sweep keeps the look where it was.
	const float Before = SweepFraction(0.5f, 1.f, 0, 1);
	TestTrue(TEXT("the phase-based fraction is continuous across a pace switch"), FMath::IsNearlyEqual(SweepFraction(0.5f + 0.001f, 1.f, 0, 1), Before, 0.01f));
	return true;
}

#undef CORNER_HOLD_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
