#include "Misc/AutomationTest.h"
#include "Quests/QuestChain.h"

#if WITH_DEV_AUTOMATION_TESTS

// 2026-10-21 (Demo 01 A7, the canister quest on the lake): the chain reports its steps to the tutorial ("QuestStep.<Kind>") and its
// progress as the condition stat "quest_progress" - pure FQuestChainState::GetProgress / GetStepEvent.

#define QUEST_STEP_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Quests.StepEvents." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

QUEST_STEP_TEST(FQuestStepEventsChainTest, "FullChain")
bool FQuestStepEventsChainTest::RunTest(const FString&)
{
	FQuestChainState State;
	TestEqual(TEXT("nothing done: progress 0"), State.GetProgress(), 0);
	const struct
	{
		EInteractableType Type;
		FName Expected;
		int32 Progress;
	} Sequence[] = {
		{ EInteractableType::Canister, QuestSteps::CanisterTaken, 1 },
		{ EInteractableType::Vehicle, QuestSteps::FuelDrained, 2 },
		{ EInteractableType::Generator, QuestSteps::GeneratorStarted, 3 },
		{ EInteractableType::GateTerminal, QuestSteps::GatePowered, 4 },
	};
	for (const auto& Step : Sequence)
	{
		const FQuestChainState Before = State;
		State.Interact(Step.Type);
		TestEqual(FString::Printf(TEXT("step %s"), *Step.Expected.ToString()), FQuestChainState::GetStepEvent(Before, State), Step.Expected);
		TestEqual(FString::Printf(TEXT("progress after %s"), *Step.Expected.ToString()), State.GetProgress(), Step.Progress);
	}
	TestEqual(TEXT("step names"), QuestSteps::FuelDrained.ToString(), FString(TEXT("FuelDrained")));
	return true;
}

QUEST_STEP_TEST(FQuestStepEventsRefusedTest, "RefusedAndRepeatedRaiseNothing")
bool FQuestStepEventsRefusedTest::RunTest(const FString&)
{
	// Out of order: the terminal without power, the APC without a container, the generator dry - no step, no progress.
	FQuestChainState State;
	for (const EInteractableType Type : { EInteractableType::GateTerminal, EInteractableType::Vehicle, EInteractableType::Generator, EInteractableType::Gate })
	{
		const FQuestChainState Before = State;
		State.Interact(Type);
		TestTrue(TEXT("a refused interaction raises no step"), FQuestChainState::GetStepEvent(Before, State).IsNone());
	}
	TestEqual(TEXT("still 0"), State.GetProgress(), 0);

	// A repeat (the canister again, the APC again after the drain, the running generator, the powered terminal) raises nothing.
	State.Interact(EInteractableType::Canister);
	FQuestChainState Before = State;
	State.Interact(EInteractableType::Canister);
	TestTrue(TEXT("the canister twice: nothing"), FQuestChainState::GetStepEvent(Before, State).IsNone());
	State.Interact(EInteractableType::Vehicle);
	Before = State;
	State.Interact(EInteractableType::Vehicle);
	TestTrue(TEXT("the APC twice: nothing"), FQuestChainState::GetStepEvent(Before, State).IsNone());
	State.Interact(EInteractableType::Generator);
	State.Interact(EInteractableType::GateTerminal);
	Before = State;
	State.Interact(EInteractableType::GateTerminal);
	State.Interact(EInteractableType::Generator);
	TestTrue(TEXT("after the chain: nothing"), FQuestChainState::GetStepEvent(Before, State).IsNone());
	TestEqual(TEXT("the chain is done: 4"), State.GetProgress(), 4);

	// A save restored backwards (or the «Start Battle» flags) never reports a step.
	FQuestChainState Fresh;
	TestTrue(TEXT("going back is not a step"), FQuestChainState::GetStepEvent(State, Fresh).IsNone());
	return true;
}

QUEST_STEP_TEST(FQuestStepEventsProgressTest, "ProgressFromFlags")
bool FQuestStepEventsProgressTest::RunTest(const FString&)
{
	// The flags as a save / «Start Battle» set them: the furthest step wins.
	FQuestChainState Battle;
	Battle.bHasEmptyCanister = true;
	Battle.bHasFuelCanister = true;
	Battle.bIsGeneratorRunning = true;
	Battle.bIsGatePowered = true;
	TestEqual(TEXT("«Start Battle»: 4"), Battle.GetProgress(), 4);
	FQuestChainState Fuel;
	Fuel.bHasFuelCanister = true;
	TestEqual(TEXT("a full canister: 2"), Fuel.GetProgress(), 2);
	return true;
}

#endif
