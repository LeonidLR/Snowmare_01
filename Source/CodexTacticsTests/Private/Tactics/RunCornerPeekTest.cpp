// CodexTactics.Tactics.Cover.RunCornerPeek.* - the run-and-peek click (#55, user request 2026-11-07; Tactics/RunCornerPeekRules): when a
// click on a wall's end is a run-and-peek, which face of the wall he takes (the one facing him), the probe directions, the face width, the
// arrival test and who carries the order out. UE-only (no Godot reference).

#include "Misc/AutomationTest.h"
#include "Tactics/RunCornerPeekRules.h"
#include "Formation/FormationWallLeadershipRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define RUN_CORNER_PEEK_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Tactics.Cover.RunCornerPeek." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

RUN_CORNER_PEEK_TEST(FRunCornerPeekClickGateTest, "ClickGate")
bool FRunCornerPeekClickGateTest::RunTest(const FString& Parameters)
{
	using namespace RunCornerPeekRules;
	const FClickFacts Open;
	TestTrue(TEXT("real time, from the open, a wall's end: a run-and-peek"), MayRunToPeek(Open));
	const auto With = [](TFunctionRef<void(FClickFacts&)> Change)
	{
		FClickFacts Facts;
		Change(Facts);
		return MayRunToPeek(Facts);
	};
	TestFalse(TEXT("Codex.Cover.RunCornerPeek 0: the old rule"), With([](FClickFacts& F) { F.bEnabled = false; }));
	TestFalse(TEXT("turn-based: its own cover rules (out of scope)"), With([](FClickFacts& F) { F.bTurnBased = true; }));
	TestTrue(TEXT("Q3 (2026-11-08): the tactical pause plans the run-and-peek too (no longer the old cover click)"), With([](FClickFacts& F) { F.bPausePlanning = true; }));
	TestFalse(TEXT("Alt (squad-wide) keeps 'take cover at that object'"), With([](FClickFacts& F) { F.bSquadWide = true; }));
	TestFalse(TEXT("the tutorial has not taught the corner peek"), With([](FClickFacts& F) { F.bTutorialLocked = true; }));
	TestFalse(TEXT("a click on a pawn never peeks"), With([](FClickFacts& F) { F.bHitPawn = true; }));
	TestFalse(TEXT("an object keeps its own click meaning"), With([](FClickFacts& F) { F.bHitObject = true; }));
	TestFalse(TEXT("in cover at that object: the in-cover peek / shimmy / wrap decide"), With([](FClickFacts& F) { F.bInCoverAtClickedObject = true; }));
	TestFalse(TEXT("from a wall edge at a cover-switch target: the switch's preview + dash"), With([](FClickFacts& F) { F.bCoverSwitchTarget = true; }));
	return true;
}

RUN_CORNER_PEEK_TEST(FRunCornerPeekNearFaceTest, "NearFace")
bool FRunCornerPeekNearFaceTest::RunTest(const FString& Parameters)
{
	using namespace RunCornerPeekRules;
	// A 30 cm wall along X (y 0..30): its south face looks -Y, its north face +Y.
	TArray<FFace> Faces;
	FFace South;
	South.Normal = FVector2D(0.0, -1.0);
	South.Point = FVector2D(-20.0, 0.0);
	FFace North;
	North.Normal = FVector2D(0.0, 1.0);
	North.Point = FVector2D(-20.0, 30.0);
	Faces.Add(South);
	Faces.Add(North);
	TestEqual(TEXT("running in from the south, 10 m out: the south face"), ChooseNearFace(Faces, FVector2D(300.0, -1000.0)), 0);
	TestEqual(TEXT("from the north: the north face"), ChooseNearFace(Faces, FVector2D(-500.0, 1200.0)), 1);
	TestEqual(TEXT("diagonally from the south-west, past the wall's end: still the south face"), ChooseNearFace(Faces, FVector2D(-900.0, -300.0)), 0);
	// Level with the wall's line beyond its end: both faces score ~0 - the nearer face point wins.
	FFace Far = North;
	Far.Point = FVector2D(-20.0, 30.0);
	TArray<FFace> Level{ South, Far };
	const int32 Pick = ChooseNearFace(Level, FVector2D(-1000.0, 15.0));
	TestTrue(TEXT("level with the wall line: one of the two faces"), Pick == 0 || Pick == 1);
	TestEqual(TEXT("no faces: none"), ChooseNearFace(TArray<FFace>(), FVector2D::ZeroVector), static_cast<int32>(INDEX_NONE));
	TArray<FFace> One{ North };
	TestEqual(TEXT("one face (the far one of a thick building is not probed): it"), ChooseNearFace(One, FVector2D(0.0, -800.0)), 0);
	return true;
}

