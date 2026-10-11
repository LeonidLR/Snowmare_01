#include "Misc/AutomationTest.h"
#include "Data/LevelJsonRules.h"
#include "Data/WaveConfigTypes.h"
#include "Quests/GateProgressRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Blast gate tied to the generator during the wave fight (user request 2026-10-11): the door creeps open only while the
// generator works, pauses while it is down, never opens fully before the final wave is cleared, opens after it.

#define GATE_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Quests.GateProgress." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace GateProgressTest
{
	/** Unlocked door, generator up, wave Wave of 3 running. */
	FGateProgressInputs Fighting(int32 Wave, bool bGenerator = true)
	{
		FGateProgressInputs Inputs;
		Inputs.bUnlocked = true;
		Inputs.bGeneratorWorking = bGenerator;
		Inputs.bFightRunning = true;
		Inputs.WaveIndex = Wave;
		Inputs.FinalWave = 3;
		return Inputs;
	}
}

GATE_TEST(FGateProgressOnlyWithGeneratorTest, "ProgressOnlyWithGenerator")
bool FGateProgressOnlyWithGeneratorTest::RunTest(const FString&)
{
	const FGateProgressConfig Config; // defaults: generator_waves, 0.004 / s, 90 % before the final wave
	TestEqual(TEXT("default mode"), Config.Mode, EGateOpenMode::GeneratorWaves);

	const FGateProgressStep Up = GateProgressRules::Step(Config, 0.1f, GateProgressTest::Fighting(1), 10.f);
	TestEqual(TEXT("generator up: +0.004 / s for 10 s"), Up.Progress, 0.14f, 1e-4f);
	TestEqual(TEXT("advancing"), Up.Status, EGateProgressStatus::Advancing);
	TestFalse(TEXT("not open"), Up.bOpenFully);

	const FGateProgressStep Down = GateProgressRules::Step(Config, 0.1f, GateProgressTest::Fighting(1, false), 10.f);
	TestEqual(TEXT("generator down: no progress"), Down.Progress, 0.1f);
	TestEqual(TEXT("no power"), Down.Status, EGateProgressStatus::NoPower);

	FGateProgressInputs Locked = GateProgressTest::Fighting(1);
	Locked.bUnlocked = false;
	const FGateProgressStep Before = GateProgressRules::Step(Config, 0.f, Locked, 10.f);
	TestEqual(TEXT("terminal not powered: no progress"), Before.Progress, 0.f);
	TestEqual(TEXT("locked"), Before.Status, EGateProgressStatus::Locked);

	FGateProgressInputs Calm = GateProgressTest::Fighting(1);
	Calm.bFightRunning = false;
	const FGateProgressStep Idle = GateProgressRules::Step(Config, 0.05f, Calm, 10.f);
	TestEqual(TEXT("no fight: no progress"), Idle.Progress, 0.05f);
	TestEqual(TEXT("idle"), Idle.Status, EGateProgressStatus::Idle);

	TestTrue(TEXT("real time is a fight"), GateProgressRules::IsFightRunning(ECodexGamePhase::WaveCombat, ECodexCombatMode::RealTime));
	TestTrue(TEXT("tactical pause is a fight (dilated time)"), GateProgressRules::IsFightRunning(ECodexGamePhase::WaveCombat, ECodexCombatMode::TacticalPause));
	TestFalse(TEXT("turn-based grid is not"), GateProgressRules::IsFightRunning(ECodexGamePhase::WaveCombat, ECodexCombatMode::TurnBased));
	TestFalse(TEXT("preparation is not"), GateProgressRules::IsFightRunning(ECodexGamePhase::Preparation, ECodexCombatMode::None));
	return true;
}

GATE_TEST(FGateProgressPausesTest, "PausesWhileGeneratorDown")
bool FGateProgressPausesTest::RunTest(const FString&)
{
	const FGateProgressConfig Config;
	float Progress = 0.f;
	for (int32 Second = 0; Second < 20; ++Second)
	{
		Progress = GateProgressRules::Step(Config, Progress, GateProgressTest::Fighting(1), 1.f).Progress;
	}
	const float AtBreakdown = Progress;
	TestTrue(TEXT("crept open before the breakdown"), AtBreakdown > 0.07f);
	for (int32 Second = 0; Second < 600; ++Second)
	{
		Progress = GateProgressRules::Step(Config, Progress, GateProgressTest::Fighting(1, false), 1.f).Progress;
	}
	TestEqual(TEXT("10 min without power: paused, not reversed"), Progress, AtBreakdown);
	Progress = GateProgressRules::Step(Config, Progress, GateProgressTest::Fighting(1), 5.f).Progress;
	TestEqual(TEXT("repaired: resumes from where it stopped"), Progress, AtBreakdown + 0.02f, 1e-4f);
	return true;
}

