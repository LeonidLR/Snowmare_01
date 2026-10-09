#include "Misc/AutomationTest.h"
#include "Combat/SniperRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Sniper rifle handling (user request 2026-10-09): fires only kneeling / prone and standing still; orders while standing /
// moving stop her and kneel her first; turn-based the kneel costs the stance AP on top of the shot.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSniperStanceRulesTest, "CodexTactics.Combat.Sniper.StanceAndMovement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSniperStanceRulesTest::RunTest(const FString&)
{
	TestFalse(TEXT("Standing never fires"), SniperRules::CanFireInStance(EOperativeStance::Standing));
	TestTrue(TEXT("Kneeling fires"), SniperRules::CanFireInStance(EOperativeStance::Crouching));
	TestTrue(TEXT("Prone fires"), SniperRules::CanFireInStance(EOperativeStance::Prone));
	TestFalse(TEXT("Kneeling but moving: no shot"), SniperRules::CanFireNow(EOperativeStance::Crouching, true));
	TestFalse(TEXT("Prone crawling: no shot"), SniperRules::CanFireNow(EOperativeStance::Prone, true));
	TestTrue(TEXT("Prone still: shot"), SniperRules::CanFireNow(EOperativeStance::Prone, false));
	TestFalse(TEXT("Standing still: no shot"), SniperRules::CanFireNow(EOperativeStance::Standing, false));

	TestEqual(TEXT("Standing kneels to fire"), SniperRules::FiringStance(EOperativeStance::Standing), EOperativeStance::Crouching);
	TestEqual(TEXT("Prone stays prone"), SniperRules::FiringStance(EOperativeStance::Prone), EOperativeStance::Prone);
	TestEqual(TEXT("Kneeling stays"), SniperRules::FiringStance(EOperativeStance::Crouching), EOperativeStance::Crouching);

	TestEqual(TEXT("Autonomy never stands a sniper up"), SniperRules::AutonomyStance(EOperativeStance::Standing, true), EOperativeStance::Crouching);
	TestEqual(TEXT("Autonomy prone kept"), SniperRules::AutonomyStance(EOperativeStance::Prone, true), EOperativeStance::Prone);
	TestEqual(TEXT("Autonomy of a rifleman unchanged"), SniperRules::AutonomyStance(EOperativeStance::Standing, false), EOperativeStance::Standing);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSniperFireStepTest, "CodexTactics.Combat.Sniper.FireStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSniperFireStepTest::RunTest(const FString&)
{
	FSniperFireContext Context;
	Context.Stance = EOperativeStance::Standing;
	Context.bMoving = true;
	Context.bDirectOrder = true;
	TestEqual(TEXT("Ordered while walking: stop first"), SniperRules::NextStep(Context), ESniperFireStep::Stop);
	Context.bDirectOrder = false;
	TestEqual(TEXT("Auto fire while walking: the move order wins, no shot"), SniperRules::NextStep(Context), ESniperFireStep::WaitForMove);

	Context.bMoving = false;
	TestEqual(TEXT("Standing still: kneel"), SniperRules::NextStep(Context), ESniperFireStep::Kneel);
	Context.bDirectOrder = true;
	TestEqual(TEXT("Ordered standing still: kneel"), SniperRules::NextStep(Context), ESniperFireStep::Kneel);

	Context.Stance = EOperativeStance::Crouching;
	Context.bStanceTransitionPlaying = true;
	TestEqual(TEXT("Kneel clip playing: settle"), SniperRules::NextStep(Context), ESniperFireStep::Settle);
	Context.bStanceTransitionPlaying = false;
	TestEqual(TEXT("Kneeling still: fire"), SniperRules::NextStep(Context), ESniperFireStep::Ready);

	Context.Stance = EOperativeStance::Prone;
	TestEqual(TEXT("Prone still: fire (stays prone)"), SniperRules::NextStep(Context), ESniperFireStep::Ready);
	Context.bMoving = true;
	TestEqual(TEXT("Crawling + order: stop"), SniperRules::NextStep(Context), ESniperFireStep::Stop);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSniperTurnBasedCostTest, "CodexTactics.Combat.Sniper.TurnBasedCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSniperTurnBasedCostTest::RunTest(const FString&)
{
	// Turn-based balance: stance 1 AP, attack 3 AP.
	TestEqual(TEXT("Standing: kneel 1 + shot 3"), SniperRules::TurnBasedAttackCost(EOperativeStance::Standing, 1, 3), 4);
	TestEqual(TEXT("Kneeling: shot only"), SniperRules::TurnBasedAttackCost(EOperativeStance::Crouching, 1, 3), 3);
	TestEqual(TEXT("Prone: shot only"), SniperRules::TurnBasedAttackCost(EOperativeStance::Prone, 1, 3), 3);
	TestTrue(TEXT("4 AP standing: affordable"), SniperRules::CanAffordTurnBasedAttack(4, EOperativeStance::Standing, 1, 3));
	TestFalse(TEXT("3 AP standing: refused (kneel + shot)"), SniperRules::CanAffordTurnBasedAttack(3, EOperativeStance::Standing, 1, 3));
	TestTrue(TEXT("3 AP kneeling: affordable"), SniperRules::CanAffordTurnBasedAttack(3, EOperativeStance::Crouching, 1, 3));
	TestFalse(TEXT("2 AP prone: refused"), SniperRules::CanAffordTurnBasedAttack(2, EOperativeStance::Prone, 1, 3));
	TestEqual(TEXT("Negative costs clamp"), SniperRules::TurnBasedAttackCost(EOperativeStance::Standing, -2, 3), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSniperAnimRulesTest, "CodexTactics.Combat.Sniper.AnimRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSniperAnimRulesTest::RunTest(const FString&)
{
	TestEqual(TEXT("Stand clip index"), SniperRules::ClipIndex(EOperativeStance::Standing), 0);
	TestEqual(TEXT("Knee clip index"), SniperRules::ClipIndex(EOperativeStance::Crouching), 1);
	TestEqual(TEXT("Prone clip index"), SniperRules::ClipIndex(EOperativeStance::Prone), 2);
	TestTrue(TEXT("Rounds left: bolt"), SniperRules::ShouldCycleBolt(4));
	TestFalse(TEXT("Empty magazine: no bolt (reload)"), SniperRules::ShouldCycleBolt(0));
	TestEqual(TEXT("3.3 s reload clip into 3.5 s"), SniperRules::PlayRateToFit(3.3f, 3.5f), 3.3f / 3.5f, 0.001f);
	TestEqual(TEXT("No clip length: rate 1"), SniperRules::PlayRateToFit(0.f, 3.5f), 1.f);
	return true;
}

#endif
