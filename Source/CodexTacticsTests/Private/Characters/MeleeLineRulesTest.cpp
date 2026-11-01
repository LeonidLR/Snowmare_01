#include "Misc/AutomationTest.h"
#include "Characters/MeleeLineRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Game designer bug 06 (2026-11-01, L_FormationLabCombat): the Cutter hit and pounced through the lab's 3 m walls. UE-only rule
// (Godot can_melee_attack / start_jump_attack check the distance only): a blow and a pounce need a clear line, the pounce a free arc.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMeleeLineRulesLineTest, "CodexTactics.Characters.EnemyAI.MeleeLine.Line",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMeleeLineRulesLineTest::RunTest(const FString&)
{
	using namespace MeleeLineRules;
	TestTrue(TEXT("the line runs over low cover (130 cm) and under a full-height wall (180 cm)"), LineHeightCm > 130.f && LineHeightCm < 180.f);
	TestTrue(TEXT("line point 150 cm over the feet"), LinePoint(FVector(10.f, 20.f, 5.f)).Equals(FVector(10.f, 20.f, 155.f)));

	TestEqual(TEXT("the target itself"), ClassifyHit(true, true, true), EMeleeLineHit::Target);
	TestEqual(TEXT("another character"), ClassifyHit(false, true, true), EMeleeLineHit::Pawn);
	TestEqual(TEXT("a click proxy / trigger (does not block bodies)"), ClassifyHit(false, false, false), EMeleeLineHit::NonSolid);
	TestEqual(TEXT("a wall"), ClassifyHit(false, false, true), EMeleeLineHit::Solid);

	TestTrue(TEXT("nothing hit: clear"), IsLineClear({}));
	TestTrue(TEXT("straight to the target: clear"), IsLineClear({ EMeleeLineHit::Target }));
	TestTrue(TEXT("past another hound and a click proxy to the target: clear"),
		IsLineClear({ EMeleeLineHit::Pawn, EMeleeLineHit::NonSolid, EMeleeLineHit::Target }));
	TestFalse(TEXT("a wall first: blocked (the bug: the blow landed through it)"), IsLineClear({ EMeleeLineHit::Solid, EMeleeLineHit::Target }));
	TestFalse(TEXT("a body, then a wall: blocked"), IsLineClear({ EMeleeLineHit::Pawn, EMeleeLineHit::Solid }));
	TestTrue(TEXT("the target before a wall behind him: clear"), IsLineClear({ EMeleeLineHit::Target, EMeleeLineHit::Solid }));

	TestTrue(TEXT("in reach, clear line: the blow lands"), MayStrike(true, true));
	TestFalse(TEXT("in reach through a wall: no blow"), MayStrike(true, false));
	TestFalse(TEXT("out of reach: no blow"), MayStrike(false, true));

	TestTrue(TEXT("the pounce landing hurts within 2.2 m with a clear line"), PounceReaches(200.f, 50.f, 220.f, true));
	TestFalse(TEXT("... never through the wall it ran into"), PounceReaches(120.f, 0.f, 220.f, false));
	TestFalse(TEXT("... nor beyond the radius"), PounceReaches(230.f, 0.f, 220.f, true));
	TestFalse(TEXT("... nor 1.5 m+ above / below (Godot)"), PounceReaches(100.f, -160.f, 220.f, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMeleeLineRulesLeapTest, "CodexTactics.Characters.EnemyAI.MeleeLine.Leap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMeleeLineRulesLeapTest::RunTest(const FString&)
{
	using namespace MeleeLineRules;
	// Cutter: 3.5-9 m, at most 2.5 m of height difference (Godot enemy_cutter.gd).
	TestEqual(TEXT("6 m, level, clear: leap"), EvaluateLeap(600.f, 0.f, 350.f, 900.f, true, true), ECutterLeapCheck::Leap);
	TestEqual(TEXT("3 m: too close"), EvaluateLeap(300.f, 0.f, 350.f, 900.f, true, true), ECutterLeapCheck::OutOfRange);
	TestEqual(TEXT("10 m: too far"), EvaluateLeap(1000.f, 0.f, 350.f, 900.f, true, true), ECutterLeapCheck::OutOfRange);
	TestEqual(TEXT("3 m up: too high"), EvaluateLeap(600.f, 300.f, 350.f, 900.f, true, true), ECutterLeapCheck::TooHigh);
	TestEqual(TEXT("3 m down: too high"), EvaluateLeap(600.f, -300.f, 350.f, 900.f, true, true), ECutterLeapCheck::TooHigh);
	TestEqual(TEXT("a wall between: no leap (bug 06)"), EvaluateLeap(600.f, 0.f, 350.f, 900.f, false, true), ECutterLeapCheck::WallInLine);
	TestEqual(TEXT("the arc runs into something: no leap"), EvaluateLeap(600.f, 0.f, 350.f, 900.f, true, false), ECutterLeapCheck::PathBlocked);
	TestEqual(TEXT("the range is checked first (no traces wasted)"), EvaluateLeap(200.f, 0.f, 350.f, 900.f, false, false), ECutterLeapCheck::OutOfRange);

	// The flight (Godot _launch_jump_flight): starts at Start, ends at Landing after the flight time, highest in the middle.
	const float Gravity = 980.f;
	const float Flight = 1.3f / 1.85f; // the cutter's jump speed
	const FVector Start(0.f, 0.f, 90.f);
	const FVector Landing(600.f, 0.f, 90.f);
	const TArray<FVector> Arc = LeapArc(Start, Landing, Flight, Gravity, 900.f, 8);
	TestEqual(TEXT("9 points for 8 segments"), Arc.Num(), 9);
	TestTrue(TEXT("the arc starts at the cutter"), Arc[0].Equals(Start, 0.1f));
	TestTrue(TEXT("the arc lands on the target"), Arc.Last().Equals(Landing, 0.5f));
	const float Apex = Gravity * Flight * Flight / 8.f; // a level flight's rise at half time
	TestTrue(FString::Printf(TEXT("highest at half time: %.1f cm over the start (g T^2 / 8 = %.1f) - far below a 3 m lab wall"), Arc[4].Z - Start.Z, Apex),
		FMath::IsNearlyEqual(Arc[4].Z - Start.Z, Apex, 0.5f) && Arc[4].Z - Start.Z < 150.f);
	TestTrue(TEXT("the arc's middle is half way across"), FMath::IsNearlyEqual(Arc[4].X, 300.f, 0.5f));
	const FVector Velocity = LeapVelocity(Start, Landing, Flight, Gravity, 900.f);
	TestTrue(TEXT("the launch velocity carries it to the landing"), LeapArcPoint(Start, Velocity, Gravity, Flight).Equals(Landing, 0.5f));
	const FVector Far(2000.f, 0.f, 90.f);
	TestTrue(TEXT("beyond the max distance the flight is cut to 9 m"), FMath::IsNearlyEqual(LeapArc(Start, Far, Flight, Gravity, 900.f).Last().X, 900.f, 0.5f));
	const FVector Up(600.f, 0.f, 290.f);
	TestTrue(TEXT("onto a 2 m ledge: it lands at the ledge's height"), FMath::IsNearlyEqual(LeapArc(Start, Up, Flight, Gravity, 900.f).Last().Z, 290.f, 0.5f));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