RUN_CORNER_PEEK_TEST(FRunCornerPeekFacesTest, "FaceProbesAndWidth")
bool FRunCornerPeekFacesTest::RunTest(const FString& Parameters)
{
	using namespace RunCornerPeekRules;
	const TArray<FVector2D> Directions = ProbeDirections(FVector2D(0.0, -2.0), 8);
	TestEqual(TEXT("eight probe directions"), Directions.Num(), 8);
	TestTrue(TEXT("the first is the clicked face's normal (unit)"), Directions[0].Equals(FVector2D(0.0, -1.0), 1e-6));
	TestTrue(TEXT("the fifth is the opposite side (the far face)"), Directions[4].Equals(FVector2D(0.0, 1.0), 1e-6));
	TestTrue(TEXT("the third / seventh are the wall's ends"), FMath::IsNearlyEqual(FMath::Abs(Directions[2].X), 1.0, 1e-6) && FMath::IsNearlyEqual(Directions[2].X, -Directions[6].X, 1e-6));
	bool bUnit = true;
	for (const FVector2D& Each : Directions)
	{
		bUnit &= FMath::IsNearlyEqual(Each.Size(), 1.0, 1e-6);
	}
	TestTrue(TEXT("all unit vectors"), bUnit);
	TestEqual(TEXT("a zero primary still probes"), ProbeDirections(FVector2D::ZeroVector, 4).Num(), 4);
	TestEqual(TEXT("count < 1: the primary alone"), ProbeDirections(FVector2D(1.0, 0.0), 0).Num(), 1);

	TestTrue(TEXT("a long wall (one end open): wide"), IsFaceWideEnough(true, 20.f, false, 0.f));
	TestTrue(TEXT("a 2.5 m container side: wide"), IsFaceWideEnough(true, 60.f, true, 190.f));
	TestFalse(TEXT("a 30 cm end face: no run-and-peek face"), IsFaceWideEnough(true, 15.f, true, 15.f));
	TestFalse(TEXT("a 60 cm post"), IsFaceWideEnough(true, 30.f, true, 30.f));
	TestTrue(TEXT("exactly the minimum"), IsFaceWideEnough(true, 50.f, true, 50.f));

	TArray<FVector2D> Known{ FVector2D(0.0, -1.0) };
	TestFalse(TEXT("the same face again (a probe from the diagonal)"), IsNewFace(Known, FVector2D(0.05, -1.0)));
	TestTrue(TEXT("the far face is new"), IsNewFace(Known, FVector2D(0.0, 1.0)));
	TestTrue(TEXT("an end face is new"), IsNewFace(Known, FVector2D(1.0, 0.0)));
	TestFalse(TEXT("a zero normal is no face"), IsNewFace(Known, FVector2D::ZeroVector));
	return true;
}

RUN_CORNER_PEEK_TEST(FRunCornerPeekArrivalTest, "Arrival")
bool FRunCornerPeekArrivalTest::RunTest(const FString& Parameters)
{
	using namespace RunCornerPeekRules;
	TestTrue(TEXT("armed, the ordered corner slot: the peek starts at once"), IsPeekArrival(true, true, 0.f));
	TestTrue(TEXT("within the cover walk's arrival tolerance"), IsPeekArrival(true, true, 140.f));
	TestFalse(TEXT("beyond it"), IsPeekArrival(true, true, 160.f));
	TestFalse(TEXT("another wall (a later order took him elsewhere)"), IsPeekArrival(true, false, 0.f));
	TestFalse(TEXT("not armed (any later order disarms it)"), IsPeekArrival(false, true, 0.f));
	return true;
}

RUN_CORNER_PEEK_TEST(FRunCornerPeekGroupTest, "Group")
bool FRunCornerPeekGroupTest::RunTest(const FString& Parameters)
{
	using namespace RunCornerPeekRules;
	TestTrue(TEXT("solo mode, one selected: single"), DecideGroup(false, true, true, true, 0) == EGroup::Single);
	TestTrue(TEXT("solo mode, a box selection with him: the box selection"), DecideGroup(true, true, true, true, 0) == EGroup::BoxSelection);
	TestTrue(TEXT("a box selection, Codex.Squad.GroupCornerPeek 0: single"), DecideGroup(true, true, false, true, 0) == EGroup::Single);
	TestTrue(TEXT("a box selection without him: not his group"), DecideGroup(true, false, true, true, 0) == EGroup::Single);
	TestTrue(TEXT("formation with followers that join a cover order: formation"), DecideGroup(false, false, true, false, 2) == EGroup::Formation);
	TestTrue(TEXT("formation, nobody joins (all on their own orders): single"), DecideGroup(false, false, true, false, 0) == EGroup::Single);
	TestTrue(TEXT("solo mode never orders the formation"), DecideGroup(false, true, true, true, 3) == EGroup::Single);
	return true;
}

