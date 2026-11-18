// CodexTactics.Formation.MantleQueue.* - R17-1 (designer 2026-11-17): the squad climbs an obstacle in turns like a ladder (Characters/
// SquadMantleRules; MANTLING.md 6.4, FORMATION_CONTRACT.md 22): the lanes along the face, the climb order (the leader, then the formation
// slots), the waiters' looks (outwards, the threat side first, never into the obstacle) and the looks of the men up. UE-only.

#include "Misc/AutomationTest.h"
#include "Characters/MantleRules.h"
#include "Characters/SquadMantleRules.h"
#include "Formation/FormationSectorRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define MANTLE_QUEUE_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Formation.MantleQueue." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

MANTLE_QUEUE_TEST(FMantleQueueLanesTest, "Lanes")
bool FMantleQueueLanesTest::RunTest(const FString&)
{
	using namespace SquadMantleRules;
	const TArray<float> Wide = LaneOffsets(3, 100.f, -200.f, 200.f);
	TestTrue(TEXT("a 4 m wreck side: the first lane, then right and left of it"), Wide.Num() == 3 && Wide[0] == 0.f && Wide[1] == 100.f && Wide[2] == -100.f);
	const TArray<float> Five = LaneOffsets(5, 100.f, -250.f, 250.f);
	TestTrue(TEXT("five men: +-1 then +-2 lanes"), Five.Num() == 5 && Five[3] == 200.f && Five[4] == -200.f);
	const TArray<float> Edge = LaneOffsets(3, 100.f, 0.f, 260.f);
	TestTrue(TEXT("the first lane at the face's left end: the others to its right"), Edge.Num() == 3 && Edge[1] == 100.f && Edge[2] == 200.f);
	const TArray<float> Short = LaneOffsets(3, 100.f, -40.f, 40.f);
	TestTrue(TEXT("a face with room for one lane: one (the rest hold at the base)"), Short.Num() == 1 && Short[0] == 0.f);
	TestEqual(TEXT("nobody: no lane"), LaneOffsets(0, 100.f, -500.f, 500.f).Num(), 0);
	for (int32 A = 0; A < Five.Num(); ++A)
	{
		for (int32 B = A + 1; B < Five.Num(); ++B)
		{
			TestTrue(TEXT("lanes at least the spacing apart"), FMath::Abs(Five[A] - Five[B]) >= 100.f - KINDA_SMALL_NUMBER);
		}
	}
	return true;
}

MANTLE_QUEUE_TEST(FMantleQueueOrderTest, "ClimbOrder")
bool FMantleQueueOrderTest::RunTest(const FString&)
{
	using namespace SquadMantleRules;
	// Index 0 the leader (no slot), the followers' formation slots 2 / 0 / 1.
	const TArray<int32> Order = ClimbOrder(0, { INDEX_NONE, 2, 0, 1 });
	TestTrue(TEXT("the leader first, then the followers by their slots"), Order == TArray<int32>({ 0, 2, 3, 1 }));
	const TArray<int32> Box = ClimbOrder(0, { INDEX_NONE, INDEX_NONE, INDEX_NONE });
	TestTrue(TEXT("a box selection (no slots): the selection's order"), Box == TArray<int32>({ 0, 1, 2 }));
	const TArray<int32> Mixed = ClimbOrder(1, { 0, INDEX_NONE, INDEX_NONE, 1 });
	TestTrue(TEXT("a leader in the middle of the list: he first, slotted ones, then the rest as given"), Mixed == TArray<int32>({ 1, 0, 3, 2 }));
	return true;
}

