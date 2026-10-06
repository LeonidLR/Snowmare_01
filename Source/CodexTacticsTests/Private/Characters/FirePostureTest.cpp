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
	TestEqual(TEXT("Label"), GetLabel(ESquadFirePosture::Aggressive), FString(TEXT("АГРЕССИВНЫЙ")));
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

#undef POSTURE_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
