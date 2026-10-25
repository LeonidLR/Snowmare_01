// CodexTactics.Squad.Posture.* — rules of engagement of the operatives' automatic fire (user request 2026-10-06,
// FirePostureRules): Passive / Defensive / Aggressive, direct orders always obeyed, Aggressive auto-fire starts an ambush.

#include "Misc/AutomationTest.h"
#include "Characters/FirePostureRules.h"
#include "GameFlow/GameFlowStateMachine.h"
#include "GameFlow/LevelEncounterRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define POSTURE_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Squad.Posture." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

POSTURE_TEST(FPosturePassiveNeverAutoFiresTest, "PassiveNeverAutoFires")
bool FPosturePassiveNeverAutoFiresTest::RunTest(const FString&)
{
	using namespace FirePostureRules;
	FFirePostureConfig Config;
	TestFalse(TEXT("Not provoked"), MayAutoFire(ESquadFirePosture::Passive, false, false, Config));
	TestFalse(TEXT("Attacked itself"), MayAutoFire(ESquadFirePosture::Passive, true, true, Config));
	Config.bDefensiveSquadWideProvocation = true;
	TestFalse(TEXT("Squad attacked, squad-wide option"), MayAutoFire(ESquadFirePosture::Passive, true, true, Config));
	TestFalse(TEXT("No exploration opening"), MayAutoFireInExploration(ESquadFirePosture::Passive, true, ECodexGamePhase::Exploration, false, Config));
	return true;
}

POSTURE_TEST(FPosturePassiveObeysDirectOrderTest, "PassiveObeysDirectOrder")
bool FPosturePassiveObeysDirectOrderTest::RunTest(const FString&)
{
	using namespace FirePostureRules;
	const FFirePostureConfig Config;
	for (const ESquadFirePosture Posture : { ESquadFirePosture::Passive, ESquadFirePosture::Defensive, ESquadFirePosture::Aggressive })
	{
		TestTrue(FString::Printf(TEXT("%s obeys a direct order"), *GetLabel(Posture)), MayFire(Posture, true, false, false, Config));
	}
	TestFalse(TEXT("Passive without an order holds"), MayFire(ESquadFirePosture::Passive, false, true, true, Config));
	// The per-operative override wins over the squad posture.
	TestEqual(TEXT("Override"), Resolve(ESquadFirePosture::Aggressive, true, ESquadFirePosture::Passive), ESquadFirePosture::Passive);
	TestEqual(TEXT("No override: squad"), Resolve(ESquadFirePosture::Defensive, false, ESquadFirePosture::Passive), ESquadFirePosture::Defensive);
	TestEqual(TEXT("Default reproduces the old auto-fire"), DefaultPosture, ESquadFirePosture::Aggressive);
	TestEqual(TEXT("Keys"), GetKeyHint(ESquadFirePosture::Passive) + GetKeyHint(ESquadFirePosture::Defensive) + GetKeyHint(ESquadFirePosture::Aggressive),
		FString(TEXT(",./")));
	return true;
}

POSTURE_TEST(FPostureDefensiveHoldsUntilAttackedTest, "DefensiveHoldsUntilAttacked")
bool FPostureDefensiveHoldsUntilAttackedTest::RunTest(const FString&)
{
	using namespace FirePostureRules;
	FFirePostureConfig Config;
	TestFalse(TEXT("Sees enemies, nobody attacked: holds"), MayAutoFire(ESquadFirePosture::Defensive, false, false, Config));
	TestFalse(TEXT("A mate attacked, own-only (default): still holds"), MayAutoFire(ESquadFirePosture::Defensive, false, true, Config));
	TestFalse(TEXT("No exploration opening"), MayAutoFireInExploration(ESquadFirePosture::Defensive, true, ECodexGamePhase::Exploration, false, Config));
	// Provocation lasts one fight.
	TestTrue(TEXT("Cleared after the wave"), ClearsProvocation(ECodexGamePhase::WaveCleared));
	TestTrue(TEXT("Cleared in the rest before the next wave (2026-10-25: no panel)"), ClearsProvocation(ECodexGamePhase::Preparation));
	TestTrue(TEXT("Cleared back in exploration"), ClearsProvocation(ECodexGamePhase::Exploration));
	TestFalse(TEXT("Kept during the fight"), ClearsProvocation(ECodexGamePhase::WaveCombat));
	return true;
}