MANTLE_QUEUE_TEST(FMantleQueueWaitLookTest, "WaitLooks")
bool FMantleQueueWaitLookTest::RunTest(const FString&)
{
	using namespace SquadMantleRules;
	const float Out = 180.f; // the obstacle to the east (+X): the waiters face west
	TestEqual(TEXT("place 0, no threat: straight out"), WaitLookYaw(Out, 0, 45.f, 80.f, TOptional<float>()), 180.f);
	TestEqual(TEXT("place 1: out +45"), WaitLookYaw(Out, 1, 45.f, 80.f, TOptional<float>()), -135.f);
	TestEqual(TEXT("place 2: out -45"), WaitLookYaw(Out, 2, 45.f, 80.f, TOptional<float>()), 135.f);
	TestEqual(TEXT("place 3: +90 capped at 80 off the outward look"), WaitLookYaw(Out, 3, 45.f, 80.f, TOptional<float>()), -100.f);
	TestEqual(TEXT("the threat side first: place 0 looks at a threat in the south-west"), WaitLookYaw(Out, 0, 45.f, 80.f, TOptional<float>(-150.f)), -150.f);
	TestEqual(TEXT("the others fan from the threat"), WaitLookYaw(Out, 1, 45.f, 80.f, TOptional<float>(-150.f)), -105.f);
	TestEqual(TEXT("... never more than 80 off outwards"), WaitLookYaw(Out, 3, 45.f, 80.f, TOptional<float>(-150.f)), -100.f);
	TestEqual(TEXT("a threat beyond the obstacle (east): the waiters keep facing out (the man up takes it)"), WaitLookYaw(Out, 0, 45.f, 80.f, TOptional<float>(10.f)), 180.f);
	for (int32 Place = 0; Place < 8; ++Place)
	{
		const float Look = WaitLookYaw(Out, Place, 45.f, 80.f, TOptional<float>(-120.f));
		TestTrue(FString::Printf(TEXT("place %d never covers into the obstacle"), Place), FMath::Abs(FMath::FindDeltaAngleDegrees(Out, Look)) <= 80.f + KINDA_SMALL_NUMBER);
	}
	TestTrue(TEXT("open side: within 90 of the outward look"), IsThreatOnOpenSide(Out, 95.f) && !IsThreatOnOpenSide(Out, 0.f));
	return true;
}

MANTLE_QUEUE_TEST(FMantleQueueWaitLookWallsTest, "WaitLookWalls")
bool FMantleQueueWaitLookWallsTest::RunTest(const FString&)
{
	// FormationMantleSmoke c / e (2026-11-18, the Depot's props spawned since G003b): a waiter 250 cm off a fence (east) with walls round him
	// behind and to both sides - the formation's ClearLook alone turned him towards the fence's free edge; ClearWaitLook keeps him within
	// squad.wait_max_off_deg of the outward look.
	using namespace SquadMantleRules;
	const FFormationConfig Config; // look.wall_dist 260, look.wall_angle 55, look.min_free_deg 30 (formation_config.json)
	const auto Wall = [](float AX, float AY, float BX, float BY)
	{
		FFormationWall W;
		W.A = FVector2D(AX, AY);
		W.B = FVector2D(BX, BY);
		W.Thickness = 10.f;
		return W;
	};
	const float Out = 180.f;
	const float MaxOff = 80.f;
	const FVector2D Pos(0.f, 0.f);
	const TArray<FFormationWall> Fence = { Wall(250.f, -400.f, 250.f, 400.f) };
	// Open behind him: the straight-out look stays.
	TestEqual(TEXT("only the fence (in front, 250 cm): he looks straight out"), ClearWaitLook(Fence, Pos, Out, Out, Out, MaxOff, Config), 180.f);
	// A crate right behind him: aside, within the arc.
	TArray<FFormationWall> Crate = Fence;
	Crate.Add(Wall(-100.f, -60.f, -100.f, 60.f));
	const float Aside = ClearWaitLook(Crate, Pos, Out, Out, Out, MaxOff, Config);
	TestTrue(FString::Printf(TEXT("a crate behind him: his look turns aside (%.0f), off the crate, within 80 of outwards"), Aside),
		FMath::Abs(FMath::FindDeltaAngleDegrees(Out, Aside)) >= 40.f && FMath::Abs(FMath::FindDeltaAngleDegrees(Out, Aside)) <= MaxOff + 0.01f);
	// Boxed in behind and on both sides: every direction is blocked (ClearLook's enclosed case).
	TArray<FFormationWall> Boxed = Fence;
	Boxed.Add(Wall(-100.f, -400.f, -100.f, 400.f));
	Boxed.Add(Wall(-400.f, 150.f, 0.f, 150.f));
	Boxed.Add(Wall(-400.f, -150.f, 0.f, -150.f));
	const float Unlimited = FormationSectorRules::ClearLook(Boxed, Pos, Out, Out, Config);
	TestTrue(FString::Printf(TEXT("the formation's ClearLook alone leaves the outward arc here (%.0f) - the smoke's bug"), Unlimited),
		FMath::Abs(FMath::FindDeltaAngleDegrees(Out, Unlimited)) > 90.f);
	for (const float Body : { 180.f, 90.f, 0.f, -90.f })
	{
		const float Look = ClearWaitLook(Boxed, Pos, Out, Body, Out, MaxOff, Config);
		TestTrue(FString::Printf(TEXT("boxed in, body at %.0f: the waiter's look %.0f stays within 80 of outwards"), Body, Look),
			FMath::Abs(FMath::FindDeltaAngleDegrees(Out, Look)) <= MaxOff + 0.01f);
	}
	// A fanned wanted look (place 3: -100) is kept when it is free.
	TestEqual(TEXT("a free fanned look is kept"), ClearWaitLook(Fence, Pos, -100.f, Out, Out, MaxOff, Config), -100.f);
	return true;
}

