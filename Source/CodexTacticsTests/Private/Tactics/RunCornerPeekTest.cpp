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

// --- Arrives already turned (the game designer's video Bug_2 2026-11-08: "another small extra turn" on the peek entry) ---

RUN_CORNER_PEEK_TEST(FRunCornerPeekPreTurnPlanTest, "ArrivesTurned.PreTurnPlan")
bool FRunCornerPeekPreTurnPlanTest::RunTest(const FString& Parameters)
{
	using namespace RunCornerPeekRules;
	// Where it starts: ~0.6 s of his run before the entry (which comes the run-in glide short of the slot), at least the first fix's 1.5 m.
	TestEqual(TEXT("a run (450 cm/s, 90 cm glide): 0.6 s = 270 cm before the entry"), PreTurnStartCm(450.f, 90.f), 360.f, 1.0e-3f);
	TestEqual(TEXT("a walk (150 cm/s): never shorter than 1.5 m"), PreTurnStartCm(150.f, 30.f), 180.f, 1.0e-3f);
	TestEqual(TEXT("a very fast run: at most 4 m"), PreTurnStartCm(1000.f, 120.f), 520.f, 1.0e-3f);
	TestEqual(TEXT("standing: the minimum"), PreTurnStartCm(0.f, 0.f), PreTurnMinCm, 1.0e-3f);

	// The planned turn: a run at 450 cm/s from 270 cm before the entry, 40 deg to turn, 60 fps - an even turn done 0.1 s before the entry.
	const float Dt = 1.f / 60.f;
	const float Speed = 450.f;
	float Yaw = 0.f;
	float MaxStep = 0.f;
	bool bMonotonic = true;
	bool bNeverPast = true;
	float YawAtFinish = -1.f;
	for (float ToEntry = 270.f; ToEntry > 0.f; ToEntry -= Speed * Dt)
	{
		const float Next = PreTurnStepYaw(Yaw, 40.f, ToEntry, Speed, Dt);
		const float Step = FRotator::NormalizeAxis(Next - Yaw);
		bMonotonic &= Step >= -1.0e-4f;
		bNeverPast &= Next <= 40.f + 1.0e-3f;
		MaxStep = FMath::Max(MaxStep, Step);
		Yaw = Next;
		if (YawAtFinish < 0.f && ToEntry / Speed <= PreTurnFinishEarlySeconds)
		{
			YawAtFinish = Yaw;
		}
	}
	TestTrue(TEXT("one way only"), bMonotonic);
	TestTrue(TEXT("never past the target"), bNeverPast);
	TestEqual(TEXT("done by 0.1 s before the entry"), YawAtFinish, 40.f, 1.0e-2f);
	// Even: 40 deg over the 0.5 s to the planned end = 1.33 deg a frame (the first fix's turn rate snapped ~25 deg a frame).
	TestTrue(FString::Printf(TEXT("an even rate, no snap (max %.2f deg a frame)"), MaxStep), MaxStep <= 40.f * Dt / 0.5f * 1.05f);
	TestEqual(TEXT("the short way round the wrap: 170 -> -170 turns +20"), static_cast<float>(FRotator::NormalizeAxis(PreTurnStepYaw(170.f, -170.f, 0.f, Speed, Dt) - 170.f)), 20.f, 1.0e-3f);
	TestEqual(TEXT("no time step: no turn"), PreTurnStepYaw(10.f, 50.f, 200.f, Speed, 0.f), 10.f, 1.0e-4f);
	TestEqual(TEXT("past the planned end (or no speed): the rest at once"), PreTurnStepYaw(10.f, 50.f, 20.f, Speed, Dt), 50.f, 1.0e-3f);
	return true;
}

