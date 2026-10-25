#include "Misc/AutomationTest.h"
#include "Characters/ProgressionRules.h"
#include "GameFlow/GameFlowStateMachine.h"

#if WITH_DEV_AUTOMATION_TESTS

// 2026-10-22 (game designer play-test): "after a fight ends, remove the 'End combat and continue exploring' button - it's not needed"
// and "don't stop for level-up points during a fight - mark the operative's card". UE-only deviations from Godot
// main.gd _on_wave_cleared (victory panel after the last wave, the profile opened for the member with points).

#define FIGHT_END_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.GameFlow.FightEnd." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

FIGHT_END_TEST(FFightEndLastWaveTest, "LastWaveNoPanel")
bool FFightEndLastWaveTest::RunTest(const FString&)
{
	FGameFlowConfig Config;
	Config.TotalWaves = 2;
	Config.bSuddenWaveStart = true;
	FGameFlowStateMachine Machine(Config);
	TestEqual(TEXT("wave 1 starts"), Machine.TriggerCombatZone(), EGameFlowResult::Ok);
	TestFalse(TEXT("wave 1 of 2 is not the final fight"), Machine.IsFinalFight());
	TestEqual(TEXT("wave 1 cleared"), Machine.NotifyWaveCleared(), EGameFlowResult::Ok);
	// 2026-10-25: no panel between waves either (CodexTactics.GameFlow.AutoNextWave.*): the rest, then the wave by itself.
	TestTrue(TEXT("... between waves: the rest, no panel"), Machine.IsRestBetweenWaves());
	Machine.Tick(Config.WaveRestDuration + 0.1f);
	TestTrue(TEXT("wave 2 is the final fight"), Machine.IsFinalFight() && Machine.GetPhase() == ECodexGamePhase::WaveCombat);
	TestEqual(TEXT("the last wave cleared"), Machine.NotifyWaveCleared(), EGameFlowResult::Ok);
	TestEqual(TEXT("... straight to PostCombat: no panel, no button"), Machine.GetPhase(), ECodexGamePhase::PostCombat);
	TestEqual(TEXT("... the world keeps running"), Machine.GetTimeDilation(), 1.f);
	TestEqual(TEXT("... nothing left to click"), Machine.AdvanceAfterWave(), EGameFlowResult::WrongPhase);

	// A single ambush fight ("level" mode) ends the same way.
	FGameFlowConfig AmbushConfig;
	AmbushConfig.TotalWaves = 3;
	FGameFlowStateMachine Ambush(AmbushConfig);
	Ambush.StartAmbushCombat();
	TestTrue(TEXT("a single ambush fight is the final fight"), Ambush.IsFinalFight());
	Ambush.NotifyWaveCleared();
	TestEqual(TEXT("... its end goes straight to PostCombat"), Ambush.GetPhase(), ECodexGamePhase::PostCombat);

	// The Godot panel stays available per config.
	FGameFlowConfig Godot = Config;
	Godot.TotalWaves = 1;
	Godot.bVictoryPanelAfterLastWave = true;
	FGameFlowStateMachine Old(Godot);
	Old.TriggerCombatZone();
	Old.NotifyWaveCleared();
	TestEqual(TEXT("bVictoryPanelAfterLastWave: the old panel after the last wave"), Old.GetPhase(), ECodexGamePhase::WaveCleared);
	TestEqual(TEXT("... its button leads to PostCombat"), Old.AdvanceAfterWave(), EGameFlowResult::Ok);
	TestEqual(TEXT("... PostCombat"), Old.GetPhase(), ECodexGamePhase::PostCombat);
	return true;
}

FIGHT_END_TEST(FFightEndLevelUpBadgeTest, "LevelUpBadge")
bool FFightEndLevelUpBadgeTest::RunTest(const FString&)
{
	TestEqual(TEXT("no points: no badge"), ProgressionRules::GetUnspentBadgeText(0), FString());
	TestEqual(TEXT("3 points: \"+3\""), ProgressionRules::GetUnspentBadgeText(3), FString(TEXT("+3")));
	TestEqual(TEXT("two levels unspent: \"+6\""), ProgressionRules::GetUnspentBadgeText(6), FString(TEXT("+6")));
	const FString Line = ProgressionRules::GetLevelUpMessage(2);
	TestTrue(TEXT("the level-up line says the points wait (no modal)"), Line.Contains(TEXT("LEVEL 2")) && Line.Contains(TEXT("any time")));
	return true;
}

#undef FIGHT_END_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
