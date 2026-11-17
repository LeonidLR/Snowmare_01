// CodexTactics.Formation.MantleQueue.* - R17-1 (designer 2026-11-17): the squad climbs an obstacle in turns like a ladder (Characters/
// SquadMantleRules; MANTLING.md 6.4, FORMATION_CONTRACT.md 22): the lanes along the face, the climb order (the leader, then the formation
// slots), the waiters' looks (outwards, the threat side first, never into the obstacle) and the looks of the men up. UE-only.

#include "Misc/AutomationTest.h"
#include "Characters/MantleRules.h"
#include "Characters/SquadMantleRules.h"

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

#endif // WITH_DEV_AUTOMATION_TESTS
