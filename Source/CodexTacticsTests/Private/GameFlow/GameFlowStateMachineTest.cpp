#include "Misc/AutomationTest.h"
#include "GameFlow/GameFlowStateMachine.h"

#if WITH_DEV_AUTOMATION_TESTS

// Rules mirror Godot Scenes/movements/main.gd, plus the 2026-09-28 decision that turn-based
// combat starts only from WaveCombat/RealTime.

#define GAMEFLOW_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.GameFlow." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace GameFlowTest
{
	/** Drives a fresh machine into WaveCombat/RealTime of wave 1. */
	void EnterFirstWave(FGameFlowStateMachine& Machine)
	{
		Machine.TriggerCombatZone();
		Machine.FinishCutscene();
		Machine.FinishPreparation();
	}

	/** Drives a fresh machine into Preparation of wave 1. */
	void EnterFirstPreparation(FGameFlowStateMachine& Machine)
	{
		Machine.TriggerCombatZone();
		Machine.FinishCutscene();
	}
}

using namespace GameFlowTest;

// --- Mission phases ---

GAMEFLOW_TEST(FGameFlowStartsInExplorationTest, "Phases.StartsInExploration")
bool FGameFlowStartsInExplorationTest::RunTest(const FString&)
{
	const FGameFlowStateMachine Machine;
	TestEqual(TEXT("Phase"), Machine.GetPhase(), ECodexGamePhase::Exploration);
	TestEqual(TEXT("Mode"), Machine.GetCombatMode(), ECodexCombatMode::None);
	TestEqual(TEXT("Wave index"), Machine.GetWaveIndex(), 0);
	TestEqual(TEXT("Time dilation"), Machine.GetTimeDilation(), 1.f);
	return true;
}

GAMEFLOW_TEST(FGameFlowCombatZoneOnceTest, "Phases.CombatZoneTriggersCutsceneOnce")
bool FGameFlowCombatZoneOnceTest::RunTest(const FString&)
{
	FGameFlowStateMachine Machine;
	TestEqual(TEXT("First trigger"), Machine.TriggerCombatZone(), EGameFlowResult::Ok);
	TestEqual(TEXT("Phase"), Machine.GetPhase(), ECodexGamePhase::Cutscene);
	TestEqual(TEXT("Second trigger during cutscene"), Machine.TriggerCombatZone(), EGameFlowResult::WrongPhase);
	return true;
}

GAMEFLOW_TEST(FGameFlowCutsceneToPreparationTest, "Phases.CutsceneStartsFirstPreparation")
bool FGameFlowCutsceneToPreparationTest::RunTest(const FString&)
{
	FGameFlowStateMachine Machine;
	EnterFirstPreparation(Machine);
	TestEqual(TEXT("Phase"), Machine.GetPhase(), ECodexGamePhase::Preparation);
	TestEqual(TEXT("Wave index"), Machine.GetWaveIndex(), 1);
	TestEqual(TEXT("Preparation time"), Machine.GetPreparationTimeRemaining(), Machine.GetConfig().PreparationDuration);
	return true;
}

GAMEFLOW_TEST(FGameFlowPreparationTimerStartsWaveTest, "Phases.PreparationTimerStartsWave")
bool FGameFlowPreparationTimerStartsWaveTest::RunTest(const FString&)
{
	FGameFlowStateMachine Machine;
	EnterFirstPreparation(Machine);
	const float Duration = Machine.GetConfig().PreparationDuration;
	Machine.Tick(Duration - 0.5f);
	TestEqual(TEXT("Still preparing"), Machine.GetPhase(), ECodexGamePhase::Preparation);
	Machine.Tick(1.f);
	TestEqual(TEXT("Wave started"), Machine.GetPhase(), ECodexGamePhase::WaveCombat);
	TestEqual(TEXT("Real-time mode"), Machine.GetCombatMode(), ECodexCombatMode::RealTime);
	return true;
}

