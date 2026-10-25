#include "Misc/AutomationTest.h"
#include "Characters/FirePostureRules.h"
#include "GameFlow/GameFlowStateMachine.h"
#include "Quests/GateProgressRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// 2026-10-25 (game designer): "When a wave ends, remove the plate with the text «Start the next wave». The waves must break through
// on their own." A cleared wave that is not the fight's last goes straight into the rest before the next one (Preparation,
// WaveRestDuration - level JSON wave_rest_duration - the world running) and the next wave starts by itself when it runs out; the
// last wave still ends the fight (PostCombat). UE-only deviation from Godot main.gd _on_wave_cleared / _on_next_wave_pressed
// (FGameFlowConfig::bVictoryPanelBetweenWaves restores the panel).

#define AUTO_NEXT_WAVE_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.GameFlow.AutoNextWave." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AutoNextWaveTest
{
	/** Ticks the machine in 0.1 s steps for Seconds real seconds. */
	void TickFor(FGameFlowStateMachine& Machine, float Seconds)
	{
		const int32 Steps = FMath::RoundToInt(Seconds / 0.1f);
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			Machine.Tick(0.1f);
		}
	}
}

using namespace AutoNextWaveTest;

AUTO_NEXT_WAVE_TEST(FAutoNextWaveSuddenTest, "ClearedWaveRestsThenTheNextBreaksThrough")
bool FAutoNextWaveSuddenTest::RunTest(const FString&)
{
	FGameFlowConfig Config;
	Config.TotalWaves = 3;
	Config.WaveRestDuration = 20.f;
	TestFalse(TEXT("default: no panel between waves"), Config.bVictoryPanelBetweenWaves);
	FGameFlowStateMachine Machine(Config);
	TArray<ECodexGamePhase> Phases;
	Machine.OnStateChanged.AddLambda([&Phases](ECodexGamePhase Phase, ECodexCombatMode) { Phases.AddUnique(Phase); });
	Machine.TriggerCombatZone();
	Machine.CancelHordeIntro();
	Machine.ToggleTacticalPause(); // spend a charge
	Machine.ToggleTacticalPause();
	TestEqual(TEXT("wave 1 cleared"), Machine.NotifyWaveCleared(), EGameFlowResult::Ok);
	TestEqual(TEXT("straight into the rest (Preparation)"), Machine.GetPhase(), ECodexGamePhase::Preparation);
	TestTrue(TEXT("the rest between waves, of wave 2"), Machine.IsRestBetweenWaves() && Machine.GetWaveIndex() == 2);
	TestFalse(TEXT("no Start Battle offered"), Machine.OffersStartBattle());
	TestEqual(TEXT("the world runs (no stopped panel)"), Machine.GetTimeDilation(), 1.f);
	TestEqual(TEXT("the level's rest seconds"), Machine.GetPreparationTimeRemaining(), 20.f);
	TestEqual(TEXT("pause charges back"), Machine.GetPauseCharges(), Config.TacticalPauseMaxCharges);
	TestTrue(TEXT("the rest counts as an active wave (cold drain, Susanin)"), Machine.IsWaveActive());
	TestEqual(TEXT("nothing to click: AdvanceAfterWave refused"), Machine.AdvanceAfterWave(), EGameFlowResult::WrongPhase);
	TickFor(Machine, 19.f);
	TestEqual(TEXT("19 s: still resting"), Machine.GetPhase(), ECodexGamePhase::Preparation);
	TestEqual(TEXT("... 1 s left"), Machine.GetPreparationTimeRemaining(), 1.f, 0.05f);
	TickFor(Machine, 1.2f);
	TestEqual(TEXT("20 s: wave 2 breaks through on its own"), Machine.GetPhase(), ECodexGamePhase::WaveCombat);
	TestTrue(TEXT("... a sudden wave: real time, its horde intro armed"), Machine.GetCombatMode() == ECodexCombatMode::RealTime
		&& Machine.IsAwaitingHordeIntro() && Machine.GetWaveIndex() == 2);
	Machine.CancelHordeIntro();
	Machine.NotifyWaveCleared();
	TickFor(Machine, 20.2f);
	TestTrue(TEXT("wave 3 came by itself too"), Machine.GetPhase() == ECodexGamePhase::WaveCombat && Machine.GetWaveIndex() == 3);
	TestTrue(TEXT("wave 3 is the final fight"), Machine.IsFinalFight());
	Machine.NotifyWaveCleared();
	TestEqual(TEXT("the last wave still ends the fight"), Machine.GetPhase(), ECodexGamePhase::PostCombat);
	TickFor(Machine, 30.f);
	TestEqual(TEXT("... and stays over (no 4th wave)"), Machine.GetPhase(), ECodexGamePhase::PostCombat);
	TestFalse(TEXT("the victory panel phase never came"), Phases.Contains(ECodexGamePhase::WaveCleared));
	return true;
}