// The game designer's answers 2026-11-08 (#55): Q2 / Q3 change, Q1 / Q4 / Q5 / Q6 pinned as they are.
RUN_CORNER_PEEK_TEST(FRunCornerPeekDesignerAnswersTest, "DesignerAnswers")
bool FRunCornerPeekDesignerAnswersTest::RunTest(const FString& Parameters)
{
	using namespace RunCornerPeekRules;
	// Q3: the tactical pause plans it (run there and peek when the pause ends), real time runs it now.
	FClickFacts Pause;
	Pause.bPausePlanning = true;
	TestTrue(TEXT("Q3: in the tactical pause the click is a run-and-peek ..."), MayRunToPeek(Pause));
	TestTrue(TEXT("... planned for the pause's release"), DecideTiming(Pause) == ETiming::OnPauseRelease);
	TestTrue(TEXT("real time: now"), DecideTiming(FClickFacts()) == ETiming::Now);
	FClickFacts PauseTurnBased = Pause;
	PauseTurnBased.bTurnBased = true;
	TestFalse(TEXT("turn-based keeps its own rules (pause or not)"), MayRunToPeek(PauseTurnBased));
	// Q1 (kept): from a wall edge, the neighbouring cover's edge is the slide to it - no run-and-peek, no auto-peek after it.
	FClickFacts Switch;
	Switch.bCoverSwitchTarget = true;
	TestFalse(TEXT("Q1: a cover-switch target from a wall edge stays the switch"), MayRunToPeek(Switch));
	Switch.bPausePlanning = true;
	TestFalse(TEXT("Q1: ... in the pause too"), MayRunToPeek(Switch));
	// Q4 (kept): the normal run; a sprint only on a double-click on the same corner.
	TestFalse(TEXT("Q4: one click - the normal run"), IsSprintClick(-1.0, 0.3, 0.f, 8.f, 0.f));
	TestTrue(TEXT("Q4: the second click on the same corner within the double-click time - a sprint"), IsSprintClick(0.2, 0.3, 2.f, 8.f, 10.f));
	TestFalse(TEXT("Q4: a second click too late - the normal run"), IsSprintClick(0.5, 0.3, 2.f, 8.f, 10.f));
	TestFalse(TEXT("Q4: a second click on another corner - the normal run"), IsSprintClick(0.2, 0.3, 2.f, 8.f, 300.f));
	TestFalse(TEXT("Q4: a second click far off on screen - the normal run"), IsSprintClick(0.2, 0.3, 40.f, 8.f, 10.f));
	// Q5 (kept): a box selection - the corner man is the able one nearest the clicked end ALONG the wall (not the selection's leader).
	const FormationWallLeadership::FGroupCornerPeekPlan Plan = FormationWallLeadership::PlanGroupCornerPeek({ 300.0, 100.0, 520.0 }, { true, true, true }, 600.0, 560.0, 100.0,
		30.0);
	TestEqual(TEXT("Q5: the man nearest the end along the wall takes the corner"), Plan.Peeker, 2);
	const FormationWallLeadership::FGroupCornerPeekPlan Down = FormationWallLeadership::PlanGroupCornerPeek({ 300.0, 100.0, 520.0 }, { true, true, false }, 600.0, 560.0, 100.0,
		30.0);
	TestEqual(TEXT("Q5: ... the next nearest when he cannot"), Down.Peeker, 0);
	// Q6 (kept): covers narrower than 1 m (posts, columns) and barricades stay plain take cover.
	TestFalse(TEXT("Q6: a 60 cm column is no run-and-peek face"), IsFaceWideEnough(true, 30.f, true, 30.f));
	TestTrue(TEXT("Q6: 1 m is"), IsFaceWideEnough(true, 50.f, true, 50.f, 100.f));
	FClickFacts Barricade;
	Barricade.bHitObject = true;
	TestFalse(TEXT("Q6: a barricade (an object with its own click) stays take cover / its menu"), MayRunToPeek(Barricade));
	return true;
}