GAMEFLOW_TEST(FGameFlowReadyButtonStartsWaveTest, "Phases.ReadyButtonStartsWave")
bool FGameFlowReadyButtonStartsWaveTest::RunTest(const FString&)
{
	FGameFlowStateMachine Machine;
	EnterFirstPreparation(Machine);
	TestEqual(TEXT("Finish preparation"), Machine.FinishPreparation(), EGameFlowResult::Ok);
	TestEqual(TEXT("Phase"), Machine.GetPhase(), ECodexGamePhase::WaveCombat);
	TestEqual(TEXT("Charges"), Machine.GetPauseCharges(), Machine.GetConfig().TacticalPauseMaxCharges);
	return true;
}

GAMEFLOW_TEST(FGameFlowWaveClearedStopsTimeTest, "Phases.WaveClearedStopsTime")
bool FGameFlowWaveClearedStopsTimeTest::RunTest(const FString&)
{
	FGameFlowStateMachine Machine;
	EnterFirstWave(Machine);
	TestEqual(TEXT("Clear"), Machine.NotifyWaveCleared(), EGameFlowResult::Ok);
	TestEqual(TEXT("Phase"), Machine.GetPhase(), ECodexGamePhase::WaveCleared);
	TestEqual(TEXT("Mode"), Machine.GetCombatMode(), ECodexCombatMode::None);
	TestEqual(TEXT("Time dilation"), Machine.GetTimeDilation(), 0.f);
	return true;
}

GAMEFLOW_TEST(FGameFlowWaveClearedFromTurnBasedTest, "Phases.WaveClearedFromTurnBased")
bool FGameFlowWaveClearedFromTurnBasedTest::RunTest(const FString&)
{
	FGameFlowStateMachine Machine;
	EnterFirstWave(Machine);
	Machine.RequestEnterTurnBased(true);
	TestEqual(TEXT("Clear during turn-based"), Machine.NotifyWaveCleared(), EGameFlowResult::Ok);
	TestEqual(TEXT("Phase"), Machine.GetPhase(), ECodexGamePhase::WaveCleared);
	return true;
}

GAMEFLOW_TEST(FGameFlowNextWaveRestTest, "Phases.NextWaveIsRestPreparation")
bool FGameFlowNextWaveRestTest::RunTest(const FString&)
{
	FGameFlowStateMachine Machine;
	EnterFirstWave(Machine);
	Machine.NotifyWaveCleared();
	TestEqual(TEXT("Advance"), Machine.AdvanceAfterWave(), EGameFlowResult::Ok);
	TestEqual(TEXT("Phase"), Machine.GetPhase(), ECodexGamePhase::Preparation);
	TestEqual(TEXT("Wave index"), Machine.GetWaveIndex(), 2);
	TestEqual(TEXT("Rest time"), Machine.GetPreparationTimeRemaining(), Machine.GetConfig().WaveRestDuration);
	TestEqual(TEXT("No pause in rest"), Machine.ToggleTacticalPause(), EGameFlowResult::NotInWave);
	return true;
}

GAMEFLOW_TEST(FGameFlowLastWaveToPostCombatTest, "Phases.LastWaveLeadsToPostCombatAndExploration")
bool FGameFlowLastWaveToPostCombatTest::RunTest(const FString&)
{
	FGameFlowConfig Config;
	Config.TotalWaves = 2;
	FGameFlowStateMachine Machine(Config);
	EnterFirstWave(Machine);
	Machine.NotifyWaveCleared();
	Machine.AdvanceAfterWave();
	Machine.FinishPreparation();
	Machine.NotifyWaveCleared();
	TestEqual(TEXT("Advance after last wave"), Machine.AdvanceAfterWave(), EGameFlowResult::Ok);
	TestEqual(TEXT("Phase"), Machine.GetPhase(), ECodexGamePhase::PostCombat);
	TestEqual(TEXT("Finish post-combat"), Machine.FinishPostCombat(), EGameFlowResult::Ok);
	TestEqual(TEXT("Back to exploration"), Machine.GetPhase(), ECodexGamePhase::Exploration);
	TestEqual(TEXT("Combat zone can trigger again"), Machine.TriggerCombatZone(), EGameFlowResult::Ok);
	return true;
}

