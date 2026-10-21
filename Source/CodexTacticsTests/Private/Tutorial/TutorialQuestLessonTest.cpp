#include "Misc/AutomationTest.h"
#include "Core/PlayerCommands.h"
#include "Quests/QuestChain.h"
#include "Tutorial/TutorialRules.h"
#include "Tutorial/TutorialStateMachine.h"

#if WITH_DEV_AUTOMATION_TESTS

// 2026-10-21 (user decisions, Demo 01 user flow step 14): the A7 canister quest on the lake - the yard volume VOL_A7_YARD starts
// A7, four objectives follow the quest chain (QuestStep.<Kind> events, "quest_progress" for steps already done), the console starts
// A8 (the horde). Runs on the real demo_01_steps.json.

#define TUTORIAL_QUEST_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Tutorial.QuestLesson." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace TutorialQuestTestData
{
	bool LoadQuestDemo(FAutomationTestBase& Test, FTutorialStepsData& Data)
	{
		TArray<FString> Warnings;
		FString Error;
		const bool bOk = TutorialRules::LoadStepsFile(TEXT("demo_01_steps.json"), Data, Warnings, Error);
		Test.TestTrue(FString::Printf(TEXT("demo file loads (%s)"), *Error), bOk);
		return bOk;
	}

	void SettleQuest(FTutorialStateMachine& Machine, int32 Ticks = 12)
	{
		for (int32 Index = 0; Index < Ticks; ++Index)
		{
			Machine.Tick(0.5f);
		}
	}

	/** "QuestStep.<Kind>" then "QuestStep", as UQuestSubsystem raises them. */
	void RaiseStep(FTutorialStateMachine& Machine, FName Kind)
	{
		Machine.HandleEvent(FName(*(GameplayEvents::QuestStep.ToString() + TEXT(".") + Kind.ToString())), TEXT("Engineer"));
		Machine.HandleEvent(GameplayEvents::QuestStep, TEXT("Engineer"));
	}

	bool IsDone(const FTutorialStateMachine& Machine, const TCHAR* StepId)
	{
		const ETutorialStepState State = Machine.GetState(StepId);
		return State == ETutorialStepState::Completed || State == ETutorialStepState::Skipped || State == ETutorialStepState::TimedOut;
	}
}

TUTORIAL_QUEST_TEST(FTutorialQuestSchemaTest, "Schema")
bool FTutorialQuestSchemaTest::RunTest(const FString&)
{
	FTutorialStepsData Data;
	if (!TutorialQuestTestData::LoadQuestDemo(*this, Data))
	{
		return false;
	}
	const int32 A4 = Data.FindBeatIndex(TEXT("A4"));
	const int32 A7 = Data.FindBeatIndex(TEXT("A7"));
	const int32 A8 = Data.FindBeatIndex(TEXT("A8"));
	TestTrue(TEXT("A7 then A8 after A4"), A4 != INDEX_NONE && A7 == A4 + 1 && A8 == A7 + 1);
	if (A7 == INDEX_NONE || A8 == INDEX_NONE)
	{
		return false;
	}
	TestTrue(TEXT("A7: a lesson with a checkpoint"), Data.Beats[A7].bLesson && Data.Beats[A7].bCheckpoint);
	TestFalse(TEXT("A8: no checkpoint (a save between the console and the first wave would lose the fight)"), Data.Beats[A8].bCheckpoint);
	TestTrue(TEXT("A7 unlocks the quest objects' use and the defence prep"), Data.Beats[A7].Unlock.Contains(TEXT("Object.*"))
		&& Data.Beats[A7].Unlock.Contains(TEXT("Deploy.*")) && Data.Beats[A7].Unlock.Contains(TEXT("Trap.*")));
	TestFalse(TEXT("A7 keeps the time controls for the fight"), Data.Beats[A7].Unlock.Contains(TEXT("Time.*")));
	TestTrue(TEXT("quest_progress is a known stat"), TutorialRules::IsKnownStat(TEXT("quest_progress")));

	const struct
	{
		const TCHAR* Step;
		const TCHAR* Event;
		int32 Progress;
	} Objectives[] = {
		{ TEXT("A7_obj_canister"), TEXT("QuestStep.CanisterTaken"), 1 },
		{ TEXT("A7_obj_drain"), TEXT("QuestStep.FuelDrained"), 2 },
		{ TEXT("A7_obj_generator"), TEXT("QuestStep.GeneratorStarted"), 3 },
		{ TEXT("A7_obj_terminal"), TEXT("QuestStep.GatePowered"), 4 },
	};
	for (const auto& Objective : Objectives)
	{
		const FTutorialStep* Step = Data.FindStep(Objective.Step);
		TestTrue(FString::Printf(TEXT("%s: an A7 objective done on %s or quest_progress >= %d"), Objective.Step, Objective.Event, Objective.Progress),
			Step && Step->Beat == FName(TEXT("A7")) && Step->Kind == ETutorialStepKind::Objective && Step->Complete.Event == Objective.Event
			&& Step->Complete.Conditions.Num() == 1 && Step->Complete.Conditions[0].Stat == FName(TEXT("quest_progress"))
			&& FMath::IsNearlyEqual(Step->Complete.Conditions[0].Value, static_cast<float>(Objective.Progress)));
	}
	const FTutorialStep* Horde = Data.FindStep(TEXT("A8_radio_horde"));
	TestTrue(TEXT("the console starts A8"), Horde && Horde->Beat == FName(TEXT("A8")) && Horde->Trigger.Type == ETutorialTriggerType::Event
		&& Horde->Trigger.Event == TEXT("QuestStep.GatePowered"));
	return true;
}

