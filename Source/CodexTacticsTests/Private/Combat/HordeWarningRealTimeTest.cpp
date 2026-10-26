#include "Misc/AutomationTest.h"
#include "Combat/HordeIntroRules.h"
#include "GameFlow/GameFlowStateMachine.h"

#if WITH_DEV_AUTOMATION_TESTS

// User decisions 2026-10-26 (UE-only; Godot played a cutscene + preparation before every wave):
//  - "after the horde warning don't put us into the tactical pause, let the game continue in real time - the tactical pause ONLY by
//    pressing Space": the warning (FGameFlowStateMachine::NotifyHordeWarned) keeps the fight in real time; the wave's first Space opens
//    the free pause (no charge, no timer) - IsFreePauseOffered;
//  - "the horde warning only for the FIRST wave of the fight; later waves only as the arrows": HordeIntroRules::ShouldWarnWave.

#define HORDEWARNING_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Combat.HordeIntro.RealTime." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

HORDEWARNING_TEST(FHordeWarningKeepsRealTimeTest, "WarningKeepsTheFightInRealTime")
bool FHordeWarningKeepsRealTimeTest::RunTest(const FString&)
{
	FGameFlowConfig Config;
	Config.TotalWaves = 3;
	FGameFlowStateMachine Machine(Config);
	int32 Changes = 0;
	Machine.OnStateChanged.AddLambda([&Changes](ECodexGamePhase, ECodexCombatMode) { ++Changes; });
	TestEqual(TEXT("combat zone: the sudden wave"), Machine.TriggerCombatZone(), EGameFlowResult::Ok);
	TestTrue(TEXT("intro armed, the first Space offered free"), Machine.IsAwaitingHordeIntro() && Machine.IsFreePauseOffered());
	const int32 ChangesBefore = Changes;
	TestEqual(TEXT("the warning"), Machine.NotifyHordeWarned(), EGameFlowResult::Ok);
	TestEqual(TEXT("no state change: no pause"), Changes, ChangesBefore);
	TestEqual(TEXT("real time"), Machine.GetCombatMode(), ECodexCombatMode::RealTime);
	TestEqual(TEXT("real time dilation"), Machine.GetTimeDilation(), 1.f);
	TestFalse(TEXT("the intro is no longer awaited"), Machine.IsAwaitingHordeIntro());
	TestTrue(TEXT("... the free pause still offered"), Machine.IsFreePauseOffered() && !Machine.IsFreePause());
	TestEqual(TEXT("only once"), Machine.NotifyHordeWarned(), EGameFlowResult::WrongPhase);
	for (int32 Step = 0; Step < 1200; ++Step)
	{
		Machine.Tick(0.1f);
	}
	TestEqual(TEXT("120 s later: still real time (nothing opens a pause by itself)"), Machine.GetCombatMode(), ECodexCombatMode::RealTime);
	TestEqual(TEXT("the player's Space"), Machine.ToggleTacticalPause(), EGameFlowResult::Ok);
	TestTrue(TEXT("... opens the FREE pause"), Machine.IsFreePause() && Machine.GetCombatMode() == ECodexCombatMode::TacticalPause);
	TestEqual(TEXT("no charge spent"), Machine.GetPauseCharges(), Config.TacticalPauseMaxCharges);
	TestFalse(TEXT("the offer is used up"), Machine.IsFreePauseOffered());
	Machine.ToggleTacticalPause();
	TestEqual(TEXT("Space resumes"), Machine.GetCombatMode(), ECodexCombatMode::RealTime);
	Machine.ToggleTacticalPause();
	TestTrue(TEXT("the next pause is a paid one"), !Machine.IsFreePause() && Machine.GetPauseCharges() == Config.TacticalPauseMaxCharges - 1);
	return true;
}

HORDEWARNING_TEST(FHordeWarningOfferLifetimeTest, "FreePauseOfferEndsWithTheWave")
bool FHordeWarningOfferLifetimeTest::RunTest(const FString&)
{
	FGameFlowConfig Config;
	Config.TotalWaves = 3;
	FGameFlowStateMachine Cancelled(Config);
	Cancelled.TriggerCombatZone();
	Cancelled.NotifyHordeWarned();
	Cancelled.CancelHordeIntro();
	TestFalse(TEXT("smokes / the bot: no offer after CancelHordeIntro"), Cancelled.IsFreePauseOffered());
	Cancelled.ToggleTacticalPause();
	TestFalse(TEXT("... a paid pause"), Cancelled.IsFreePause());

	FGameFlowStateMachine Grid(Config);
	Grid.TriggerCombatZone();
	Grid.NotifyHordeWarned();
	Grid.RequestEnterTurnBased(true);
	Grid.ExitTurnBasedToRealTime();
	TestFalse(TEXT("the grid used the wave's free moment"), Grid.IsFreePauseOffered());

	FGameFlowStateMachine Waves(Config);
	Waves.TriggerCombatZone();
	Waves.NotifyHordeWarned();
	Waves.NotifyWaveCleared();
	TestFalse(TEXT("the rest: nothing offered"), Waves.IsFreePauseOffered());
	Waves.FinishPreparation();
	TestTrue(TEXT("wave 2 comes suddenly: its own first Space is free again"), Waves.GetWaveIndex() == 2 && Waves.IsFreePauseOffered());
	Waves.TriggerGameOver();
	TestFalse(TEXT("game over: nothing offered"), Waves.IsFreePauseOffered());
	return true;
}

HORDEWARNING_TEST(FHordeWarningFirstWaveOnlyTest, "WarningOnlyForTheFightsFirstWave")
bool FHordeWarningFirstWaveOnlyTest::RunTest(const FString&)
{
	const FHordeIntroConfig Defaults;
	TestFalse(TEXT("default: the warning opens no pause"), Defaults.bAutoPause);
	TestFalse(TEXT("default: later waves are not warned"), Defaults.bWarnLaterWaves);
	TestTrue(TEXT("wave 1 is warned"), HordeIntroRules::ShouldWarnWave(1, Defaults));
	TestFalse(TEXT("wave 2: only the arrows"), HordeIntroRules::ShouldWarnWave(2, Defaults));
	TestFalse(TEXT("wave 3: only the arrows"), HordeIntroRules::ShouldWarnWave(3, Defaults));
	FHordeIntroConfig Old;
	Old.bWarnLaterWaves = true;
	TestTrue(TEXT("the old rule (Codex.HordeIntro.LaterWaves 1): every wave"), HordeIntroRules::ShouldWarnWave(3, Old));
	return true;
}

#undef HORDEWARNING_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