// Q2 (2026-11-08): in formation the corner man peeks once the LEADER is at the wall, not after the whole squad settled.
RUN_CORNER_PEEK_TEST(FRunCornerPeekFormationCarryTest, "FormationCarry")
bool FRunCornerPeekFormationCarryTest::RunTest(const FString& Parameters)
{
	using namespace RunCornerPeekRules;
	using FormationPeekCarry::EPeekCarryStep;
	FormationPeekCarry::FCarryProgressFacts Facts;
	Facts.bLeaderMoved = true;
	Facts.Seconds = 6.f;
	Facts.LeaderStillSeconds = 0.5f;
	Facts.bApproachDone = false; // a follower is still on his way
	TestTrue(TEXT("Q2: the leader at the wall, a follower still walking: the peek fires"), DecideFormationCarry(Facts, true, true) == EPeekCarryStep::Fire);
	TestTrue(TEXT("... the same for a group wall order's carried peek (bug_04 A): waits for the squad"), DecideFormationCarry(Facts, false, true) == EPeekCarryStep::Wait);
	TestTrue(TEXT("the leader stopped short of the wall: wait"), DecideFormationCarry(Facts, true, false) == EPeekCarryStep::Wait);
	FormationPeekCarry::FCarryProgressFacts Walking = Facts;
	Walking.LeaderStillSeconds = 0.1f;
	TestTrue(TEXT("the leader still walking in: wait"), DecideFormationCarry(Walking, true, true) == EPeekCarryStep::Wait);
	FormationPeekCarry::FCarryProgressFacts NotYet = Facts;
	NotYet.bLeaderMoved = false;
	NotYet.Seconds = 0.5f;
	TestTrue(TEXT("he has not set off yet (the stand-up clip): wait"), DecideFormationCarry(NotYet, true, true) == EPeekCarryStep::Wait);
	FormationPeekCarry::FCarryProgressFacts Other = Facts;
	Other.bLeaderOrderUnchanged = false;
	TestTrue(TEXT("the leader's next order: dropped"), DecideFormationCarry(Other, true, true) == EPeekCarryStep::Drop);
	FormationPeekCarry::FCarryProgressFacts Settled = Facts;
	Settled.bApproachDone = true;
	TestTrue(TEXT("everyone settled: fires either way"), DecideFormationCarry(Settled, false, false) == EPeekCarryStep::Fire);
	return true;
}

RUN_CORNER_PEEK_TEST(FRunCornerPeekPreTurnTest, "PreTurn")
bool FRunCornerPeekPreTurnTest::RunTest(const FString& Parameters)
{
	using namespace RunCornerPeekRules;
	// The game designer's video 2026-11-08: he must arrive already looking where the peek looks - the run's last metres turn to it.
	TestEqual(TEXT("the pre-turn starts 1.5 m out"), PreTurnCm, 150.f);
	TestTrue(TEXT("armed, 1.2 m out, running: turn to the peek facing"), ShouldPreTurn(true, 120.f, 375.f));
	TestTrue(TEXT("armed, at the edge of the stretch: turn"), ShouldPreTurn(true, 150.f, 375.f));
	TestFalse(TEXT("armed but 1.6 m out: not yet (the run faces its way)"), ShouldPreTurn(true, 160.f, 375.f));
	TestFalse(TEXT("not armed (an ordinary cover walk): the wall-facing approach as before"), ShouldPreTurn(false, 100.f, 375.f));
	TestFalse(TEXT("standing at the slot: the entry decides"), ShouldPreTurn(true, 20.f, 10.f));
	TestTrue(TEXT("walking in slowly still turns"), ShouldPreTurn(true, 80.f, 60.f));
	TestFalse(TEXT("a negative distance is no stretch"), ShouldPreTurn(true, -1.f, 375.f));
	TestTrue(TEXT("a custom stretch"), ShouldPreTurn(true, 190.f, 375.f, 200.f));
	return true;
}