POSTURE_TEST(FPostureDefensiveReturnsFireTest, "DefensiveReturnsFireWhenAttacked")
bool FPostureDefensiveReturnsFireTest::RunTest(const FString&)
{
	using namespace FirePostureRules;
	FFirePostureConfig Config;
	TestTrue(TEXT("Attacked itself: fights back"), MayAutoFire(ESquadFirePosture::Defensive, true, true, Config));
	Config.bDefensiveSquadWideProvocation = true;
	TestTrue(TEXT("Squad-wide option: a mate attacked is enough"), MayAutoFire(ESquadFirePosture::Defensive, false, true, Config));
	TestFalse(TEXT("Squad-wide option, nobody attacked"), MayAutoFire(ESquadFirePosture::Defensive, false, false, Config));
	return true;
}

POSTURE_TEST(FPostureAggressiveFiresOnSightTest, "AggressiveFiresOnSight")
bool FPostureAggressiveFiresOnSightTest::RunTest(const FString&)
{
	using namespace FirePostureRules;
	const FFirePostureConfig Config;
	TestTrue(TEXT("Fires unprovoked"), MayAutoFire(ESquadFirePosture::Aggressive, false, false, Config));
	TestEqual(TEXT("Cycle"), Next(Next(Next(ESquadFirePosture::Aggressive))), ESquadFirePosture::Aggressive);
	TestEqual(TEXT("Label"), GetLabel(ESquadFirePosture::Aggressive), FString(TEXT("AGGRESSIVE")));
	return true;
}

POSTURE_TEST(FPostureAggressiveStartsAmbushTest, "AggressiveAutoFireStartsAmbushCombat")
bool FPostureAggressiveStartsAmbushTest::RunTest(const FString&)
{
	using namespace FirePostureRules;
	FFirePostureConfig Config;
	FGameFlowStateMachine Machine;
	// Ambush level, still exploring: the aggressive operative who sees a patrol opens fire = a squad attack.
	TestTrue(TEXT("Aggressive opens fire in exploration"),
		MayAutoFireInExploration(ESquadFirePosture::Aggressive, true, Machine.GetPhase(), Machine.IsCombatUnlocked(), Config));
	TestTrue(TEXT("That contact starts the ambush"),
		LevelEncounterRules::ShouldStartAmbush(true, Machine.GetPhase(), Machine.IsCombatUnlocked()));
	TestEqual(TEXT("Ambush start"), Machine.StartAmbushCombat(), EGameFlowResult::Ok);
	TestEqual(TEXT("Real-time fight at once"), Machine.GetCombatMode(), ECodexCombatMode::RealTime);
	TestEqual(TEXT("Wave combat"), Machine.GetPhase(), ECodexGamePhase::WaveCombat);
	TestFalse(TEXT("Once: no second opening"),
		MayAutoFireInExploration(ESquadFirePosture::Aggressive, true, Machine.GetPhase(), Machine.IsCombatUnlocked(), Config));
	TestTrue(TEXT("In the fight it keeps firing"), MayAutoFire(ESquadFirePosture::Aggressive, false, false, Config));

	FGameFlowStateMachine WaveMap;
	TestFalse(TEXT("A classic wave map stays peaceful in exploration"),
		MayAutoFireInExploration(ESquadFirePosture::Aggressive, false, WaveMap.GetPhase(), WaveMap.IsCombatUnlocked(), Config));
	Config.bAggressiveOpensFireInExploration = false;
	TestFalse(TEXT("Option off"), MayAutoFireInExploration(ESquadFirePosture::Aggressive, true, ECodexGamePhase::Exploration, false, Config));
	return true;
}

