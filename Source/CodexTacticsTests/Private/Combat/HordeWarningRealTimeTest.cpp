#include "Misc/AutomationTest.h"
#include "Combat/HordeIntroRules.h"
#include "GameFlow/GameFlowStateMachine.h"
#include "UI/PhaseBannersWidget.h"

#if WITH_DEV_AUTOMATION_TESTS

// User decisions 2026-10-26 (UE-only; Godot played a cutscene + preparation before every wave):
//  - "after the horde warning don't put us into the tactical pause, let the game continue in real time - the tactical pause ONLY by
//    pressing Space": the warning (FGameFlowStateMachine::NotifyHordeWarned) keeps the fight in real time;
//  - "the first pause of a wave being free: NO": the wave's first Space is an ordinary pause (a charge, the timer, the order radius) -
//    before or after the warning (a Space before it ends the wait);
//  - "the horde warning only for the FIRST wave of the fight; later waves only as the arrows": HordeIntroRules::ShouldWarnWave.

#define HORDEWARNING_TEST(TestClass, TestPath) 	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Combat.HordeIntro.RealTime." TestPath, 		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

HORDEWARNING_TEST(FHordeWarningKeepsRealTimeTest, "WarningKeepsTheFightInRealTime")
bool FHordeWarningKeepsRealTimeTest::RunTest(const FString&)
{
	FGameFlowConfig Config;
	Config.TotalWaves = 3;
	FGameFlowStateMachine Machine(Config);
	int32 Changes = 0;
	Machine.OnStateChanged.AddLambda([&Changes](ECodexGamePhase, ECodexCombatMode) { ++Changes; });
	TestEqual(TEXT("combat zone: the sudden wave"), Machine.TriggerCombatZone(), EGameFlowResult::Ok);
	TestTrue(TEXT("intro armed"), Machine.IsAwaitingHordeIntro());
	const int32 ChangesBefore = Changes;
	TestEqual(TEXT("the warning"), Machine.NotifyHordeWarned(), EGameFlowResult::Ok);
	TestEqual(TEXT("no state change: no pause"), Changes, ChangesBefore);
	TestEqual(TEXT("real time"), Machine.GetCombatMode(), ECodexCombatMode::RealTime);
	TestEqual(TEXT("real time dilation"), Machine.GetTimeDilation(), 1.f);
	TestFalse(TEXT("the intro is no longer awaited"), Machine.IsAwaitingHordeIntro());
	TestEqual(TEXT("only once"), Machine.NotifyHordeWarned(), EGameFlowResult::WrongPhase);
	for (int32 Step = 0; Step < 1200; ++Step)
	{
		Machine.Tick(0.1f);
	}
	TestEqual(TEXT("120 s later: still real time (nothing opens a pause by itself)"), Machine.GetCombatMode(), ECodexCombatMode::RealTime);
	TestEqual(TEXT("the player's Space"), Machine.ToggleTacticalPause(), EGameFlowResult::Ok);
	TestTrue(TEXT("... opens an ORDINARY pause (no free first pause, 2026-10-26)"), !Machine.IsFreePause()
		&& Machine.GetCombatMode() == ECodexCombatMode::TacticalPause);
	TestEqual(TEXT("... a charge spent"), Machine.GetPauseCharges(), Config.TacticalPauseMaxCharges - 1);
	TestEqual(TEXT("... with its timer"), Machine.GetPauseTimeRemaining(), Config.TacticalPauseDuration);
	for (float Time = 0.f; Time < Config.TacticalPauseDuration + 0.5f; Time += 0.25f)
	{
		Machine.Tick(0.25f);
	}
	TestEqual(TEXT("the timer ends it: real time"), Machine.GetCombatMode(), ECodexCombatMode::RealTime);
	return true;
}

HORDEWARNING_TEST(FHordeWarningFirstSpaceOrdinaryTest, "FirstSpaceOfEveryWaveIsAnOrdinaryPause")
bool FHordeWarningFirstSpaceOrdinaryTest::RunTest(const FString&)
{
	FGameFlowConfig Config;
	Config.TotalWaves = 3;
	FGameFlowStateMachine Early(Config);
	Early.TriggerCombatZone();
	TestEqual(TEXT("Space before the warning"), Early.ToggleTacticalPause(), EGameFlowResult::Ok);
	TestTrue(TEXT("... an ordinary pause: a charge, the timer"), !Early.IsFreePause() && Early.GetPauseCharges() == Config.TacticalPauseMaxCharges - 1
		&& Early.GetPauseTimeRemaining() == Config.TacticalPauseDuration);
	Early.ToggleTacticalPause();
	TestTrue(TEXT("... it ended the wait for the warning (back in real time, nothing armed)"), Early.GetCombatMode() == ECodexCombatMode::RealTime
		&& !Early.IsAwaitingHordeIntro());

	FGameFlowStateMachine Waves(Config);
	Waves.TriggerCombatZone();
	Waves.NotifyHordeWarned();
	Waves.ToggleTacticalPause();
	Waves.ToggleTacticalPause();
	Waves.NotifyWaveCleared();
	Waves.FinishPreparation();
	TestTrue(TEXT("wave 2 comes suddenly, charges back"), Waves.GetWaveIndex() == 2 && Waves.GetPauseCharges() == Config.TacticalPauseMaxCharges);
	Waves.NotifyHordeWarned();
	Waves.ToggleTacticalPause();
	TestTrue(TEXT("wave 2's first Space: ordinary too"), !Waves.IsFreePause() && Waves.GetPauseCharges() == Config.TacticalPauseMaxCharges - 1);
	return true;
}

HORDEWARNING_TEST(FHordeWarningBannerHintTest, "BannerShowsTheOrdinaryPause")
bool FHordeWarningBannerHintTest::RunTest(const FString&)
{
	// The combat banner said "| [SPACE] free tactical pause" until the wave's first pause; now the charges (UPhaseBannersWidget::GetPauseHint).
	TestEqual(TEXT("charges left"), UPhaseBannersWidget::GetPauseHint(3, 3, 0.f), FString(TEXT("[SPACE] tactical pause 3/3")));
	TestEqual(TEXT("one left"), UPhaseBannersWidget::GetPauseHint(1, 3, 0.f), FString(TEXT("[SPACE] tactical pause 1/3")));
	TestEqual(TEXT("recharging"), UPhaseBannersWidget::GetPauseHint(0, 3, 12.2f), FString(TEXT("pauses recharge: 13 s")));
	TestEqual(TEXT("none"), UPhaseBannersWidget::GetPauseHint(0, 3, 0.f), FString(TEXT("no pauses left")));
	TestFalse(TEXT("never 'free'"), UPhaseBannersWidget::GetPauseHint(3, 3, 0.f).Contains(TEXT("free")));
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