MANTLE_QUEUE_TEST(FMantleQueueUpLookTest, "UpLooks")
bool FMantleQueueUpLookTest::RunTest(const FString&)
{
	using namespace SquadMantleRules;
	const float Climb = 0.f; // climbed east
	TestEqual(TEXT("the first man up covers the far side (the fence vault: over it)"), UpLookYaw(Climb, 0, TOptional<float>()), 0.f);
	TestEqual(TEXT("... or a known threat"), UpLookYaw(Climb, 0, TOptional<float>(-60.f)), -60.f);
	TestEqual(TEXT("the second: +90"), UpLookYaw(Climb, 1, TOptional<float>()), 90.f);
	TestEqual(TEXT("the third: -90"), UpLookYaw(Climb, 2, TOptional<float>()), -90.f);
	TestEqual(TEXT("the fourth: back over the base"), FMath::Abs(UpLookYaw(Climb, 3, TOptional<float>())), 180.f);
	return true;
}

MANTLE_QUEUE_TEST(FMantleQueueTuningTest, "TuningNextToTheLadder")
bool FMantleQueueTuningTest::RunTest(const FString&)
{
	// The squad's numbers live in mantle.json (squad.*); the line's spot rule repeats the ladder queue's (LadderClimbComponent 90 / 75).
	const FMantleConfig Config = MantleRules::MakeDefaultConfig();
	TestEqual(TEXT("first spot = the ladder's QueueFirstSpotCm"), Config.SquadQueueFirstCm, 90.f);
	TestEqual(TEXT("spacing = the ladder's QueueSpacingCm"), Config.SquadQueueSpacingCm, 75.f);
	TestTrue(TEXT("two lanes' landings never closer than two bodies (2 x capsule radius 34)"), Config.SquadMinLandingGapCm >= 68.f
		&& Config.SquadLaneSpacingCm >= Config.SquadMinLandingGapCm);
	TestTrue(TEXT("the waiters' spots of a 3-man squad lie inside the base radius"), Config.SquadQueueFirstCm + Config.SquadQueueSpacingCm * 2.f < Config.SquadBaseRadiusCm);
	return true;
}

