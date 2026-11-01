// CodexTactics.Formation.PeekCarry.* - the squad's corner peek moves on to the next wall (Formation/FormationPeekCarryRules; the game
// designer's play-test 2026-11-01, bug_04 A: the squad peeking round one wall's end, the double-click on the parallel wall's end gave a wall
// layout with the old peeker left in his corner and no peek). UE-only.

#include "Misc/AutomationTest.h"
#include "Formation/FormationPeekCarryRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define PEEK_CARRY_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Formation.PeekCarry." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

PEEK_CARRY_TEST(FFormationPeekCarryShouldCarryTest, "ShouldCarry")
bool FFormationPeekCarryShouldCarryTest::RunTest(const FString&)
{
	using namespace FormationPeekCarry;
	FCarryOrderFacts Facts;
	Facts.bPeekInForce = true;
	Facts.bSlotAtCorner = true;
	Facts.bWallLayoutOrder = true;
	TestTrue(TEXT("the designer's case: peeking, the next wall's corner, the wall layout -> the peek moves on"), ShouldCarry(Facts));
	FCarryOrderFacts NoPeek = Facts;
	NoPeek.bPeekInForce = false;
	TestFalse(TEXT("no peek in force: a plain wall order (the first wall's order stays the wall layout)"), ShouldCarry(NoPeek));
	FCarryOrderFacts Middle = Facts;
	Middle.bSlotAtCorner = false;
	TestFalse(TEXT("a slot in the wall's middle (no corner to fire from): no peek there"), ShouldCarry(Middle));
	FCarryOrderFacts Box = Facts;
	Box.bWallLayoutOrder = false;
	TestFalse(TEXT("a box selection / cover switch (no wall layout): no carry"), ShouldCarry(Box));
	FCarryOrderFacts Planned = Facts;
	Planned.bPlanned = true;
	TestFalse(TEXT("planned in the tactical pause: no carry"), ShouldCarry(Planned));
	return true;
}

PEEK_CARRY_TEST(FFormationPeekCarryPickCornerTest, "PickCorner")
bool FFormationPeekCarryPickCornerTest::RunTest(const FString&)
{
	using namespace FormationPeekCarry;
	ECoverFacing Side = ECoverFacing::Left;
	float Along = 0.f;
	TestFalse(TEXT("no exposed edge: no corner"), PickCorner(false, 0.f, false, 0.f, Side, Along));
	TestTrue(TEXT("only the left edge"), PickCorner(true, 27.f, false, 0.f, Side, Along) && Side == ECoverFacing::Left);
	TestEqual(TEXT("... along the wall to the left (negative)"), Along, -27.f);
	TestTrue(TEXT("only the right edge"), PickCorner(false, 0.f, true, 140.f, Side, Along) && Side == ECoverFacing::Right);
	TestEqual(TEXT("... to the right (positive)"), Along, 140.f);
	TestTrue(TEXT("both: the nearer one (left 60 / right 200)"), PickCorner(true, 60.f, true, 200.f, Side, Along) && Side == ECoverFacing::Left && Along == -60.f);
	TestTrue(TEXT("both: the nearer one (left 300 / right 90)"), PickCorner(true, 300.f, true, 90.f, Side, Along) && Side == ECoverFacing::Right && Along == 90.f);
	// The aim point: 25 cm in from the corner, never past the slot.
	TestEqual(TEXT("aim 25 cm in from a corner 140 cm right"), AimAlong(140.f), 115.f);
	TestEqual(TEXT("aim 25 cm in from a corner 60 cm left"), AimAlong(-60.f), -35.f);
	TestEqual(TEXT("a corner closer than the inset: the slot itself"), AimAlong(-10.f), 0.f);
	return true;
}

PEEK_CARRY_TEST(FFormationPeekCarryDecideTest, "Decide")
bool FFormationPeekCarryDecideTest::RunTest(const FString&)
{
	using namespace FormationPeekCarry;
	FCarryProgressFacts OnWay;
	OnWay.bLeaderMoved = true;
	OnWay.Seconds = 2.f;
	TestTrue(TEXT("the leader runs to the new wall: wait"), Decide(OnWay) == EPeekCarryStep::Wait);
	FCarryProgressFacts StandUp;
	StandUp.Seconds = 0.5f;
	StandUp.LeaderStillSeconds = 0.5f;
	StandUp.bApproachDone = true;
	TestTrue(TEXT("still in his stand-up clip right after the order (not set off yet): wait"), Decide(StandUp) == EPeekCarryStep::Wait);
	FCarryProgressFacts There = OnWay;
	There.bApproachDone = true;
	There.LeaderStillSeconds = LeaderStillSeconds;
	TestTrue(TEXT("everyone at the new wall, the leader still: the peek"), Decide(There) == EPeekCarryStep::Fire);
	FCarryProgressFacts Straggler = OnWay;
	Straggler.LeaderStillSeconds = StragglerSeconds - 0.1f;
	TestTrue(TEXT("a man still on his way, the leader still just under the straggler time: wait"), Decide(Straggler) == EPeekCarryStep::Wait);
	Straggler.LeaderStillSeconds = StragglerSeconds;
	TestTrue(TEXT("... at the straggler time: the peek anyway"), Decide(Straggler) == EPeekCarryStep::Fire);
	FCarryProgressFacts NoMoveNeeded;
	NoMoveNeeded.Seconds = StartGraceSeconds;
	NoMoveNeeded.bApproachDone = true;
	NoMoveNeeded.LeaderStillSeconds = StartGraceSeconds;
	TestTrue(TEXT("his slot where he stood (never moved), all at the wall after the grace: the peek"), Decide(NoMoveNeeded) == EPeekCarryStep::Fire);
	FCarryProgressFacts NewOrder = There;
	NewOrder.bLeaderOrderUnchanged = false;
	TestTrue(TEXT("the leader's next order: dropped"), Decide(NewOrder) == EPeekCarryStep::Drop);
	FCarryProgressFacts NewLeader = There;
	NewLeader.bSameLeader = false;
	TestTrue(TEXT("another leader: dropped"), Decide(NewLeader) == EPeekCarryStep::Drop);
	FCarryProgressFacts Over = There;
	Over.bWallOrder = false;
	TestTrue(TEXT("the wall order is over: dropped"), Decide(Over) == EPeekCarryStep::Drop);
	FCarryProgressFacts Late = OnWay;
	Late.Seconds = MaxSeconds + 0.1f;
	TestTrue(TEXT("never got there: dropped"), Decide(Late) == EPeekCarryStep::Drop);
	return true;
}

#undef PEEK_CARRY_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