GATE_TEST(FGateProgressNeverFullBeforeFinalTest, "NeverFullBeforeFinalWave")
bool FGateProgressNeverFullBeforeFinalTest::RunTest(const FString&)
{
	const FGateProgressConfig Config;
	const float Caps[] = { 0.3f, 0.6f, 0.9f };
	float Progress = 0.f;
	for (int32 Wave = 1; Wave <= 3; ++Wave)
	{
		// A very long wave with the generator up all the time.
		FGateProgressStep Step;
		for (int32 Second = 0; Second < 3600; ++Second)
		{
			Step = GateProgressRules::Step(Config, Progress, GateProgressTest::Fighting(Wave), 1.f);
			Progress = Step.Progress;
			if (Step.bOpenFully)
			{
				break;
			}
		}
		TestFalse(FString::Printf(TEXT("wave %d: never opens fully"), Wave), Step.bOpenFully);
		TestEqual(FString::Printf(TEXT("wave %d: held at its cap"), Wave), Progress, Caps[Wave - 1], 1e-4f);
		TestEqual(FString::Printf(TEXT("wave %d: holding for the wave"), Wave), Step.Status, EGateProgressStatus::HoldingForWave);
		TestEqual(FString::Printf(TEXT("wave %d: cap"), Wave), GateProgressRules::GetProgressCap(Config, Wave, 3, false), Caps[Wave - 1], 1e-4f);
	}
	// Data asking for "full" before the final wave is clamped below 1.
	FGateProgressConfig Greedy;
	Greedy.MaxProgressBeforeFinal = 5.f;
	TestTrue(TEXT("cap stays below 1"), GateProgressRules::GetProgressCap(Greedy, 3, 3, false) < 1.f);
	TestFalse(TEXT("between waves the final one is not cleared"), GateProgressRules::IsFinalWaveCleared(ECodexGamePhase::WaveCleared, 2, 3));
	TestFalse(TEXT("during the final wave it is not cleared"), GateProgressRules::IsFinalWaveCleared(ECodexGamePhase::WaveCombat, 3, 3));
	return true;
}

GATE_TEST(FGateProgressFullAfterFinalTest, "FullAfterFinalWaveWithGenerator")
bool FGateProgressFullAfterFinalTest::RunTest(const FString&)
{
	const FGateProgressConfig Config;
	TestTrue(TEXT("final wave's victory panel"), GateProgressRules::IsFinalWaveCleared(ECodexGamePhase::WaveCleared, 3, 3));
	TestTrue(TEXT("post-combat"), GateProgressRules::IsFinalWaveCleared(ECodexGamePhase::PostCombat, 3, 3));

	FGateProgressInputs Cleared = GateProgressTest::Fighting(3);
	Cleared.bFightRunning = false;
	Cleared.bFinalWaveCleared = true;
	const FGateProgressStep Open = GateProgressRules::Step(Config, 0.9f, Cleared, 0.f);
	TestTrue(TEXT("final wave cleared + generator: opens fully"), Open.bOpenFully);
	TestEqual(TEXT("progress 1"), Open.Progress, 1.f);
	TestEqual(TEXT("opening"), Open.Status, EGateProgressStatus::Opening);

	Cleared.bGeneratorWorking = false;
	const FGateProgressStep Waiting = GateProgressRules::Step(Config, 0.9f, Cleared, 30.f);
	TestFalse(TEXT("final wave cleared, generator down: waits"), Waiting.bOpenFully);
	TestEqual(TEXT("waits at its progress"), Waiting.Progress, 0.9f);
	TestEqual(TEXT("no power"), Waiting.Status, EGateProgressStatus::NoPower);

	// gate_full_open_after_wave.
	FGateProgressConfig AfterTwo;
	AfterTwo.FullOpenAfterWave = 2;
	TestEqual(TEXT("final wave 0 = the level's last"), GateProgressRules::ResolveFinalWave(Config, 3), 3);
	TestEqual(TEXT("final wave 2 of 3"), GateProgressRules::ResolveFinalWave(AfterTwo, 3), 2);
	AfterTwo.FullOpenAfterWave = 7;
	TestEqual(TEXT("final wave beyond the level clamps to its last"), GateProgressRules::ResolveFinalWave(AfterTwo, 3), 3);
	TestTrue(TEXT("a later wave than the door's one"), GateProgressRules::IsFinalWaveCleared(ECodexGamePhase::WaveCombat, 3, 2));

	// The instant mode (Godot gate.gd): the powered terminal opens it at once.
	FGateProgressConfig Instant;
	Instant.Mode = EGateOpenMode::Instant;
	FGateProgressInputs Powered;
	Powered.bUnlocked = true;
	TestTrue(TEXT("instant: opens at once"), GateProgressRules::Step(Instant, 0.f, Powered, 0.f).bOpenFully);
	return true;
}

