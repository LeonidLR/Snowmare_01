#include "Misc/AutomationTest.h"
#include "Ladders/LadderRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// docs/design/LADDERS.md (UE-only system, no Godot reference): geometry, rung math, refusals, the climb state table.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLadderGeometryTest, "CodexTactics.Ladder.Rules.Geometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLadderGeometryTest::RunTest(const FString&)
{
	using namespace LadderRules;
	const FClipContract Clip;
	TestEqual(TEXT("255 cm -> 7 rungs (the shortest climbable ladder)"), RungCountForHeight(255.f, 36.4f), 7);
	TestEqual(TEXT("4 m -> 11 rungs"), RungCountForHeight(400.f, 36.4f), 11);
	TestEqual(TEXT("a tiny ladder still has one rung"), RungCountForHeight(5.f, 36.4f), 1);
	TestEqual(TEXT("a bad pitch draws nothing"), RungCountForHeight(255.f, 5.f), 0);
	TestTrue(TEXT("top floor = rungs x pitch (11 -> 400.4)"), FMath::IsNearlyEqual(TopZ(11, 36.4f), 400.4f, 0.01f));
	TestTrue(TEXT("7 rungs with a top exit are climbable"), IsClimbable(7, true));
	TestFalse(TEXT("6 rungs = decoration (user decision 2026-10-22)"), IsClimbable(6, true));
	TestFalse(TEXT("no top exit = decoration"), IsClimbable(11, false));

	TestTrue(TEXT("rung 3 centre"), RungLocal(3, 36.4f).Equals(FVector(0.f, 0.f, 109.2f), 0.01f));
	TestTrue(TEXT("hang line 48.2 cm in front of the rungs, 7 cm aside"), HangLocal(Clip, 10.f).Equals(FVector(48.2f, 7.f, 10.f), 0.01f));
	TestTrue(TEXT("bottom link point further out on the floor"), BottomLinkLocal(Clip).Equals(FVector(68.2f, 7.f, 0.f), 0.01f));
	TestTrue(TEXT("top link point on the platform behind the rungs"), TopLinkLocal(Clip, 11).Equals(FVector(-49.1f, 7.f, 400.4f), 0.01f));
	TestTrue(TEXT("ClimbOut ends on the platform"), TopExitLocal(Clip, 8).Equals(FVector(-40.f, 7.f, 291.2f), 0.01f));
	TestTrue(TEXT("the standing capsule (r 40) never touches the rungs"), Clip.HangStandoff >= 40.f + 2.f + 6.f);

	TestEqual(TEXT("rail tiling: 400.4 / 100 -> 5 + 1 overshoot"), RailSegmentCount(400.4f, 100.f, 1, 36.4f), 6);
	TestEqual(TEXT("rail tiling exact fit: 300 / 100 -> 3"), RailSegmentCount(300.f, 100.f, 0, 36.4f), 3);
	TestEqual(TEXT("guard: segment < 1 cm"), RailSegmentCount(300.f, 0.5f, 1, 36.4f), INDEX_NONE);
	TestEqual(TEXT("guard: pitch < 10 cm"), RailSegmentCount(300.f, 100.f, 1, 5.f), INDEX_NONE);
	const TArray<float> Brackets = BracketHeights(3, 36.4f, 400.4f);
	TestTrue(TEXT("3 brackets from the first rung to the top"), Brackets.Num() == 3 && FMath::IsNearlyEqual(Brackets[0], 36.4f)
		&& FMath::IsNearlyEqual(Brackets[2], 400.4f) && FMath::IsNearlyEqual(Brackets[1], 218.4f));
	TestEqual(TEXT("free-standing: no brackets"), BracketHeights(0, 36.4f, 400.4f).Num(), 0);

	TestTrue(TEXT("climber faces the wall: ladder yaw + 180"), FMath::IsNearlyEqual(ClimberYaw(30.f), -150.f));
	TestTrue(TEXT("climber yaw normalised"), FMath::IsNearlyEqual(ClimberYaw(-170.f), 10.f));
	TestFalse(TEXT("facing the drop within 10 deg: no pre-turn"), NeedsPreTurn(95.f, 90.f));
	TestTrue(TEXT("facing the wall: pre-turn to the drop"), NeedsPreTurn(-90.f, 90.f));
	TestFalse(TEXT("across the +-180 seam"), NeedsPreTurn(-175.f, 178.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLadderRungMathTest, "CodexTactics.Ladder.Rules.RungMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLadderRungMathTest::RunTest(const FString&)
{
	using namespace LadderRules;
	const FClipContract Clip;
	constexpr float L = 1.767f;
	TestTrue(TEXT("ClimbOut starts 5 rungs below the platform (re-measured 2026-10-23: the clip steps onto a top 182 cm up)"), ClimbOutStart(Clip, 11) == FLoopPos{ 6, 0.f });
	TestTrue(TEXT("IntoFromTop lands 5 rungs below the platform"), IntoFromTopEnd(Clip, 11) == FLoopPos{ 6, 0.f });
	TestTrue(TEXT("even target: start at the floor, loop start"), BottomPos(4, L) == FLoopPos{ 0, 0.f });
	TestTrue(TEXT("odd target: the one-rung remainder is the first half of the loop"), BottomPos(1, L) == FLoopPos{ -1, L * 0.5f });

	// Up 4 rungs = two loops: the boundary snaps exactly on the rungs.
	FLoopPos Pos{ 0, 0.f };
	const FLoopPos Target{ 4, 0.f };
	TestFalse(TEXT("one loop is not the target"), Advance(Pos, L, 1, Target, L));
	TestTrue(TEXT("one loop = two rungs"), Pos == FLoopPos{ 2, 0.f });
	TestTrue(TEXT("height snapped to 2 x pitch at the boundary"), FMath::IsNearlyEqual(HeightAt(Pos, 0.f, 36.4f), 72.8f, 0.001f));
	TestTrue(TEXT("overshooting stops exactly on the target"), Advance(Pos, L * 1.7f, 1, Target, L) && Pos == Target);

	// Odd ladder: 8 rungs, target rung 1 from the half loop start: half a loop.
	FLoopPos Odd = BottomPos(1, L);
	TestFalse(TEXT("odd: not yet"), Advance(Odd, L * 0.25f, 1, FLoopPos{ 1, 0.f }, L));
	TestTrue(TEXT("odd: half a loop reaches rung 1"), Advance(Odd, L * 0.3f, 1, FLoopPos{ 1, 0.f }, L) && Odd == FLoopPos{ 1, 0.f });
	TestTrue(TEXT("odd: start height ~ the floor (rise share 0.5)"), FMath::IsNearlyEqual(HeightAt(BottomPos(1, L), 0.5f, 36.4f), 0.f, 0.01f));

	// Down from IntoFromTop (rung 6) to the floor: three loops reversed, wrapping each boundary.
	FLoopPos Down{ 6, 0.f };
	TestFalse(TEXT("down: first step wraps to the loop below"), Advance(Down, 0.1f, -1, FLoopPos{ 0, 0.f }, L));
	TestTrue(TEXT("down: the wrap moved two rungs down"), Down.Rung == 4 && FMath::IsNearlyEqual(Down.Time, L - 0.1f, 1e-3f));
	TestTrue(TEXT("down: reaches the floor exactly"), Advance(Down, 10.f, -1, FLoopPos{ 0, 0.f }, L) && Down == FLoopPos{ 0, 0.f });
	FLoopPos DownOdd{ 3, 0.f };
	TestTrue(TEXT("down odd: ends on the half-loop bottom pose"), Advance(DownOdd, 10.f, -1, BottomPos(1, L), L) && DownOdd == BottomPos(1, L));

	TestTrue(TEXT("nearest end: low on the ladder -> bottom"), !NearestEndIsTop(2, 11));
	TestTrue(TEXT("nearest end: high on the ladder -> top"), NearestEndIsTop(6, 11));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLadderGripTest, "CodexTactics.Ladder.Rules.LimbGrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLadderGripTest::RunTest(const FString&)
{
	using namespace LadderRules;
	// The loop start pose (LADDERS.md 6.2): left foot k, right foot k+1, right hand k+4, left hand k+5, all gripping.
	TestTrue(TEXT("start: left hand on k+5"), LimbGrip(ELadderLimb::HandL, 0.f).RungOffset == 5 && LimbGrip(ELadderLimb::HandL, 0.f).Alpha == 1.f);
	TestTrue(TEXT("start: right hand on k+4"), LimbGrip(ELadderLimb::HandR, 0.f).RungOffset == 4 && LimbGrip(ELadderLimb::HandR, 0.f).Alpha == 1.f);
	TestTrue(TEXT("start: left foot on k"), LimbGrip(ELadderLimb::FootL, 0.f).RungOffset == 0);
	TestTrue(TEXT("start: right foot on k+1"), LimbGrip(ELadderLimb::FootR, 0.f).RungOffset == 1);
	// The end pose: every limb two rungs higher.
	TestTrue(TEXT("end: left hand on k+7"), LimbGrip(ELadderLimb::HandL, 1.f).RungOffset == 7 && LimbGrip(ELadderLimb::HandL, 1.f).Alpha == 1.f);
	TestTrue(TEXT("end: right foot on k+3"), LimbGrip(ELadderLimb::FootR, 1.f).RungOffset == 3 && LimbGrip(ELadderLimb::FootR, 1.f).Alpha == 1.f);
	// Mid-move: free (no IK pulling a moving hand).
	TestTrue(TEXT("right hand moving at frame 14"), LimbGrip(ELadderLimb::HandR, 14.f / 53.f).Alpha == 0.f);
	TestTrue(TEXT("left hand moving at frame 39"), LimbGrip(ELadderLimb::HandL, 39.f / 53.f).Alpha == 0.f);
	TestTrue(TEXT("fade: alpha between 0 and 1 at the release"), LimbGrip(ELadderLimb::HandL, 31.f / 53.f).Alpha > 0.f && LimbGrip(ELadderLimb::HandL, 31.f / 53.f).Alpha < 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLadderRefusalTest, "CodexTactics.Ladder.Rules.Refusals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLadderRefusalTest::RunTest(const FString&)
{
	using namespace LadderRules;
	FEntryContext Go;
	TestTrue(TEXT("free to climb"), CanStartClimb(Go) == ELadderRefusal::None);
	auto With = [](TFunctionRef<void(FEntryContext&)> Set) { FEntryContext Context; Set(Context); return CanStartClimb(Context); };
	TestTrue(TEXT("turn-based: forbidden"), With([](FEntryContext& C) { C.bTurnBased = true; }) == ELadderRefusal::TurnBased);
	TestTrue(TEXT("carrying: refused"), With([](FEntryContext& C) { C.bCarrying = true; }) == ELadderRefusal::Carrying);
	TestTrue(TEXT("mounted: refused"), With([](FEntryContext& C) { C.bMounted = true; }) == ELadderRefusal::Mounted);
	TestTrue(TEXT("knocked down: refused"), With([](FEntryContext& C) { C.bKnockedDown = true; }) == ELadderRefusal::KnockedDown);
	TestTrue(TEXT("dead: refused"), With([](FEntryContext& C) { C.bDead = true; }) == ELadderRefusal::Dead);
	TestTrue(TEXT("< 7 rungs: decoration"), With([](FEntryContext& C) { C.bClimbable = false; }) == ELadderRefusal::NotClimbable);
	TestTrue(TEXT("no clips for the body"), With([](FEntryContext& C) { C.bHasClips = false; }) == ELadderRefusal::NoClips);
	TestTrue(TEXT("leased: he queues"), With([](FEntryContext& C) { C.bLeasedByOther = true; }) == ELadderRefusal::Leased);
	TestTrue(TEXT("prone / crouched: he stands up first"), With([](FEntryContext& C) { C.bStanding = false; }) == ELadderRefusal::MustStand);

	// Enemies (LADDERS.md 7.5): the climbing humans with clips on a ladder open to enemies; mutants never.
	TestTrue(TEXT("a zombified PMC / marksman with clips on an open ladder climbs"), EnemyMayClimb(true, true, true));
	TestFalse(TEXT("a mutant never climbs"), EnemyMayClimb(false, true, true));
	TestFalse(TEXT("a climbing class without clips for its body paths around"), EnemyMayClimb(true, false, true));
	TestFalse(TEXT("a squad-only ladder (bAllowEnemies off)"), EnemyMayClimb(true, true, false));

	// Turn-based (LADDERS.md 7.6): a climber joins on the roof where the ladder left him, on the ground floor on a cell.
	TestTrue(TEXT("off a 2.9 m deck: joins up there"), JoinsOnUpperFloor(291.f, 0.f));
	TestFalse(TEXT("off at the foot: takes a cell"), JoinsOnUpperFloor(2.f, 0.f));
	TestFalse(TEXT("a step of 40 cm is the same floor"), JoinsOnUpperFloor(140.f, 100.f));

	const FFallDamage Rule;
	TestTrue(TEXT("a fall under 1 m: no damage"), FallDamage(90.f, Rule) == 0.f);
	TestTrue(TEXT("a full 7-rung ladder (254.8 cm): ~15.5 HP"), FMath::IsNearlyEqual(FallDamage(254.8f, Rule), 15.48f, 0.01f));
	TestTrue(TEXT("4 m: 30 HP"), FMath::IsNearlyEqual(FallDamage(400.f, Rule), 30.f, 0.01f));
	TestTrue(TEXT("capped at 40"), FallDamage(1000.f, Rule) == 40.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLadderReverseTest, "CodexTactics.Ladder.Climb.Reverse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLadderReverseTest::RunTest(const FString&)
{
	// A new order behind him mid-climb (2026-10-23): the loop turns round at the next rung.
	using namespace LadderRules;
	constexpr float L = 1.8f;
	TestTrue(TEXT("up from early in the loop: its half"), NextRungBoundary(FLoopPos{ 2, 0.3f }, 1, L) == FLoopPos{ 2, 0.9f });
	TestTrue(TEXT("up from late in the loop: the next loop's start (two rungs on)"), NextRungBoundary(FLoopPos{ 2, 1.2f }, 1, L) == FLoopPos{ 4, 0.f });
	TestTrue(TEXT("up from the half itself: the next start"), NextRungBoundary(FLoopPos{ 2, 0.9f }, 1, L) == FLoopPos{ 4, 0.f });
	TestTrue(TEXT("down from late: the half"), NextRungBoundary(FLoopPos{ 2, 1.2f }, -1, L) == FLoopPos{ 2, 0.9f });
	TestTrue(TEXT("down from early: the loop start"), NextRungBoundary(FLoopPos{ 2, 0.3f }, -1, L) == FLoopPos{ 2, 0.f });
	TestTrue(TEXT("down from a loop start: the half below"), NextRungBoundary(FLoopPos{ 2, 0.f }, -1, L) == FLoopPos{ 0, 0.9f });
	TestTrue(TEXT("beyond going up"), IsBeyond(FLoopPos{ 4, 0.f }, FLoopPos{ 2, 0.9f }, 1, L) && !IsBeyond(FLoopPos{ 2, 0.5f }, FLoopPos{ 2, 0.9f }, 1, L));
	TestTrue(TEXT("beyond going down"), IsBeyond(FLoopPos{ 0, 0.9f }, FLoopPos{ 2, 0.f }, -1, L) && !IsBeyond(FLoopPos{ 2, 0.5f }, FLoopPos{ 2, 0.f }, -1, L));
	TestTrue(TEXT("the table: up turns down"), Transition(ELadderClimbState::ClimbUp, ELadderClimbEvent::Reverse) == ELadderClimbState::ClimbDown);
	TestTrue(TEXT("... down turns up"), Transition(ELadderClimbState::ClimbDown, ELadderClimbEvent::Reverse) == ELadderClimbState::ClimbUp);
	TestTrue(TEXT("... nothing to turn on a transition clip"), Transition(ELadderClimbState::ExitTop, ELadderClimbEvent::Reverse) == ELadderClimbState::ExitTop);
	TestTrue(TEXT("the shorter way serves: a goal on the ground"), !PreferTopEnd(900.f, 300.f, true));
	TestTrue(TEXT("... a goal on the roof"), PreferTopEnd(150.f, 1200.f, false));
	TestTrue(TEXT("unreachable from the bottom: the top"), PreferTopEnd(500.f, -1.f, false));
	TestTrue(TEXT("a tie keeps his way"), PreferTopEnd(400.f, 400.f, true) && !PreferTopEnd(400.f, 400.f, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLadderClimbTableTest, "CodexTactics.Ladder.Climb.StateTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLadderClimbTableTest::RunTest(const FString&)
{
	using namespace LadderRules;
	using S = ELadderClimbState;
	using E = ELadderClimbEvent;
	// Up: None -> AlignBottom -> ClimbUp -> ExitTop -> None.
	TestTrue(TEXT("bottom link -> align"), Transition(S::None, E::LinkReachedBottom) == S::AlignBottom);
	TestTrue(TEXT("aligned -> climb up"), Transition(S::AlignBottom, E::AlignDone) == S::ClimbUp);
	TestTrue(TEXT("top reached -> climb out"), Transition(S::ClimbUp, E::TargetReached) == S::ExitTop);
	TestTrue(TEXT("out -> none"), Transition(S::ExitTop, E::ExitDone) == S::None);
	// Down: None -> AlignTop -> EnterTop -> ClimbDown -> ExitBottom -> None.
	TestTrue(TEXT("top link -> align top"), Transition(S::None, E::LinkReachedTop) == S::AlignTop);
	TestTrue(TEXT("aligned at the top -> IntoFromTop"), Transition(S::AlignTop, E::AlignDone) == S::EnterTop);
	TestTrue(TEXT("IntoFromTop done -> climb down"), Transition(S::EnterTop, E::EnterDone) == S::ClimbDown);
	TestTrue(TEXT("bottom reached -> exit bottom"), Transition(S::ClimbDown, E::TargetReached) == S::ExitBottom);
	TestTrue(TEXT("exit bottom -> none"), Transition(S::ExitBottom, E::ExitDone) == S::None);
	// Queue.
	TestTrue(TEXT("leased -> queue"), Transition(S::None, E::MustWait) == S::Queue);
	TestTrue(TEXT("queue -> align when free"), Transition(S::Queue, E::LinkReachedBottom) == S::AlignBottom);
	// The one interrupt path.
	for (const S From : { S::Queue, S::AlignBottom, S::AlignTop, S::EnterTop, S::ClimbUp, S::ClimbDown, S::ExitTop, S::ExitBottom, S::Dropping })
	{
		TestTrue(TEXT("abort from any state -> none"), Transition(From, E::Abort) == S::None);
	}
	TestTrue(TEXT("knockdown on the ladder -> dropping"), Transition(S::ClimbUp, E::Drop) == S::Dropping);
	TestTrue(TEXT("dropping -> landed -> none"), Transition(S::Dropping, E::Landed) == S::None);
	TestTrue(TEXT("no drop when not on a ladder"), Transition(S::None, E::Drop) == S::None);
	// Invalid events keep the state.
	TestTrue(TEXT("climbing up ignores a link event"), Transition(S::ClimbUp, E::LinkReachedTop) == S::ClimbUp);
	TestTrue(TEXT("align ignores target reached"), Transition(S::AlignBottom, E::TargetReached) == S::AlignBottom);
	TestTrue(TEXT("on-ladder states"), IsOnLadderState(S::ClimbDown) && IsOnLadderState(S::Dropping) && !IsOnLadderState(S::Queue) && !IsOnLadderState(S::None));
	TestTrue(TEXT("timeout = 2x expected + 1 s"), FMath::IsNearlyEqual(StateTimeout(3.f), 7.f) && StateTimeout(0.f) == 0.f);
	return true;
}

#endif