TUTORIAL_QUEST_TEST(FTutorialQuestChainFlowTest, "ChainFlow")
bool FTutorialQuestChainFlowTest::RunTest(const FString&)
{
	using namespace TutorialQuestTestData;
	FTutorialStepsData Data;
	if (!LoadQuestDemo(*this, Data))
	{
		return false;
	}
	float Progress = 0.f;
	FTutorialStateMachine Machine;
	Machine.StatProvider = [&Progress](FName Stat) { return Stat == FName(TEXT("quest_progress")) ? Progress : 0.f; };
	Machine.Load(Data);
	Machine.RegisterVolumeBeat(TEXT("VOL_A7_YARD"), TEXT("A7"));
	Machine.StartBeat(TEXT("A0"));
	Machine.StartBeat(TEXT("A4"));
	SettleQuest(Machine);
	TestTrue(TEXT("A7 objectives are idle before the yard"), Machine.GetState(TEXT("A7_obj_canister")) == ETutorialStepState::Idle);

	Machine.HandleVolume(TEXT("VOL_A7_YARD"), NAME_None, TEXT("A7"), TEXT("Commander"));
	SettleQuest(Machine);
	TestEqual(TEXT("the yard starts A7"), Machine.GetCurrentBeat(), FName(TEXT("A7")));
	TestTrue(TEXT("objective 1 (the jerrycan) is up"), Machine.GetActiveObjectives().Contains(FName(TEXT("A7_obj_canister"))));
	TestFalse(TEXT("A7 unlocks Object.Use (the quest objects' menus)"), Machine.IsCommandLocked(PlayerCommands::UseObject));
	TestFalse(TEXT("A7 unlocks the deployables"), Machine.IsCommandLocked(PlayerCommands::DeployTurret));

	const struct
	{
		FName Kind;
		const TCHAR* Done;
		const TCHAR* Next;
	} Chain[] = {
		{ QuestSteps::CanisterTaken, TEXT("A7_obj_canister"), TEXT("A7_obj_drain") },
		{ QuestSteps::FuelDrained, TEXT("A7_obj_drain"), TEXT("A7_obj_generator") },
		{ QuestSteps::GeneratorStarted, TEXT("A7_obj_generator"), TEXT("A7_obj_terminal") },
	};
	for (const auto& Link : Chain)
	{
		Progress += 1.f;
		RaiseStep(Machine, Link.Kind);
		SettleQuest(Machine);
		TestTrue(FString::Printf(TEXT("%s done on QuestStep.%s"), Link.Done, *Link.Kind.ToString()), IsDone(Machine, Link.Done));
		TestTrue(FString::Printf(TEXT("then %s is up"), Link.Next), Machine.GetActiveObjectives().Contains(FName(Link.Next)));
	}
	TestTrue(TEXT("the generator line plays (or played)"), Machine.GetState(TEXT("A7_wt_generator")) != ETutorialStepState::Idle);
	TestTrue(TEXT("Time.* still locked before the console"), Machine.IsCommandLocked(PlayerCommands::TacticalPause));

	Progress = 4.f;
	RaiseStep(Machine, QuestSteps::GatePowered);
	SettleQuest(Machine, 2);
	TestTrue(TEXT("the console objective is done"), IsDone(Machine, TEXT("A7_obj_terminal")));
	TestEqual(TEXT("the console starts A8 (the horde)"), Machine.GetCurrentBeat(), FName(TEXT("A8")));
	TestTrue(TEXT("HQ: they heard it"), Machine.GetState(TEXT("A8_radio_horde")) != ETutorialStepState::Idle);
	TestFalse(TEXT("A8 unlocks the time controls"), Machine.IsCommandLocked(PlayerCommands::TacticalPause));
	return true;
}

TUTORIAL_QUEST_TEST(FTutorialQuestAlreadyDoneTest, "StepsDoneBeforeA7PassSilently")
bool FTutorialQuestAlreadyDoneTest::RunTest(const FString&)
{
	using namespace TutorialQuestTestData;
	FTutorialStepsData Data;
	if (!LoadQuestDemo(*this, Data))
	{
		return false;
	}
	// The jerrycan lies on the A4 route: the squad may take it - and drain the BTR - before the yard volume starts A7.
	float Progress = 2.f;
	FTutorialStateMachine Machine;
	Machine.StatProvider = [&Progress](FName Stat) { return Stat == FName(TEXT("quest_progress")) ? Progress : 0.f; };
	Machine.Load(Data);
	Machine.StartBeat(TEXT("A0"));
	Machine.StartBeat(TEXT("A7"));
	SettleQuest(Machine, 16);
	TestTrue(TEXT("the jerrycan objective completed silently"), IsDone(Machine, TEXT("A7_obj_canister")));
	TestTrue(TEXT("the drain objective completed silently"), IsDone(Machine, TEXT("A7_obj_drain")));
	TestTrue(TEXT("the tracker card is skipped (nothing to follow any more)"), Machine.GetState(TEXT("A7_objective")) == ETutorialStepState::Skipped);
	TestTrue(TEXT("the generator objective is up"), Machine.GetActiveObjectives().Contains(FName(TEXT("A7_obj_generator"))));
	return true;
}

#endif