RUN_CORNER_PEEK_TEST(FRunCornerPeekBodyFacingTest, "ArrivesTurned.BodyFacing")
bool FRunCornerPeekBodyFacingTest::RunTest(const FString& Parameters)
{
	using namespace RunCornerPeekRules;
	// The drawn body: the normal of the left -> right line (forward +X, right +Y - the smoke's shoulder-line metric).
	TestEqual(TEXT("right shoulder at +Y: facing +X (0)"), BodyLineYaw(FVector2D(0.f, -20.f), FVector2D(0.f, 20.f)), 0.f, 1.0e-3f);
	TestEqual(TEXT("right shoulder at -X: facing +Y (90)"), BodyLineYaw(FVector2D(20.f, 0.f), FVector2D(-20.f, 0.f)), 90.f, 1.0e-3f);
	TestEqual(TEXT("right shoulder at +X: facing -Y (-90)"), BodyLineYaw(FVector2D(-20.f, 0.f), FVector2D(20.f, 0.f)), -90.f, 1.0e-3f);
	TestEqual(TEXT("shoulders only"), MixBodyYaw(30.f, 10.f, 1.f), 30.f, 1.0e-3f);
	TestEqual(TEXT("hips only"), MixBodyYaw(30.f, 10.f, 0.f), 10.f, 1.0e-3f);
	TestEqual(TEXT("half and half, the short way round the wrap"), FMath::Abs(MixBodyYaw(170.f, -170.f, 0.5f)), 180.f, 1.0e-3f);
	// The pre-turn's target: the actor yaw that draws the run's body where the fire idle will draw it. Measured 2026-11-08 (A, the _R fire
	// idle): the run's shoulders stood 39 deg off his yaw, the fire idle's 59 off the corner aim - the first fix's actor at the corner aim left 20.
	const float FacingYaw = -100.f; // his cover facing at the slot (back to the wall)
	const float FinalCs = 150.f;    // the fire idle's drawn body, component space
	const float RunCs = 130.f;      // the run pose's, component space
	const float MeshBase = -90.f;   // the mesh's yaw on the capsule (cancels out)
	const float Actor = BodyTargetActorYaw(FacingYaw, FinalCs, RunCs);
	TestEqual(TEXT("the actor 20 deg further round"), Actor, -80.f, 1.0e-3f);
	TestEqual(TEXT("drawn: the run's last frame = the fire idle's first"), static_cast<float>(FRotator::NormalizeAxis(Actor + MeshBase + RunCs)),
		static_cast<float>(FRotator::NormalizeAxis(FacingYaw + MeshBase + FinalCs)), 1.0e-3f);
	TestEqual(TEXT("wrapped"), BodyTargetActorYaw(170.f, 20.f, -10.f), -160.f, 1.0e-3f);
	return true;
}

RUN_CORNER_PEEK_TEST(FRunCornerPeekYawLockTest, "ArrivesTurned.YawLock")
bool FRunCornerPeekYawLockTest::RunTest(const FString& Parameters)
{
	using namespace RunCornerPeekRules;
	TestEqual(TEXT("the path starts at the run's last frame"), LockTargetYaw(10.f, 30.f, 0.f, 0.3f), 10.f, 1.0e-3f);
	TestEqual(TEXT("half way"), LockTargetYaw(10.f, 30.f, 0.15f, 0.3f), 20.f, 1.0e-3f);
	TestEqual(TEXT("ends at the fire idle's own"), LockTargetYaw(10.f, 30.f, 0.3f, 0.3f), 30.f, 1.0e-3f);
	TestEqual(TEXT("and stays there"), LockTargetYaw(10.f, 30.f, 1.f, 0.3f), 30.f, 1.0e-3f);
	TestEqual(TEXT("the short way round the wrap"), FMath::Abs(LockTargetYaw(170.f, -170.f, 0.15f, 0.3f)), 180.f, 1.0e-3f);
	TestEqual(TEXT("full strength over the path"), LockAlpha(0.3f, 0.3f), 1.f, 1.0e-4f);
	TestEqual(TEXT("half way through the fade"), LockAlpha(0.3f + 0.5f * LockFadeSeconds, 0.3f), 0.5f, 1.0e-4f);
	TestEqual(TEXT("let go"), LockAlpha(1.f, 0.3f), 0.f, 1.0e-4f);
	TestEqual(TEXT("the pose turned 157 deg off the target: counter-turn it"), LockCorrectionDeg(20.f, 177.f, 1.f), -157.f, 1.0e-3f);
	TestEqual(TEXT("past 180 the same rotation the short way"), LockCorrectionDeg(20.f, -150.f, 1.f), 170.f, 1.0e-3f);
	TestEqual(TEXT("fading"), LockCorrectionDeg(20.f, 24.f, 0.5f), -2.f, 1.0e-3f);

	// Why a lock on the evaluated pose (the smoke's A trace 2026-11-08: 39 -> 47, a plateau, -> 59): the pose blend turns the pelvis
	// ~160 deg by per-bone nlerp, not linearly in the weight - a held yaw released linearly with the weight (the first fix) leaves a
	// wobble even when its total matches the blend's turn exactly; the lock follows the pose itself.
	const float BlendTurn = 157.f;
	const FQuat Run = FQuat::Identity;
	const FQuat Idle(FVector::UpVector, FMath::DegreesToRadians(BlendTurn));
	const auto PoseYaw = [&Run, &Idle](float Weight)
	{
		FQuat Blend = Run * (1.f - Weight) + Idle * Weight; // the slot blend: weighted quaternion sum, normalised (nlerp)
		Blend.Normalize();
		const FVector Forward = Blend.RotateVector(FVector::ForwardVector);
		return FMath::RadiansToDegrees(FMath::Atan2(Forward.Y, Forward.X));
	};
	float HeldWobble = 0.f;
	float LockError = 0.f;
	for (int32 Frame = 0; Frame <= 18; ++Frame)
	{
		const float Weight = Frame / 18.f;
		const float Pose = PoseYaw(Weight);
		// The first fix: the mesh holds -BlendTurn x (1 - weight) (the actor turned back to the wall by the same amount).
		HeldWobble = FMath::Max(HeldWobble, FMath::Abs(FRotator::NormalizeAxis(Pose - BlendTurn * Weight)));
		// The lock: the drawn body held at the run's last frame (0 here) - the pose plus the lock's correction.
		const float Drawn = FRotator::NormalizeAxis(Pose + LockCorrectionDeg(0.f, Pose, 1.f));
		LockError = FMath::Max(LockError, FMath::Abs(Drawn));
	}
	TestTrue(FString::Printf(TEXT("the first fix's linear release wobbles (%.1f deg mid-blend)"), HeldWobble), HeldWobble > 4.f);
	TestTrue(FString::Printf(TEXT("the lock holds the drawn body exactly (%.3f deg)"), LockError), LockError < 0.01f);
	return true;
}