GAMEFLOW_TEST(FGameFlowGameOverTest, "Phases.GameOverStopsTime")
bool FGameFlowGameOverTest::RunTest(const FString&)
{
	FGameFlowStateMachine Machine;
	EnterFirstWave(Machine);
	Machine.ToggleTacticalPause();
	Machine.TriggerGameOver();
	TestEqual(TEXT("Phase"), Machine.GetPhase(), ECodexGamePhase::GameOver);
	TestEqual(TEXT("Mode"), Machine.GetCombatMode(), ECodexCombatMode::None);
	TestEqual(TEXT("Time dilation"), Machine.GetTimeDilation(), 0.f);
	return true;
}

// --- Tactical pause ---

GAMEFLOW_TEST(FGameFlowPauseSpendsChargeTest, "TacticalPause.SpendsChargeAndSlowsTime")
bool FGameFlowPauseSpendsChargeTest::RunTest(const FString&)
{
	FGameFlowStateMachine Machine;
	EnterFirstWave(Machine);
	const FGameFlowConfig& Config = Machine.GetConfig();
	TestEqual(TEXT("Pause"), Machine.ToggleTacticalPause(), EGameFlowResult::Ok);
	TestEqual(TEXT("Mode"), Machine.GetCombatMode(), ECodexCombatMode::TacticalPause);
	TestEqual(TEXT("Charges"), Machine.GetPauseCharges(), Config.TacticalPauseMaxCharges - 1);
	TestEqual(TEXT("Pause time"), Machine.GetPauseTimeRemaining(), Config.TacticalPauseDuration);
	TestEqual(TEXT("Time dilation"), Machine.GetTimeDilation(), Config.TacticalPauseTimeDilation);
	return true;
}

GAMEFLOW_TEST(FGameFlowPauseRejectedOutsideWaveTest, "TacticalPause.RejectedOutsideWave")
bool FGameFlowPauseRejectedOutsideWaveTest::RunTest(const FString&)
{
	FGameFlowStateMachine Machine;
	TestEqual(TEXT("Exploration"), Machine.ToggleTacticalPause(), EGameFlowResult::NotInWave);
	EnterFirstPreparation(Machine);
	TestEqual(TEXT("Preparation"), Machine.ToggleTacticalPause(), EGameFlowResult::NotInWave);
	return true;
}

GAMEFLOW_TEST(FGameFlowPauseReleaseEventTest, "TacticalPause.ReleaseRunsOrdersByToggleAndTimeout")
bool FGameFlowPauseReleaseEventTest::RunTest(const FString&)
{
	FGameFlowStateMachine Machine;
	int32 Releases = 0;
	Machine.OnTacticalPauseReleased.AddLambda([&Releases]() { ++Releases; });
	EnterFirstWave(Machine);

	Machine.ToggleTacticalPause();
	TestEqual(TEXT("Manual release"), Machine.ToggleTacticalPause(), EGameFlowResult::Ok);
	TestEqual(TEXT("Mode after manual release"), Machine.GetCombatMode(), ECodexCombatMode::RealTime);
	TestEqual(TEXT("Release events"), Releases, 1);

	Machine.ToggleTacticalPause();
	Machine.Tick(Machine.GetConfig().TacticalPauseDuration - 0.5f);
	TestEqual(TEXT("Still paused"), Machine.GetCombatMode(), ECodexCombatMode::TacticalPause);
	Machine.Tick(1.f);
	TestEqual(TEXT("Mode after timeout"), Machine.GetCombatMode(), ECodexCombatMode::RealTime);
	TestEqual(TEXT("Release events"), Releases, 2);
	return true;
}

GAMEFLOW_TEST(FGameFlowPauseChargesAndCooldownTest, "TacticalPause.ChargesExhaustedStartCooldown")
bool FGameFlowPauseChargesAndCooldownTest::RunTest(const FString&)
{
	FGameFlowStateMachine Machine;
	EnterFirstWave(Machine);
	const FGameFlowConfig& Config = Machine.GetConfig();
	for (int32 Index = 0; Index < Config.TacticalPauseMaxCharges; ++Index)
	{
		TestEqual(TEXT("Pause"), Machine.ToggleTacticalPause(), EGameFlowResult::Ok);
		TestEqual(TEXT("Release"), Machine.ToggleTacticalPause(), EGameFlowResult::Ok);
	}
	TestEqual(TEXT("Charges spent"), Machine.GetPauseCharges(), 0);
	TestEqual(TEXT("Cooldown started"), Machine.GetPauseCooldownRemaining(), Config.TacticalPauseCooldown);
	TestEqual(TEXT("Pause on cooldown"), Machine.ToggleTacticalPause(), EGameFlowResult::PauseOnCooldown);

	Machine.Tick(Config.TacticalPauseCooldown + 0.1f);
	TestEqual(TEXT("Charges restored"), Machine.GetPauseCharges(), Config.TacticalPauseMaxCharges);
	TestEqual(TEXT("Pause available again"), Machine.ToggleTacticalPause(), EGameFlowResult::Ok);
	return true;
}