RUN_CORNER_PEEK_TEST(FRunCornerPeekStanceYawBlendTest, "PeekStanceYawBlend")
bool FRunCornerPeekStanceYawBlendTest::RunTest(const FString& Parameters)
{
	using namespace RunCornerPeekRules;
	TestEqual(TEXT("pre-turned (5 deg left over): the short settle"), PeekStanceYawBlendSeconds(5.f), PeekStanceYawBlendMinSeconds, 1.0e-4f);
	TestEqual(TEXT("30 deg: still the short settle"), PeekStanceYawBlendSeconds(-30.f), PeekStanceYawBlendMinSeconds, 1.0e-4f);
	TestEqual(TEXT("a half turn (he faced a target on the run): the longest blend"), PeekStanceYawBlendSeconds(180.f), PeekStanceYawBlendMaxSeconds, 1.0e-4f);
	TestEqual(TEXT("105 deg: half way"), PeekStanceYawBlendSeconds(105.f), 0.5f * (PeekStanceYawBlendMinSeconds + PeekStanceYawBlendMaxSeconds), 1.0e-4f);
	TestEqual(TEXT("the sign does not matter"), PeekStanceYawBlendSeconds(-105.f), PeekStanceYawBlendSeconds(105.f), 1.0e-4f);
	TestEqual(TEXT("wrapped yaws: 350 deg = 10 deg"), PeekStanceYawBlendSeconds(350.f), PeekStanceYawBlendMinSeconds, 1.0e-4f);
	TestTrue(TEXT("never shorter than the minimum"), PeekStanceYawBlendSeconds(90.f, 0.3f, 0.1f) >= 0.3f);
	return true;
}

RUN_CORNER_PEEK_TEST(FRunCornerPeekHeldTurnTest, "HeldTurnSameWayAsBlend")
bool FRunCornerPeekHeldTurnTest::RunTest(const FString& Parameters)
{
	using namespace RunCornerPeekRules;
	// Measured 2026-11-08 (RunCornerPeekSmoke, the shoulder-line trace): the actor turns 170 deg on entry; the _R fire idle's blend turns the
	// pose -157 deg - held +170 the body spun ~340 deg, held -190 it stayed within 20 deg.
	TestEqual(TEXT("_R (his left): held +170, the blend -157 -> hold -190"), HeldTurnSameWayAsBlend(170.f, -157.f), -190.f, 1.0e-3f);
	TestEqual(TEXT("_L (his right): held -170, the blend -154 -> keep -170"), HeldTurnSameWayAsBlend(-170.f, -154.f), -170.f, 1.0e-3f);
	TestEqual(TEXT("a small held turn stays"), HeldTurnSameWayAsBlend(20.f, 15.f), 20.f, 1.0e-3f);
	TestEqual(TEXT("held -175, the blend +170 -> hold +185"), HeldTurnSameWayAsBlend(-175.f, 170.f), 185.f, 1.0e-3f);
	// The guard: the blend shows its way round in its first frames.
	TestTrue(TEXT("held +172, the pose turning -5 deg: flip"), HeldTurnGoesTheOtherWay(172.f, -5.f));
	TestFalse(TEXT("held +172, the pose turning +5 deg: right way round"), HeldTurnGoesTheOtherWay(172.f, 5.f));
	TestFalse(TEXT("not seen yet (1 deg)"), HeldTurnGoesTheOtherWay(172.f, -1.f));
	TestFalse(TEXT("nothing held: nothing to flip"), HeldTurnGoesTheOtherWay(0.f, -10.f));
	return true;
}

RUN_CORNER_PEEK_TEST(FRunCornerPeekHisFloorTest, "HisFloorOnly")
bool FRunCornerPeekHisFloorTest::RunTest(const FString& Parameters)
{
	using namespace RunCornerPeekRules;
	// LadderQueueSmoke (2026-11-08): the box selection's click on the roof a metre beyond the wall ladder's head (~4 m up) - a climb, never a peek.
	TestFalse(TEXT("a roof top 4 m above his feet: no run-and-peek"), IsClickAtHisLevel(true, 400.f));
	TestFalse(TEXT("a wall's top: no run-and-peek (a top surface)"), IsClickAtHisLevel(true, 120.f));
	TestFalse(TEXT("the ground at his level: a move"), IsClickAtHisLevel(true, 0.f));
	TestTrue(TEXT("the wall's side face at chest height beside its end: a run-and-peek"), IsClickAtHisLevel(false, 120.f));
	TestTrue(TEXT("the side face at knee height"), IsClickAtHisLevel(false, 30.f));
	TestTrue(TEXT("up to head height"), IsClickAtHisLevel(false, 200.f));
	TestFalse(TEXT("a face 3.5 m up (an upper floor's wall): no"), IsClickAtHisLevel(false, 350.f));
	TestFalse(TEXT("a face 2 m below (a lower floor): no"), IsClickAtHisLevel(false, -200.f));
	TestTrue(TEXT("the corner slot on his floor (a 40 cm step): yes"), IsCornerOnHisFloor(40.f));
	TestFalse(TEXT("the corner slot on the roof (4 m up): no"), IsCornerOnHisFloor(400.f));
	TestFalse(TEXT("the corner slot a floor below: no"), IsCornerOnHisFloor(-300.f));
	return true;
}

#undef RUN_CORNER_PEEK_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