RUN_CORNER_PEEK_TEST(FRunCornerPeekFaceUnderCursorTest, "FaceUnderCursor")
bool FRunCornerPeekFaceUnderCursorTest::RunTest(const FString& Parameters)
{
	using namespace RunCornerPeekRules;
	// The game designer's PIE 2026-11-08: a free-standing 1.5 m block, he in cover at another wall to its north - "I want to press to the
	// face I point at; the ghost appears only on one side". The four faces, each with a corner at the click (the probes found them all).
	const auto MakeFace = [](double X, double Y)
	{
		FFace Face;
		Face.Normal = FVector2D(X, Y);
		Face.Point = FVector2D(X * 75.0, Y * 75.0);
		return Face;
	};
	const TArray<FFace> Faces{ MakeFace(0.0, 1.0), MakeFace(1.0, 0.0), MakeFace(0.0, -1.0), MakeFace(-1.0, 0.0) }; // north (his), east, south, west
	const FVector2D Him(0.0, 700.0);
	const FVector2D TowardCamera = FVector2D(-0.3, -1.0).GetSafeNormal(); // the camera south of the block, looking north
	TestEqual(TEXT("decision 3 (before): the face looking at him, whichever was clicked"), ChooseNearFace(Faces, Him), 0);
	TestEqual(TEXT("the north face under the cursor: north"), ChooseClickedFace(Faces, FVector2D(0.0, 1.0), TowardCamera), 0);
	TestEqual(TEXT("the east face: east"), ChooseClickedFace(Faces, FVector2D(1.0, 0.0), TowardCamera), 1);
	TestEqual(TEXT("the far (south) face: south - he runs round to it"), ChooseClickedFace(Faces, FVector2D(0.0, -1.0), TowardCamera), 2);
	TestEqual(TEXT("the west face: west"), ChooseClickedFace(Faces, FVector2D(-1.0, 0.0), TowardCamera), 3);
	TestEqual(TEXT("a slightly tilted hit normal still picks its face"), ChooseClickedFace(Faces, FVector2D(0.9, 0.2), TowardCamera), 1);
	// Exactly on the south-west edge (an averaged normal): the face facing the camera more - south here.
	TestEqual(TEXT("an edge hit: the camera-facing face"), ChooseClickedFace(Faces, FVector2D(-1.0, -1.0), TowardCamera), 2);
	TestEqual(TEXT("an edge hit, the camera west: the west face"), ChooseClickedFace(Faces, FVector2D(-1.0, -1.0), FVector2D(-1.0, -0.2)), 3);
	// The hit face is no run-and-peek face of its own (a wall's narrow end face east of two long faces): none -> decision 3's fallback.
	const TArray<FFace> LongFaces{ MakeFace(0.0, 1.0), MakeFace(0.0, -1.0) };
	TestEqual(TEXT("a click on the narrow end face: no clicked face (the faces round it decide)"), ChooseClickedFace(LongFaces, FVector2D(1.0, 0.0), TowardCamera),
		static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("no faces: none"), ChooseClickedFace(TArray<FFace>(), FVector2D(1.0, 0.0), TowardCamera), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("a top hit (no planar normal): none"), ChooseClickedFace(Faces, FVector2D::ZeroVector, TowardCamera), static_cast<int32>(INDEX_NONE));

	// Q1 narrowed: from a wall edge (his wall looks south, -Y) a switch target stays the switch only on the protected-side face at its near end.
	const FVector2D OwnWall(0.0, -1.0);
	TestTrue(TEXT("the protected-side (south) face, its end nearer to him: the switch (Q1)"), KeepsCoverSwitch(true, FVector2D(0.0, -1.0), OwnWall, 300.f, 420.f));
	TestFalse(TEXT("the same face's far end: the run-and-peek there"), KeepsCoverSwitch(true, FVector2D(0.0, -1.0), OwnWall, 420.f, 300.f));
	TestFalse(TEXT("the face towards him (north): the run-and-peek"), KeepsCoverSwitch(true, FVector2D(0.0, 1.0), OwnWall, 200.f, 300.f));
	TestFalse(TEXT("a side face: the run-and-peek"), KeepsCoverSwitch(true, FVector2D(1.0, 0.0), OwnWall, 200.f, 300.f));
	TestFalse(TEXT("no switch in range: never"), KeepsCoverSwitch(false, FVector2D(0.0, -1.0), OwnWall, 200.f, 300.f));
	return true;
}