GAMEFLOW_TEST(FGameFlowNoChargesWithoutCooldownTest, "TacticalPause.NoChargesReportedWhenCooldownZero")
bool FGameFlowNoChargesWithoutCooldownTest::RunTest(const FString&)
{
	FGameFlowConfig Config;
	Config.TacticalPauseMaxCharges = 0;
	FGameFlowStateMachine Machine(Config);
	EnterFirstWave(Machine);
	TestEqual(TEXT("No charges"), Machine.ToggleTacticalPause(), EGameFlowResult::NoPauseCharges);
	return true;
}

GAMEFLOW_TEST(FGameFlowNewWaveResetsCountersTest, "TacticalPause.NewWaveResetsCounters")
bool FGameFlowNewWaveResetsCountersTest::RunTest(const FString&)
{
	FGameFlowConfig Config;
	Config.TurnBasedUsesPerWave = 1;
	FGameFlowStateMachine Machine(Config);
	EnterFirstWave(Machine);
	for (int32 Index = 0; Index < Config.TacticalPauseMaxCharges; ++Index)
	{
		Machine.ToggleTacticalPause();
		Machine.ToggleTacticalPause();
	}
	Machine.RequestEnterTurnBased(true);
	Machine.ExitTurnBased();
	Machine.ToggleTacticalPause();
	Machine.NotifyWaveCleared();
	Machine.AdvanceAfterWave();
	Machine.FinishPreparation();

	TestEqual(TEXT("Charges"), Machine.GetPauseCharges(), Config.TacticalPauseMaxCharges);
	TestEqual(TEXT("Cooldown"), Machine.GetPauseCooldownRemaining(), 0.f);
	TestEqual(TEXT("Turn-based uses"), Machine.GetTurnBasedUsesThisWave(), 0);
	TestEqual(TEXT("Turn-based allowed"), Machine.RequestEnterTurnBased(true), EGameFlowResult::Ok);
	return true;
}

GAMEFLOW_TEST(FGameFlowRealTimeTimersTest, "TacticalPause.TimersIgnoreTimeDilation")
bool FGameFlowRealTimeTimersTest::RunTest(const FString&)
{
	// The machine only sees real seconds: 30 real seconds end the pause even at dilation 0.02.
	FGameFlowStateMachine Machine;
	EnterFirstWave(Machine);
	Machine.ToggleTacticalPause();
	for (int32 Frame = 0; Frame < 31 * 60; ++Frame)
	{
		Machine.Tick(1.f / 60.f);
	}
	TestEqual(TEXT("Pause ended after ~30 real seconds"), Machine.GetCombatMode(), ECodexCombatMode::RealTime);
	return true;
}

// --- Turn-based ---

GAMEFLOW_TEST(FGameFlowTurnBasedFromRealTimeTest, "TurnBased.EntersFromRealTime")
bool FGameFlowTurnBasedFromRealTimeTest::RunTest(const FString&)
{
	FGameFlowStateMachine Machine;
	EnterFirstWave(Machine);
	TestEqual(TEXT("Enter"), Machine.RequestEnterTurnBased(true), EGameFlowResult::Ok);
	TestEqual(TEXT("Mode"), Machine.GetCombatMode(), ECodexCombatMode::TurnBased);
	TestEqual(TEXT("Uses"), Machine.GetTurnBasedUsesThisWave(), 1);
	TestEqual(TEXT("Time runs normally"), Machine.GetTimeDilation(), 1.f);
	TestEqual(TEXT("No tactical pause inside turn-based"), Machine.ToggleTacticalPause(), EGameFlowResult::NotInRealTime);
	return true;
}