POSTURE_TEST(FPostureOrderSelectedOnlyTest, "OrderSetsSelectedOperativeOnly")
bool FPostureOrderSelectedOnlyTest::RunTest(const FString&)
{
	using namespace FirePostureRules;
	// User decision 2026-10-06: , . / change the selected operative(s) — the controlled leader alone, or a group.
	TestEqual(TEXT("One operative (the leader) selected"), GetOrderScope(false, 1), EPostureOrderScope::Selected);
	TestEqual(TEXT("A box-selected group"), GetOrderScope(false, 3), EPostureOrderScope::Selected);
	TestEqual(TEXT("Nobody selected: the squad"), GetOrderScope(false, 0), EPostureOrderScope::Squad);
	// Personal overrides: the leader Passive, the others keep the squad's Aggressive.
	TestEqual(TEXT("Leader's own"), Resolve(ESquadFirePosture::Aggressive, true, ESquadFirePosture::Passive), ESquadFirePosture::Passive);
	TestEqual(TEXT("Mate keeps the squad's"), Resolve(ESquadFirePosture::Aggressive, false, ESquadFirePosture::Passive), ESquadFirePosture::Aggressive);
	return true;
}

POSTURE_TEST(FPostureAltSquadWideTest, "AltSetsWholeSquad")
bool FPostureAltSquadWideTest::RunTest(const FString&)
{
	using namespace FirePostureRules;
	TestEqual(TEXT("Alt with one selected"), GetOrderScope(true, 1), EPostureOrderScope::Squad);
	TestEqual(TEXT("Alt with a group"), GetOrderScope(true, 3), EPostureOrderScope::Squad);
	return true;
}

POSTURE_TEST(FPostureIndicatorTest, "SelectionIndicator")
bool FPostureIndicatorTest::RunTest(const FString&)
{
	using namespace FirePostureRules;
	ESquadFirePosture Common = ESquadFirePosture::Aggressive;
	TestTrue(TEXT("Same postures"), GetCommonPosture({ ESquadFirePosture::Defensive, ESquadFirePosture::Defensive }, Common));
	TestEqual(TEXT("Shared one"), Common, ESquadFirePosture::Defensive);
	TestFalse(TEXT("Mixed group: none lit"), GetCommonPosture({ ESquadFirePosture::Defensive, ESquadFirePosture::Passive }, Common));
	TestFalse(TEXT("Nobody"), GetCommonPosture({}, Common));
	TestEqual(TEXT("Letters"), GetLetter(ESquadFirePosture::Passive) + GetLetter(ESquadFirePosture::Defensive) + GetLetter(ESquadFirePosture::Aggressive),
		FString(TEXT("PDA")));
	return true;
}

// User decision 2026-10-19 (game designer play-test): the three postures, per TARGET - Passive never opens fire on its own,
// Defensive returns fire / engages only enemies that attack or have detected the squad, Aggressive fires at any enemy in sight.
POSTURE_TEST(FPosturePerTargetRulesTest, "PerTargetRules")
bool FPosturePerTargetRulesTest::RunTest(const FString&)
{
	using namespace FirePostureRules;
	const FFirePostureConfig Config;
	// Passive: nothing, whatever the enemy does (direct orders are MayFire's business).
	TestFalse(TEXT("Passive: an unaware enemy"), MayAutoFireAt(ESquadFirePosture::Passive, false, false, false, Config));
	TestFalse(TEXT("Passive: an enemy that detected the squad"), MayAutoFireAt(ESquadFirePosture::Passive, false, false, true, Config));
	TestFalse(TEXT("Passive: attacked"), MayAutoFireAt(ESquadFirePosture::Passive, true, true, true, Config));
	TestFalse(TEXT("Passive: no automatic targets at all"), AllowsAutoTargets(ESquadFirePosture::Passive));
	// Defensive: only the enemies that hunt the squad - until it is attacked, then any.
	TestFalse(TEXT("Defensive: an unaware patrol is left alone"), MayAutoFireAt(ESquadFirePosture::Defensive, false, false, false, Config));
	TestTrue(TEXT("Defensive: an enemy that detected the squad is engaged"), MayAutoFireAt(ESquadFirePosture::Defensive, false, false, true, Config));
	TestTrue(TEXT("Defensive, attacked: returns fire at any enemy"), MayAutoFireAt(ESquadFirePosture::Defensive, true, true, false, Config));
	TestFalse(TEXT("Defensive: a mate attacked (own-only default) does not free the unaware ones"),
		MayAutoFireAt(ESquadFirePosture::Defensive, false, true, false, Config));
	TestTrue(TEXT("Defensive keeps looking for detecting enemies"), AllowsAutoTargets(ESquadFirePosture::Defensive));
	TestFalse(TEXT("Defensive, not provoked: no grenades / no target of its own (MayAutoFire)"), MayAutoFire(ESquadFirePosture::Defensive, false, false, Config));
	// Aggressive: every enemy in sight.
	TestTrue(TEXT("Aggressive: an unaware enemy"), MayAutoFireAt(ESquadFirePosture::Aggressive, false, false, false, Config));
	TestTrue(TEXT("Aggressive: automatic targets"), AllowsAutoTargets(ESquadFirePosture::Aggressive));
	// Data names (the tutorial's set_posture action).
	ESquadFirePosture Parsed = ESquadFirePosture::Aggressive;
	TestTrue(TEXT("parse passive"), ParseName(TEXT("passive"), Parsed) && Parsed == ESquadFirePosture::Passive);
	TestTrue(TEXT("parse Defensive"), ParseName(TEXT("Defensive"), Parsed) && Parsed == ESquadFirePosture::Defensive);
	TestTrue(TEXT("parse AGGRESSIVE"), ParseName(TEXT("AGGRESSIVE"), Parsed) && Parsed == ESquadFirePosture::Aggressive);
	TestFalse(TEXT("unknown name"), ParseName(TEXT("berserk"), Parsed));
	return true;
}

