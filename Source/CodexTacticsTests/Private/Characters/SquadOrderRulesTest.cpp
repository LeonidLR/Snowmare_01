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

SQUAD_ORDER_TEST(FSquadOrderGroupCoverJoinTest, "GroupCoverJoin")
bool FSquadOrderGroupCoverJoinTest::RunTest(const FString&)
{
	// Game designer 2026-10-20: a cover click given to a GROUP takes the others along (reverses the 2026-10-11 "no automatic
	// cover stacking" for group orders only).
	using namespace SquadOrderRules;
	FGroupCoverJoinFacts Follower;
	Follower.bFollower = true;
	TestTrue(TEXT("Group mode (Solo off, exploration): a formation follower joins"), ShouldJoinGroupCover(Follower));

	FGroupCoverJoinFacts Case = Follower;
	Case.bSoloMode = true;
	TestFalse(TEXT("Solo, single selection: only the direct order"), ShouldJoinGroupCover(Case));
	Case = Follower;
	Case.bFormationActive = false;
	TestFalse(TEXT("A fight without a box selection: only the direct order"), ShouldJoinGroupCover(Case));
	Case = Follower;
	Case.bOnOwnOrder = true;
	TestFalse(TEXT("Group mode: one busy with his own order finishes it"), ShouldJoinGroupCover(Case));
	Case = Follower;
	Case.bFollower = false;
	TestFalse(TEXT("A guard is no follower"), ShouldJoinGroupCover(Case));
	Case = Follower;
	Case.bDirectOrder = true;
	TestFalse(TEXT("The direct-order operative is not one of the others"), ShouldJoinGroupCover(Case));
	Case = Follower;
	Case.bAlive = false;
	TestFalse(TEXT("The dead stay"), ShouldJoinGroupCover(Case));
	Case = Follower;
	Case.bUnavailable = true;
	TestFalse(TEXT("Aboard / held at an obstacle"), ShouldJoinGroupCover(Case));

	// A box selection is the group - in Solo and in a fight too, busy or not; the unselected stay.
	FGroupCoverJoinFacts Boxed;
	Boxed.bMultiSelection = true;
	Boxed.bSelected = true;
	Boxed.bSoloMode = true;
	Boxed.bFormationActive = false;
	Boxed.bOnOwnOrder = true;
	TestTrue(TEXT("Box-selected (Solo, fight, busy): joins"), ShouldJoinGroupCover(Boxed));
	Case = Boxed;
	Case.bSelected = false;
	Case.bSoloMode = false;
	Case.bFormationActive = true;
	Case.bFollower = true;
	Case.bOnOwnOrder = false;
	TestFalse(TEXT("A box selection without him: stays, even as a follower"), ShouldJoinGroupCover(Case));

	TestTrue(TEXT("One joins: a group order"), IsGroupCoverOrder(1));
	TestFalse(TEXT("Nobody joins: the single cover order"), IsGroupCoverOrder(0));
	return true;
}

SQUAD_ORDER_TEST(FSquadOrderStandToSprintTest, "StandToSprintWithLeader")
bool FSquadOrderStandToSprintTest::RunTest(const FString&)
{
	// Bug 2026-10-20 (lake demo, after the hut lesson): Regroup copied the leader's crouch (the Solo guard crouch) to the followers; the
	// Commander's sprint order stood him up, the crouched followers could not copy the sprint and walked behind him.
	using namespace SquadOrderRules;
	TestTrue(TEXT("Regroup / Solo crouch (the squad's) + the leader sprints: he stands up to sprint"), ShouldStandToSprintWithLeader(true, true, true, true));
	TestFalse(TEXT("The player crouched him (stance key / Alt + C): he keeps his stance (user decision 2026-10-01)"), ShouldStandToSprintWithLeader(true, true, false, true));
	TestFalse(TEXT("The leader walks: nothing to copy"), ShouldStandToSprintWithLeader(false, true, true, true));
	TestFalse(TEXT("Already standing: SetSprinting alone"), ShouldStandToSprintWithLeader(true, false, true, true));
	TestFalse(TEXT("Too cold / wounded / carrying to sprint: stays down"), ShouldStandToSprintWithLeader(true, true, true, false));
	return true;
}

SQUAD_ORDER_TEST(FSquadOrderPostFightHoldTest, "PostFightHold")
bool FSquadOrderPostFightHoldTest::RunTest(const FString&)
{
	// Game designer play-test 2026-10-22: "when a fight ends the operatives must stay where they were".
	using namespace SquadOrderRules;
	TestTrue(TEXT("Last wave / ambush fight -> PostCombat: hold"), ArmsPostFightHold(ECodexGamePhase::WaveCleared, ECodexGamePhase::PostCombat));
	TestTrue(TEXT("PostCombat -> Exploration: hold"), ArmsPostFightHold(ECodexGamePhase::PostCombat, ECodexGamePhase::Exploration));
	TestTrue(TEXT("Skirmish over (WaveCombat -> Exploration): hold"), ArmsPostFightHold(ECodexGamePhase::WaveCombat, ECodexGamePhase::Exploration));
	TestFalse(TEXT("Exploring on: no hold"), ArmsPostFightHold(ECodexGamePhase::Exploration, ECodexGamePhase::Exploration));
	TestFalse(TEXT("A fight starting: no hold"), ArmsPostFightHold(ECodexGamePhase::Exploration, ECodexGamePhase::WaveCombat));
	TestFalse(TEXT("Next wave (WaveCleared -> WaveCombat): no hold"), ArmsPostFightHold(ECodexGamePhase::WaveCleared, ECodexGamePhase::WaveCombat));
	TestFalse(TEXT("Cutscene -> Exploration (no fight): no hold"), ArmsPostFightHold(ECodexGamePhase::Cutscene, ECodexGamePhase::Exploration));
	TestTrue(TEXT("Leader standing: the hold stays"), KeepsPostFightHold(true, false));
	TestFalse(TEXT("Leader moves: the formation follows again"), KeepsPostFightHold(true, true));
	TestFalse(TEXT("No hold armed: nothing held"), KeepsPostFightHold(false, false));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
