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
	TestFalse(TEXT("No group cover formation (turn-based / Codex.GroupCover.InCombat 0 in a fight) without a box selection: only the direct order"),
		ShouldJoinGroupCover(Case));
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

SQUAD_ORDER_TEST(FSquadOrderRegroupAfterFightTest, "RegroupAfterFight")
bool FSquadOrderRegroupAfterFightTest::RunTest(const FString&)
{
	// Bug 2026-10-23 (lake, after the horde; one operative KIA, one frozen): "even pressing B he does not return to the formation".
	using namespace SquadOrderRules;
	TestFalse(TEXT("Regroup (B) ends the post-fight hold with the leader standing"), KeepsPostFightHold(true, false, true));
	TestFalse(TEXT("Regroup while the leader moves: no hold"), KeepsPostFightHold(true, true, true));
	TestTrue(TEXT("No Regroup, leader standing: the hold stays"), KeepsPostFightHold(true, false, false));
	TestFalse(TEXT("Regroup with no hold armed: nothing held"), KeepsPostFightHold(false, false, true));

	// The formation runs while exploring and after the final fight (PostCombat lasts until the level ends).
	TestTrue(TEXT("Exploration: formation"), IsFormationPhase(ECodexGamePhase::Exploration));
	TestTrue(TEXT("PostCombat (after the final fight): formation"), IsFormationPhase(ECodexGamePhase::PostCombat));
	TestFalse(TEXT("Old rule (Codex.Squad.PostCombatFormation 0): no formation in PostCombat"), IsFormationPhase(ECodexGamePhase::PostCombat, false));
	TestTrue(TEXT("Old rule: exploration unchanged"), IsFormationPhase(ECodexGamePhase::Exploration, false));
	for (const ECodexGamePhase Fight : { ECodexGamePhase::Cutscene, ECodexGamePhase::Preparation, ECodexGamePhase::WaveCombat,
		ECodexGamePhase::WaveCleared, ECodexGamePhase::GameOver })
	{
		TestFalse(FString::Printf(TEXT("No EXPLORATION formation (group cover / wall layout) in phase %d"), static_cast<int32>(Fight)),
			IsFormationPhase(Fight));
	}
	return true;
}

SQUAD_ORDER_TEST(FSquadOrderCombatFormationTest, "CombatFormation.SquadStaysInFormationThroughTheFight")
bool FSquadOrderCombatFormationTest::RunTest(const FString&)
{
	// User decision 2026-10-26 (game designer: "after the pause the soldiers act SOLO in combat and B does nothing - until I give the order
	// to switch to solo mode, they must stay in formation"). The old rule (Godot is_tactical_mode, TANDEM Sprint 03 "formation only in
	// exploration") switched the following off from the preparation to the fight's end.
	using namespace SquadOrderRules;
	TestTrue(TEXT("Exploration"), IsFollowPhase(ECodexGamePhase::Exploration, ECodexCombatMode::None));
	TestTrue(TEXT("Cutscene"), IsFollowPhase(ECodexGamePhase::Cutscene, ECodexCombatMode::None));
	TestTrue(TEXT("Preparation / the rest between waves"), IsFollowPhase(ECodexGamePhase::Preparation, ECodexCombatMode::None));
	TestTrue(TEXT("The real-time fight"), IsFollowPhase(ECodexGamePhase::WaveCombat, ECodexCombatMode::RealTime));
	TestTrue(TEXT("The tactical pause"), IsFollowPhase(ECodexGamePhase::WaveCombat, ECodexCombatMode::TacticalPause));
	TestTrue(TEXT("The wave-cleared panel"), IsFollowPhase(ECodexGamePhase::WaveCleared, ECodexCombatMode::None));
	TestTrue(TEXT("After the final fight"), IsFollowPhase(ECodexGamePhase::PostCombat, ECodexCombatMode::None));
	TestFalse(TEXT("Not on the turn-based grid (it moves everyone)"), IsFollowPhase(ECodexGamePhase::WaveCombat, ECodexCombatMode::TurnBased));
	TestFalse(TEXT("Not after a game over"), IsFollowPhase(ECodexGamePhase::GameOver, ECodexCombatMode::None));
	// The old rule (Codex.Squad.CombatFormation 0): individual control in the fight.
	TestFalse(TEXT("Old rule: no following in the real-time fight"), IsFollowPhase(ECodexGamePhase::WaveCombat, ECodexCombatMode::RealTime, true, false));
	TestFalse(TEXT("Old rule: none in the preparation"), IsFollowPhase(ECodexGamePhase::Preparation, ECodexCombatMode::None, true, false));
	TestTrue(TEXT("Old rule: exploration unchanged"), IsFollowPhase(ECodexGamePhase::Exploration, ECodexCombatMode::None, true, false));

	// A member given an attack order in the pause carries it out, then rejoins: a running fire order is his own order.
	FOwnOrderFacts Fire;
	Fire.bFireOrder = true;
	TestTrue(TEXT("Suppressing / a quick-fire burst: the formation leaves him alone"), IsOnOwnOrder(Fire));
	TestFalse(TEXT("... the order over: he rejoins"), IsOnOwnOrder(FOwnOrderFacts()));

	// A formation member fighting from cover stays there until the leader walks away - then he follows (as out of combat).
	FCoverFollowFacts Cover;
	Cover.bInCover = true;
	Cover.bFormationActive = IsFollowPhase(ECodexGamePhase::WaveCombat, ECodexCombatMode::RealTime);
	Cover.DistanceToSlotCm = CoverLeaveSlotDistanceCm + 100.f;
	TestFalse(TEXT("In cover, the leader standing: he holds his cover in the fight"), ShouldLeaveCoverToFollow(Cover));
	Cover.bLeaderMoving = true;
	TestTrue(TEXT("The leader walks away: he leaves the cover and follows (the fight too)"), ShouldLeaveCoverToFollow(Cover));
	Cover.bFormationActive = IsFollowPhase(ECodexGamePhase::WaveCombat, ECodexCombatMode::RealTime, true, false);
	TestFalse(TEXT("Old rule: he stayed in cover for the whole fight"), ShouldLeaveCoverToFollow(Cover));
	return true;
}