MANTLE_QUEUE_TEST(FMantleQueueReviewRulesTest, "TurnRules")
bool FMantleQueueReviewRulesTest::RunTest(const FString&)
{
	using namespace SquadMantleRules;
	// Review R17-1 #3: the group + 2 lanes per face, as half-step candidates; the full-spacing ones of them are the group + 2 lanes.
	TestEqual(TEXT("no lane: no candidate"), LaneCandidateCount(0), 0);
	TestEqual(TEXT("one lane: it and its two half-step neighbours"), LaneCandidateCount(1), 3);
	const int32 Lanes = 4 + 2; // a 4-man squad
	const TArray<float> Candidates = LaneOffsets(LaneCandidateCount(Lanes), 50.f, -1000.f, 1000.f);
	TestEqual(TEXT("a 4-man squad: 13 half-step candidates"), Candidates.Num(), 13);
	int32 Full = 0;
	for (const float Offset : Candidates)
	{
		Full += FMath::IsNearlyZero(FMath::Fmod(Offset, 100.f)) ? 1 : 0;
	}
	TestTrue(TEXT("... holding the group + 2 lanes a full spacing apart (a fence takes all four men)"), Full >= Lanes);

	// #1: the turn never times out while his man is in the clip (the next man would start mid-climb).
	TestFalse(TEXT("in the clip past the timeout: not timed out"), IsTurnOverTime(true, 45.f, 30.f));
	TestTrue(TEXT("out of the clip past the timeout: timed out"), IsTurnOverTime(false, 45.f, 30.f));
	TestFalse(TEXT("inside the timeout: not timed out"), IsTurnOverTime(false, 10.f, 30.f));

	// #5: a retry waits from its re-arm, not from the turn's start.
	TestTrue(TEXT("the first ask goes at once"), IsRetryDue(0, 0.f, 1.f));
	TestFalse(TEXT("re-armed just now: the retry waits"), IsRetryDue(1, 0.1f, 1.f));
	TestTrue(TEXT("re-armed a second ago: the retry goes"), IsRetryDue(1, 1.f, 1.f));
	TestFalse(TEXT("a second refusal re-armed just now waits too"), IsRetryDue(2, 0.f, 1.f));

	// #4: a holder's place is fixed by his member index (the line moving does not move him); a waiter's is his line place.
	TestEqual(TEXT("a waiter: his line place"), WaitSpotPlace(true, false, 1, 3), 1);
	TestEqual(TEXT("a holder: his member index"), WaitSpotPlace(false, true, INDEX_NONE, 3), 3);
	TestEqual(TEXT("... whatever the line does"), WaitSpotPlace(false, true, 0, 3), 3);
	TestEqual(TEXT("neither: no place"), WaitSpotPlace(false, false, 2, 3), INDEX_NONE);

	// #2: a path climb's man walks on to his goal whatever ended the climb.
	FWalkOnCheck Check;
	Check.bFromPath = true;
	Check.bResume = true;
	Check.bAlive = true;
	Check.bSameOrder = true;
	Check.DistToGoalCm = 800.f;
	TestTrue(TEXT("the leader of a path climb walks on (any end)"), ShouldWalkOn(Check));
	FWalkOnCheck Clicked = Check;
	Clicked.bFromPath = false;
	TestFalse(TEXT("a clicked climb holds up there"), ShouldWalkOn(Clicked));
	FWalkOnCheck Reordered = Check;
	Reordered.bSameOrder = false;
	TestFalse(TEXT("given another order since: his order stands"), ShouldWalkOn(Reordered));
	FWalkOnCheck Dead = Check;
	Dead.bAlive = false;
	TestFalse(TEXT("down: no walk"), ShouldWalkOn(Dead));
	FWalkOnCheck Grid = Check;
	Grid.bTurnBased = true;
	TestFalse(TEXT("the turn-based grid owns the squad"), ShouldWalkOn(Grid));
	FWalkOnCheck There = Check;
	There.DistToGoalCm = 30.f;
	TestFalse(TEXT("already at his goal"), ShouldWalkOn(There));
	FWalkOnCheck Follower = Check;
	Follower.bResume = false;
	TestFalse(TEXT("a formation follower follows (no walk of his own)"), ShouldWalkOn(Follower));
	FWalkOnCheck Reset = Check;
	Reset.bResumePath = false;
	TestFalse(TEXT("CancelSquadMantle (a smoke's reset) walks nobody on"), ShouldWalkOn(Reset));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
