// CodexTactics.Squad.Orders.* — who owns an operative's movement: his own order or the formation (rev 5g, game designer
// play-test 2026-10-14, SquadOrderRules): a selection switch never cancels an order, Solo holds only the idle followers, a
// follower in cover leaves it to follow a leader who walks away in group mode.

#include "Misc/AutomationTest.h"
#include "Characters/SquadOrderRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define SQUAD_ORDER_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Squad.Orders." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

SQUAD_ORDER_TEST(FSquadOrderOwnOrderTest, "OwnOrderSurvivesSelection")
bool FSquadOrderOwnOrderTest::RunTest(const FString&)
{
	using namespace SquadOrderRules;
	FOwnOrderFacts Facts;
	TestFalse(TEXT("Idle / formation walk: no own order"), IsOnOwnOrder(Facts));
	Facts.bOwnMoveOrder = true;
	TestTrue(TEXT("A move order of his own"), IsOnOwnOrder(Facts));
	Facts = FOwnOrderFacts();
	Facts.bUseOrder = true;
	TestTrue(TEXT("A walk-up to use an object"), IsOnOwnOrder(Facts));
	Facts = FOwnOrderFacts();
	Facts.bObjectTask = true;
	TestTrue(TEXT("A push / deploy task"), IsOnOwnOrder(Facts));

	// Leader switch: the new leader keeps his own order, a formation walk stops.
	TestFalse(TEXT("New leader on his own order walks on"), ShouldStopOnBecomingLeader(true));
	TestTrue(TEXT("New leader walking the formation stops"), ShouldStopOnBecomingLeader(false));
	// Solo: the idle followers hold now, the one on his own order finishes it first.
	TestFalse(TEXT("Solo: own order finishes first"), ShouldHoldNow(true));
	TestTrue(TEXT("Solo: idle follower holds"), ShouldHoldNow(false));
	return true;
}

SQUAD_ORDER_TEST(FSquadOrderCoverFollowTest, "CoverFollowerLeavesForWalkingLeader")
bool FSquadOrderCoverFollowTest::RunTest(const FString&)
{
	using namespace SquadOrderRules;
	FCoverFollowFacts Facts;
	Facts.bInCover = true;
	Facts.bLeaderMoving = true;
	Facts.DistanceToSlotCm = 1500.f;
	TestTrue(TEXT("Group mode, leader walks 15 m away: leaves the cover"), ShouldLeaveCoverToFollow(Facts));

	FCoverFollowFacts Case = Facts;
	Case.DistanceToSlotCm = CoverLeaveSlotDistanceCm - 1.f;
	TestFalse(TEXT("Slot still close: stays at the wall"), ShouldLeaveCoverToFollow(Case));
	Case = Facts;
	Case.bLeaderMoving = false;
	TestFalse(TEXT("Leader stands: stays"), ShouldLeaveCoverToFollow(Case));
	Case = Facts;
	Case.bFollowersHolding = true;
	TestFalse(TEXT("Solo (holding): stays where ordered"), ShouldLeaveCoverToFollow(Case));
	Case = Facts;
	Case.bGuarding = true;
	TestFalse(TEXT("Guard: stays"), ShouldLeaveCoverToFollow(Case));
	Case = Facts;
	Case.bFormationActive = false;
	TestFalse(TEXT("Fight (formation off): the autonomy / ROE rules decide"), ShouldLeaveCoverToFollow(Case));
	Case = Facts;
	Case.bLeaderTakingCover = true;
	TestFalse(TEXT("Leader stacks up at cover himself: stays"), ShouldLeaveCoverToFollow(Case));
	Case = Facts;
	Case.bOnOwnOrder = true;
	TestFalse(TEXT("Still walking his own (cover) order: unchanged"), ShouldLeaveCoverToFollow(Case));
	Case = Facts;
	Case.bInCover = false;
	TestFalse(TEXT("Not in cover: nothing to leave"), ShouldLeaveCoverToFollow(Case));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