AUTO_NEXT_WAVE_TEST(FAutoNextWavePreparedTest, "PreparedStartKeepsItsFirstButtonOnly")
bool FAutoNextWavePreparedTest::RunTest(const FString&)
{
	// A level with the old prepared start (cutscene -> preparation with «Start Battle» -> wave 1): the first fight start stays as
	// designed; between the waves there is no button, the rest just runs out.
	FGameFlowConfig Config;
	Config.bSuddenWaveStart = false;
	Config.TotalWaves = 2;
	FGameFlowStateMachine Machine(Config);
	Machine.TriggerCombatZone();
	Machine.FinishCutscene();
	TestTrue(TEXT("the first preparation offers Start Battle"), Machine.OffersStartBattle() && !Machine.IsRestBetweenWaves());
	TestEqual(TEXT("Start Battle"), Machine.FinishPreparation(), EGameFlowResult::Ok);
	Machine.NotifyWaveCleared();
	TestTrue(TEXT("wave 1 cleared: the rest, no Start Battle"), Machine.IsRestBetweenWaves() && !Machine.OffersStartBattle());
	TickFor(Machine, Config.WaveRestDuration + 0.2f);
	TestTrue(TEXT("wave 2 came by itself (no horde intro: prepared)"), Machine.GetPhase() == ECodexGamePhase::WaveCombat
		&& Machine.GetWaveIndex() == 2 && !Machine.IsAwaitingHordeIntro());
	Machine.NotifyWaveCleared();
	TestEqual(TEXT("the last wave: PostCombat"), Machine.GetPhase(), ECodexGamePhase::PostCombat);
	return true;
}

AUTO_NEXT_WAVE_TEST(FAutoNextWaveEdgesTest, "AmbushPanelOptionAndRestRules")
bool FAutoNextWaveEdgesTest::RunTest(const FString&)
{
	// An ambush fight that is the whole battle never rests: straight to PostCombat (2026-10-22).
	FGameFlowConfig Config;
	Config.TotalWaves = 3;
	FGameFlowStateMachine Ambush(Config);
	Ambush.StartAmbushCombat();
	Ambush.NotifyWaveCleared();
	TestEqual(TEXT("single ambush fight: PostCombat"), Ambush.GetPhase(), ECodexGamePhase::PostCombat);

	// The Godot panel on request.
	FGameFlowConfig Godot = Config;
	Godot.bVictoryPanelBetweenWaves = true;
	FGameFlowStateMachine Old(Godot);
	Old.TriggerCombatZone();
	Old.NotifyWaveCleared();
	TestEqual(TEXT("bVictoryPanelBetweenWaves: the panel (time stopped)"), Old.GetPhase(), ECodexGamePhase::WaveCleared);
	TestEqual(TEXT("... stopped"), Old.GetTimeDilation(), 0.f);
	TestEqual(TEXT("... its button"), Old.AdvanceAfterWave(), EGameFlowResult::Ok);

	// A wave cleared from the tactical pause / the turn-based fight rests the same way.
	FGameFlowStateMachine Paused(Config);
	Paused.TriggerCombatZone();
	Paused.EnterFreeTacticalPause();
	Paused.NotifyWaveCleared();
	TestTrue(TEXT("cleared in the free pause: the rest, pause gone"), Paused.IsRestBetweenWaves() && !Paused.IsFreePause()
		&& Paused.GetCombatMode() == ECodexCombatMode::None);
	TestEqual(TEXT("no tactical pause in the rest"), Paused.ToggleTacticalPause(), EGameFlowResult::NotInWave);

	// A zero rest: the next wave on the next tick (never inside NotifyWaveCleared - the wave system finishes its clear first).
	FGameFlowConfig NoRest = Config;
	NoRest.WaveRestDuration = 0.f;
	FGameFlowStateMachine Quick(NoRest);
	Quick.TriggerCombatZone();
	Quick.NotifyWaveCleared();
	TestEqual(TEXT("rest 0: still Preparation right after the clear"), Quick.GetPhase(), ECodexGamePhase::Preparation);
	Quick.Tick(0.016f);
	TestTrue(TEXT("... the wave on the next tick"), Quick.GetPhase() == ECodexGamePhase::WaveCombat && Quick.GetWaveIndex() == 2);

	// Game over in the rest stops it.
	FGameFlowStateMachine Over(Config);
	Over.TriggerCombatZone();
	Over.NotifyWaveCleared();
	Over.TriggerGameOver();
	TickFor(Over, 25.f);
	TestEqual(TEXT("game over in the rest: no wave comes"), Over.GetPhase(), ECodexGamePhase::GameOver);

	// The rest's neighbours: the provocation of the fight ends, the gate does not count the final wave cleared.
	TestTrue(TEXT("the rest ends a provocation"), FirePostureRules::ClearsProvocation(ECodexGamePhase::Preparation));
	TestFalse(TEXT("the rest before the final wave is no final clear"), GateProgressRules::IsFinalWaveCleared(ECodexGamePhase::Preparation, 3, 3));
	return true;
}

#undef AUTO_NEXT_WAVE_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