SQUAD_ORDER_TEST(FSquadOrderGroupCoverInCombatTest, "CombatFormation.GroupCoverInTheFight")
bool FSquadOrderGroupCoverInCombatTest::RunTest(const FString&)
{
	// User decision 2026-10-26 ("group cover in combat: YES"): the group cover order without a box selection (the others take cover too,
	// the wall layout, the corner stack) runs in the real-time fight and the tactical pause, as outside the fight; turn-based stays as it is.
	using namespace SquadOrderRules;
	TestTrue(TEXT("Exploration"), IsGroupCoverPhase(ECodexGamePhase::Exploration, ECodexCombatMode::None));
	TestTrue(TEXT("After the final fight"), IsGroupCoverPhase(ECodexGamePhase::PostCombat, ECodexCombatMode::None));
	TestTrue(TEXT("The real-time fight"), IsGroupCoverPhase(ECodexGamePhase::WaveCombat, ECodexCombatMode::RealTime));
	TestTrue(TEXT("The tactical pause"), IsGroupCoverPhase(ECodexGamePhase::WaveCombat, ECodexCombatMode::TacticalPause));
	TestTrue(TEXT("The preparation / the rest between waves"), IsGroupCoverPhase(ECodexGamePhase::Preparation, ECodexCombatMode::None));
	TestFalse(TEXT("Not on the turn-based grid"), IsGroupCoverPhase(ECodexGamePhase::WaveCombat, ECodexCombatMode::TurnBased));
	TestFalse(TEXT("Not after a game over"), IsGroupCoverPhase(ECodexGamePhase::GameOver, ECodexCombatMode::None));
	// The old rule (Codex.GroupCover.InCombat 0) and a fight without the formation (Codex.Squad.CombatFormation 0): exploration only.
	TestFalse(TEXT("Old rule: a fight's cover click is individual"), IsGroupCoverPhase(ECodexGamePhase::WaveCombat, ECodexCombatMode::RealTime, true, true, false));
	TestTrue(TEXT("Old rule: exploration unchanged"), IsGroupCoverPhase(ECodexGamePhase::Exploration, ECodexCombatMode::None, true, true, false));
	TestFalse(TEXT("No formation in the fight: no group cover either"), IsGroupCoverPhase(ECodexGamePhase::WaveCombat, ECodexCombatMode::TacticalPause, true, false));

	// Who goes along in the fight: group mode -> the followers; Solo with a single selection -> nobody (the individual cover click).
	FGroupCoverJoinFacts Follower;
	Follower.bAlive = true;
	Follower.bFollower = true;
	Follower.bFormationActive = IsGroupCoverPhase(ECodexGamePhase::WaveCombat, ECodexCombatMode::RealTime);
	TestTrue(TEXT("Group mode in the real-time fight: a follower joins"), ShouldJoinGroupCover(Follower));
	Follower.bFormationActive = IsGroupCoverPhase(ECodexGamePhase::WaveCombat, ECodexCombatMode::TacticalPause);
	TestTrue(TEXT("... and in the tactical pause"), ShouldJoinGroupCover(Follower));
	FGroupCoverJoinFacts Solo = Follower;
	Solo.bSoloMode = true;
	TestFalse(TEXT("Solo, single selection, in the fight: only the direct order"), ShouldJoinGroupCover(Solo));
	Follower.bFormationActive = IsGroupCoverPhase(ECodexGamePhase::WaveCombat, ECodexCombatMode::RealTime, true, true, false);
	TestFalse(TEXT("Old rule: nobody joins in the fight"), ShouldJoinGroupCover(Follower));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