GAMEFLOW_TEST(FGameFlowTurnBasedRejectedOutsideWaveTest, "TurnBased.RejectedInExplorationAndPreparation")
bool FGameFlowTurnBasedRejectedOutsideWaveTest::RunTest(const FString&)
{
	FGameFlowStateMachine Machine;
	TestEqual(TEXT("Exploration"), Machine.RequestEnterTurnBased(true), EGameFlowResult::NotInWave);
	EnterFirstPreparation(Machine);
	TestEqual(TEXT("Preparation"), Machine.RequestEnterTurnBased(true), EGameFlowResult::NotInWave);
	return true;
}

GAMEFLOW_TEST(FGameFlowTurnBasedRejectedInPauseTest, "TurnBased.RejectedDuringTacticalPause")
bool FGameFlowTurnBasedRejectedInPauseTest::RunTest(const FString&)
{
	// User request 2026-10-06 allows the pause -> turn-based hold by default (CodexTactics.Combat.TimeMode.HoldEntersTurnBased);
	// the TANDEM request 2 rule stays behind bAllowTurnBasedFromTacticalPause = false.
	FGameFlowConfig Config;
	Config.bAllowTurnBasedFromTacticalPause = false;
	FGameFlowStateMachine Machine(Config);
	EnterFirstWave(Machine);
	Machine.ToggleTacticalPause();
	TestEqual(TEXT("From pause"), Machine.RequestEnterTurnBased(true), EGameFlowResult::NotInRealTime);
	TestEqual(TEXT("Still paused"), Machine.GetCombatMode(), ECodexCombatMode::TacticalPause);
	TestEqual(TEXT("No use counted"), Machine.GetTurnBasedUsesThisWave(), 0);
	return true;
}

GAMEFLOW_TEST(FGameFlowTurnBasedNeedsEnemiesTest, "TurnBased.RejectedWithoutEnemiesInRange")
bool FGameFlowTurnBasedNeedsEnemiesTest::RunTest(const FString&)
{
	FGameFlowStateMachine Machine;
	EnterFirstWave(Machine);
	TestEqual(TEXT("No enemies"), Machine.RequestEnterTurnBased(false), EGameFlowResult::NoEnemiesInRange);
	TestEqual(TEXT("Still real-time"), Machine.GetCombatMode(), ECodexCombatMode::RealTime);
	TestEqual(TEXT("No use counted"), Machine.GetTurnBasedUsesThisWave(), 0);
	return true;
}

GAMEFLOW_TEST(FGameFlowTurnBasedLimitTest, "TurnBased.LimitPerWave")
bool FGameFlowTurnBasedLimitTest::RunTest(const FString&)
{
	FGameFlowConfig Config;
	Config.TurnBasedUsesPerWave = 1;
	FGameFlowStateMachine Machine(Config);
	EnterFirstWave(Machine);
	Machine.RequestEnterTurnBased(true);
	Machine.ExitTurnBased();
	Machine.ToggleTacticalPause();
	TestEqual(TEXT("Second entry"), Machine.RequestEnterTurnBased(true), EGameFlowResult::TurnBasedLimitReached);
	return true;
}

GAMEFLOW_TEST(FGameFlowTurnBasedUnlimitedTest, "TurnBased.UnlimitedByDefault")
bool FGameFlowTurnBasedUnlimitedTest::RunTest(const FString&)
{
	FGameFlowStateMachine Machine;
	EnterFirstWave(Machine);
	for (int32 Index = 0; Index < 5; ++Index)
	{
		TestEqual(TEXT("Enter"), Machine.RequestEnterTurnBased(true), EGameFlowResult::Ok);
		Machine.ExitTurnBased();
		Machine.ToggleTacticalPause();
	}
	TestEqual(TEXT("Uses"), Machine.GetTurnBasedUsesThisWave(), 5);
	return true;
}