RUN_CORNER_PEEK_TEST(FRunCornerPeekLowCoverTest, "LowCoverIsNoRunPeek")
bool FRunCornerPeekLowCoverTest::RunTest(const FString& Parameters)
{
	using namespace RunCornerPeekRules;
	// The game designer's PIE 2026-11-08 (L_Gen_Depot_1, low cubes): the corner man ran to a low cube's corner, the others "stacked behind" a
	// cube with no room and stood in the open. Only cover tall enough to peek round is a run-and-peek.
	TestFalse(TEXT("a low cube's end (fire-over cover): no run-and-peek - a plain cover click"), IsRunPeekCover(ECoverHeight::LowCover));
	TestTrue(TEXT("a standing-height block / wall's end: the run-and-peek"), IsRunPeekCover(ECoverHeight::HighCover));
	TestFalse(TEXT("no cover: no"), IsRunPeekCover(ECoverHeight::None));
	// The stack behind the corner man: a place with no slot on that object's face (a block too short) is no cover - a nearby cover instead.
	TestFalse(TEXT("no slot on the face behind him: not covered"), IsStackPlaceCovered(false, 120.f));
	TestFalse(TEXT("the slot found is the corner man's own spot: not covered"), IsStackPlaceCovered(true, 20.f));
	TestTrue(TEXT("a slot a stack spacing behind him: covered"), IsStackPlaceCovered(true, 100.f));
	return true;
}

RUN_CORNER_PEEK_TEST(FRunCornerPeekOneWayTest, "ArrivesTurned.OneWay")
bool FRunCornerPeekOneWayTest::RunTest(const FString& Parameters)
{
	using namespace RunCornerPeekRules;
	// RunCornerPeekSmoke D with the face rule (round the wall's end to the clicked face): the strafe shifting under the turn pulled the
	// planned turn 2 deg back at its end - the pre-turn never steps back.
	int32 Sign = 0;
	TestEqual(TEXT("a tiny first step sets no way yet"), OneWayStepYaw(10.f, 10.02f, Sign), 10.02f, 1.0e-4f);
	TestEqual(TEXT("... no way"), Sign, 0);
	TestEqual(TEXT("the first real step sets the way (down)"), OneWayStepYaw(10.f, 8.f, Sign), 8.f, 1.0e-4f);
	TestEqual(TEXT("... down"), Sign, -1);
	TestEqual(TEXT("on down"), OneWayStepYaw(8.f, 6.5f, Sign), 6.5f, 1.0e-4f);
	TestEqual(TEXT("a step back up is not taken: the yaw holds"), OneWayStepYaw(6.5f, 7.5f, Sign), 6.5f, 1.0e-4f);
	int32 WrapSign = 1;
	TestEqual(TEXT("up across the wrap (179 -> -179 is +2) goes on"), OneWayStepYaw(179.f, -179.f, WrapSign), -179.f, 1.0e-4f);
	return true;
}

#undef RUN_CORNER_PEEK_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