GATE_TEST(FGateProgressTextsTest, "StatusAndRadioTexts")
bool FGateProgressTextsTest::RunTest(const FString&)
{
	TestEqual(TEXT("HUD line"), GateProgressRules::GetStatusText(EGateProgressStatus::NoPower, 0.375f), FString(TEXT("BLAST DOOR 37% — GENERATOR OFFLINE")));
	TestTrue(TEXT("advancing line"), GateProgressRules::GetStatusText(EGateProgressStatus::Advancing, 0.3f).StartsWith(TEXT("BLAST DOOR 30%")));
	TestFalse(TEXT("hidden while locked"), GateProgressRules::ShouldShowStatus(EGateProgressStatus::Locked));
	TestFalse(TEXT("hidden once open"), GateProgressRules::ShouldShowStatus(EGateProgressStatus::Open));
	TestTrue(TEXT("shown without power"), GateProgressRules::ShouldShowStatus(EGateProgressStatus::NoPower));
	TestTrue(TEXT("beacon spins while unlocked and powered"), GateProgressRules::ShouldBeaconSpin(true, false, false, true));
	TestFalse(TEXT("beacon stops without power"), GateProgressRules::ShouldBeaconSpin(true, false, false, false));
	TestFalse(TEXT("beacon off while the servos are locked"), GateProgressRules::ShouldBeaconSpin(false, false, false, true));
	TestTrue(TEXT("beacon spins while the leaves slide open"), GateProgressRules::ShouldBeaconSpin(true, false, true, false));
	TestFalse(TEXT("beacon off once open"), GateProgressRules::ShouldBeaconSpin(true, true, false, true));

	FText Speaker;
	FText Text;
	TestTrue(TEXT("unlock line"), GateProgressRules::GetRadioLine(EGateProgressStatus::Locked, EGateProgressStatus::Idle, false, Speaker, Text)
		&& Text.ToString().Contains(TEXT("overloaded")) && Speaker.ToString() == TEXT("HQ"));
	TestTrue(TEXT("breakdown line"), GateProgressRules::GetRadioLine(EGateProgressStatus::Advancing, EGateProgressStatus::NoPower, false, Speaker, Text)
		&& Text.ToString().Contains(TEXT("generator is down")));
	TestTrue(TEXT("last wave, no power line"), GateProgressRules::GetRadioLine(EGateProgressStatus::HoldingForWave, EGateProgressStatus::NoPower, true, Speaker, Text)
		&& Text.ToString().Contains(TEXT("last wave")));
	TestTrue(TEXT("power back line"), GateProgressRules::GetRadioLine(EGateProgressStatus::NoPower, EGateProgressStatus::Advancing, false, Speaker, Text)
		&& Text.ToString().Contains(TEXT("Power is back")));
	TestTrue(TEXT("opening line"), GateProgressRules::GetRadioLine(EGateProgressStatus::HoldingForWave, EGateProgressStatus::Opening, true, Speaker, Text)
		&& Text.ToString().Contains(TEXT("opening")));
	TestFalse(TEXT("no line without a change"), GateProgressRules::GetRadioLine(EGateProgressStatus::Advancing, EGateProgressStatus::Advancing, false, Speaker, Text));
	TestFalse(TEXT("no line for a wave starting"), GateProgressRules::GetRadioLine(EGateProgressStatus::Idle, EGateProgressStatus::Advancing, false, Speaker, Text));
	TestFalse(TEXT("no line when opened outright («Start Battle»)"), GateProgressRules::GetRadioLine(EGateProgressStatus::Locked, EGateProgressStatus::Opening, false, Speaker, Text));

	// The quest lines no longer promise an instantly opening gate.
	FQuestChainState Chain;
	Chain.bIsGeneratorRunning = true;
	FQuestInteractionResult Terminal = Chain.Interact(EInteractableType::GateTerminal);
	GateProgressRules::AdjustQuestLine(EInteractableType::GateTerminal, Chain.bIsGatePowered, false, Terminal);
	TestEqual(TEXT("terminal still unlocks the gate"), Terminal.Event, EQuestEvent::GateOpened);
	TestFalse(TEXT("no «swing open»"), Terminal.Text.ToString().Contains(TEXT("swing open")));
	FQuestInteractionResult Gate = Chain.Interact(EInteractableType::Gate);
	GateProgressRules::AdjustQuestLine(EInteractableType::Gate, Chain.bIsGatePowered, false, Gate);
	TestFalse(TEXT("no «The gate is open»"), Gate.Text.ToString().Contains(TEXT("The gate is open")));
	FQuestInteractionResult OpenGate = Chain.Interact(EInteractableType::Gate);
	GateProgressRules::AdjustQuestLine(EInteractableType::Gate, Chain.bIsGatePowered, true, OpenGate);
	TestTrue(TEXT("once open the Godot line stays"), OpenGate.Text.ToString().Contains(TEXT("The gate is open")));
	return true;
}

