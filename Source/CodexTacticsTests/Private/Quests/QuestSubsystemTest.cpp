#include "Misc/AutomationTest.h"
#include "GameFlow/GameFlowStateMachine.h"
#include "Quests/QuestChain.h"

#if WITH_DEV_AUTOMATION_TESTS

// Checkpoint quest chain, architect sprint spec (docs/port/TANDEM.md) + Godot quest_manager.gd parity.

#define QUEST_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Quests." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

QUEST_TEST(FQuestTerminalWithoutGeneratorTest, "TerminalWithoutGeneratorRefuses")
bool FQuestTerminalWithoutGeneratorTest::RunTest(const FString&)
{
	FQuestChainState State;
	const FQuestInteractionResult Result = State.Interact(EInteractableType::GateTerminal);
	TestEqual(TEXT("No event"), Result.Event, EQuestEvent::None);
	TestFalse(TEXT("Gate not powered"), State.bIsGatePowered);
	TestTrue(TEXT("Terminal line"), Result.Text.ToString().Contains(TEXT("обесточена")));
	return true;
}

QUEST_TEST(FQuestVehicleWithoutCanisterTest, "VehicleWithoutCanisterRefuses")
bool FQuestVehicleWithoutCanisterTest::RunTest(const FString&)
{
	FQuestChainState State;
	const FQuestInteractionResult Result = State.Interact(EInteractableType::Vehicle);
	TestFalse(TEXT("No fuel"), State.bHasFuelCanister);
	TestEqual(TEXT("Medic speaks"), Result.Speaker.ToString(), FString(TEXT("Медик")));
	return true;
}

QUEST_TEST(FQuestCanisterPickupTest, "CanisterPickup")
bool FQuestCanisterPickupTest::RunTest(const FString&)
{
	FQuestChainState State;
	const FQuestInteractionResult Result = State.Interact(EInteractableType::Canister);
	TestTrue(TEXT("Empty canister"), State.bHasEmptyCanister);
	TestEqual(TEXT("Hide canister"), Result.Event, EQuestEvent::CanisterPickedUp);
	const FQuestInteractionResult Again = State.Interact(EInteractableType::Canister);
	TestEqual(TEXT("Second pickup does nothing"), Again.Event, EQuestEvent::None);
	return true;
}

QUEST_TEST(FQuestDrainFuelTest, "DrainFuelFromVehicle")
bool FQuestDrainFuelTest::RunTest(const FString&)
{
	FQuestChainState State;
	State.Interact(EInteractableType::Canister);
	State.Interact(EInteractableType::Vehicle);
	TestTrue(TEXT("Fuel canister"), State.bHasFuelCanister);
	TestFalse(TEXT("No empty canister"), State.bHasEmptyCanister);
	return true;
}

QUEST_TEST(FQuestGeneratorTest, "GeneratorNeedsFuelThenStarts")
bool FQuestGeneratorTest::RunTest(const FString&)
{
	FQuestChainState State;
	TestEqual(TEXT("Dry generator"), State.Interact(EInteractableType::Generator).Event, EQuestEvent::None);
	State.Interact(EInteractableType::Canister);
	State.Interact(EInteractableType::Vehicle);
	TestEqual(TEXT("Generator starts"), State.Interact(EInteractableType::Generator).Event, EQuestEvent::GeneratorStarted);
	TestTrue(TEXT("Running"), State.bIsGeneratorRunning);
	TestFalse(TEXT("Fuel used"), State.bHasFuelCanister);
	TestEqual(TEXT("Running generator: no second start"), State.Interact(EInteractableType::Generator).Event, EQuestEvent::None);
	return true;
}

QUEST_TEST(FQuestGateTerminalTest, "TerminalOpensGateOnce")
bool FQuestGateTerminalTest::RunTest(const FString&)
{
	FQuestChainState State;
	State.Interact(EInteractableType::Canister);
	State.Interact(EInteractableType::Vehicle);
	State.Interact(EInteractableType::Generator);
	TestEqual(TEXT("Gate opens"), State.Interact(EInteractableType::GateTerminal).Event, EQuestEvent::GateOpened);
	TestTrue(TEXT("Powered"), State.bIsGatePowered);
	TestEqual(TEXT("Only once"), State.Interact(EInteractableType::GateTerminal).Event, EQuestEvent::None);
	return true;
}

QUEST_TEST(FQuestObjectiveProgressTest, "ObjectiveFollowsChain")
bool FQuestObjectiveProgressTest::RunTest(const FString&)
{
	FQuestChainState State;
	TestTrue(TEXT("Find canister"), State.GetObjective().ToString().Contains(TEXT("канистру")));
	State.Interact(EInteractableType::Canister);
	TestTrue(TEXT("Drain"), State.GetObjective().ToString().Contains(TEXT("БМП")));
	State.Interact(EInteractableType::Vehicle);
	TestTrue(TEXT("Generator"), State.GetObjective().ToString().Contains(TEXT("генератор")));
	State.Interact(EInteractableType::Generator);
	TestTrue(TEXT("Terminal"), State.GetObjective().ToString().Contains(TEXT("пульте")));
	State.Interact(EInteractableType::GateTerminal);
	TestTrue(TEXT("Opening"), State.GetObjective().ToString().Contains(TEXT("открываются")));
	return true;
}

QUEST_TEST(FQuestCutsceneTimerTest, "CutsceneEndsAfterFourSeconds")
bool FQuestCutsceneTimerTest::RunTest(const FString&)
{
	FGameFlowStateMachine Machine;
	Machine.TriggerCombatZone();
	Machine.Tick(Machine.GetConfig().CutsceneDuration - 0.5f);
	TestEqual(TEXT("Still cutscene"), Machine.GetPhase(), ECodexGamePhase::Cutscene);
	Machine.Tick(1.f);
	TestEqual(TEXT("Preparation"), Machine.GetPhase(), ECodexGamePhase::Preparation);
	return true;
}

#undef QUEST_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