// Fix 2026-10-19 (game designer: "they see each other, yet the AGGRESSIVE squad does not shoot"): the Demo 01 lake is a «button»
// level (combat_start "button") with enemies placed in the map; contact with a placed enemy starts the fight there
// (LevelEncounterRules::ShouldStartFightOnContact), but the exploration opening asked for an AMBUSH level only.
POSTURE_TEST(FPostureAggressiveButtonLevelTest, "AggressiveOpensFireOnPlacedEnemyOfButtonLevel")
bool FPostureAggressiveButtonLevelTest::RunTest(const FString&)
{
	using namespace FirePostureRules;
	const FFirePostureConfig Config;
	const bool bAmbushLevel = false;
	const bool bLevelHasPlacedEnemies = true;
	const bool bContactStartsFight = bAmbushLevel || bLevelHasPlacedEnemies; // AOperativeCharacter::RunCombatShooting
	TestTrue(TEXT("button level + placed patrol: Aggressive opens fire in exploration"),
		MayAutoFireInExploration(ESquadFirePosture::Aggressive, bContactStartsFight, ECodexGamePhase::Exploration, false, Config));
	TestTrue(TEXT("... and that contact (a placed enemy) starts the fight"),
		LevelEncounterRules::ShouldStartFightOnContact(bAmbushLevel, true, ECodexGamePhase::Exploration, false));
	TestFalse(TEXT("... but not contact with an enemy the map did not place (a wave spawn)"),
		LevelEncounterRules::ShouldStartFightOnContact(bAmbushLevel, false, ECodexGamePhase::Exploration, false));
	TestFalse(TEXT("Passive never opens it"), MayAutoFireInExploration(ESquadFirePosture::Passive, bContactStartsFight, ECodexGamePhase::Exploration, false, Config));
	TestFalse(TEXT("Defensive never opens it (an enemy that detects the squad starts the fight itself)"),
		MayAutoFireInExploration(ESquadFirePosture::Defensive, bContactStartsFight, ECodexGamePhase::Exploration, false, Config));
	TestFalse(TEXT("a «button» level without placed enemies stays peaceful"),
		MayAutoFireInExploration(ESquadFirePosture::Aggressive, false, ECodexGamePhase::Exploration, false, Config));
	TestFalse(TEXT("not once the fight is unlocked"),
		MayAutoFireInExploration(ESquadFirePosture::Aggressive, bContactStartsFight, ECodexGamePhase::Exploration, true, Config));
	return true;
}

#undef POSTURE_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