GATE_TEST(FGateProgressLevelJsonTest, "LevelJsonKeys")
bool FGateProgressLevelJsonTest::RunTest(const FString&)
{
	FLevelCombatConfig Defaults;
	FString Error;
	TestTrue(TEXT("no gate keys parse"), LevelJsonRules::ParseLevel(TEXT("{\"waves\": []}"), TEXT("t"), Defaults, Error));
	TestEqual(TEXT("default mode"), Defaults.Gate.Mode, EGateOpenMode::GeneratorWaves);
	TestEqual(TEXT("default rate"), Defaults.Gate.ProgressPerSecond, 0.004f);
	TestEqual(TEXT("default cap"), Defaults.Gate.MaxProgressBeforeFinal, 0.9f);
	TestEqual(TEXT("default final wave"), Defaults.Gate.FullOpenAfterWave, 0);

	FLevelCombatConfig Custom;
	TestTrue(TEXT("gate keys parse"), LevelJsonRules::ParseLevel(TEXT("{\"gate_open_mode\": \"INSTANT\", \"gate_progress_per_second\": 0.01, ")
		TEXT("\"gate_max_progress_before_final\": 1.5, \"gate_full_open_after_wave\": 2, \"waves\": []}"), TEXT("t"), Custom, Error));
	TestEqual(TEXT("mode"), Custom.Gate.Mode, EGateOpenMode::Instant);
	TestEqual(TEXT("rate"), Custom.Gate.ProgressPerSecond, 0.01f);
	TestEqual(TEXT("cap clamped below full"), Custom.Gate.MaxProgressBeforeFinal, 0.99f);
	TestEqual(TEXT("final wave"), Custom.Gate.FullOpenAfterWave, 2);

	FLevelCombatConfig Bad;
	TestFalse(TEXT("unknown mode rejected"), LevelJsonRules::ParseLevel(TEXT("{\"gate_open_mode\": \"slow\", \"waves\": []}"), TEXT("t"), Bad, Error));
	TestTrue(TEXT("error names the key"), Error.Contains(TEXT("gate_open_mode")));

	FLevelCombatConfig Active;
	TestTrue(TEXT("level_01_outpost.json loads"), LevelJsonRules::LoadLevel(TEXT("level_01_outpost.json"), Active, Error));
	TestEqual(TEXT("level_01: generator_waves"), Active.Gate.Mode, EGateOpenMode::GeneratorWaves);
	TestEqual(TEXT("level_01: opens after its last (3rd) wave"), GateProgressRules::ResolveFinalWave(Active.Gate, Active.Waves.Num()), 3);
	return true;
}

#undef GATE_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