GAMEFLOW_TEST(FGameFlowTurnBasedExitToFreePauseTest, "TurnBased.ExitGivesFreeTacticalPause")
bool FGameFlowTurnBasedExitToFreePauseTest::RunTest(const FString&)
{
	FGameFlowStateMachine Machine;
	EnterFirstWave(Machine);
	const FGameFlowConfig& Config = Machine.GetConfig();
	Machine.RequestEnterTurnBased(true);
	TestEqual(TEXT("Exit"), Machine.ExitTurnBased(), EGameFlowResult::Ok);
	TestEqual(TEXT("Mode"), Machine.GetCombatMode(), ECodexCombatMode::TacticalPause);
	TestEqual(TEXT("Pause time"), Machine.GetPauseTimeRemaining(), Config.PostTurnBasedPauseDuration);
	TestEqual(TEXT("No charge spent"), Machine.GetPauseCharges(), Config.TacticalPauseMaxCharges);
	TestEqual(TEXT("Exit again"), Machine.ExitTurnBased(), EGameFlowResult::NotInTurnBased);
	return true;
}

// --- Notifications ---

GAMEFLOW_TEST(FGameFlowStateChangedEventTest, "Events.StateChangedFiresOncePerChange")
bool FGameFlowStateChangedEventTest::RunTest(const FString&)
{
	FGameFlowStateMachine Machine;
	TArray<ECodexGamePhase> Phases;
	Machine.OnStateChanged.AddLambda([&Phases](ECodexGamePhase Phase, ECodexCombatMode) { Phases.Add(Phase); });
	EnterFirstWave(Machine);
	Machine.TriggerCombatZone(); // rejected, no event
	const TArray<ECodexGamePhase> Expected = { ECodexGamePhase::Cutscene, ECodexGamePhase::Preparation, ECodexGamePhase::WaveCombat };
	TestEqual(TEXT("Event sequence"), Phases, Expected);
	return true;
}

GAMEFLOW_TEST(FGameFlowTurnBasedKeepsPauseChargesTest, "TurnBased.KeepsPauseChargesAndCooldown")
bool FGameFlowTurnBasedKeepsPauseChargesTest::RunTest(const FString&)
{
	// TANDEM request 2 (user + Gemini 2026-10-01): no turn-based fight from the tactical pause; the fight neither
	// refills the pause charges nor runs their cooldown down. (The pause -> turn-based hold is allowed by default since the
	// RTS request of 2026-10-06; this test keeps the old flag to check the frozen charges.)
	FGameFlowConfig Config;
	Config.bAllowTurnBasedFromTacticalPause = false;
	FGameFlowStateMachine Machine(Config);
	EnterFirstWave(Machine);
	const int32 MaxCharges = Machine.GetPauseCharges();
	TestEqual(TEXT("Pause"), Machine.ToggleTacticalPause(), EGameFlowResult::Ok);
	TestEqual(TEXT("No turn-based from the pause"), Machine.RequestEnterTurnBased(true), EGameFlowResult::NotInRealTime);
	Machine.ToggleTacticalPause();
	for (int32 Use = 1; Use < MaxCharges; ++Use)
	{
		Machine.ToggleTacticalPause();
		Machine.ToggleTacticalPause();
	}
	TestEqual(TEXT("All charges spent"), Machine.GetPauseCharges(), 0);
	const float Cooldown = Machine.GetPauseCooldownRemaining();
	TestTrue(TEXT("Cooldown running"), Cooldown > 0.f);
	Machine.Tick(1.f);
	const float BeforeFight = Machine.GetPauseCooldownRemaining();
	TestEqual(TEXT("Turn-based from real time"), Machine.RequestEnterTurnBased(true), EGameFlowResult::Ok);
	Machine.Tick(600.f); // a long grid fight
	TestEqual(TEXT("Charges frozen in the fight"), Machine.GetPauseCharges(), 0);
	TestEqual(TEXT("Cooldown frozen in the fight"), Machine.GetPauseCooldownRemaining(), BeforeFight, 0.001f);
	TestEqual(TEXT("Exit"), Machine.ExitTurnBased(), EGameFlowResult::Ok);
	TestEqual(TEXT("Charges after the fight"), Machine.GetPauseCharges(), 0);
	TestEqual(TEXT("Cooldown after the fight"), Machine.GetPauseCooldownRemaining(), BeforeFight, 0.001f);
	return true;
}

#undef GAMEFLOW_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
