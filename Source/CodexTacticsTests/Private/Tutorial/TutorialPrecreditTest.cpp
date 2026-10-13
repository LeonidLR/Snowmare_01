#include "Misc/AutomationTest.h"
#include "Core/PlayerCommands.h"
#include "Dom/JsonObject.h"
#include "Tutorial/TutorialRules.h"
#include "Tutorial/TutorialStateMachine.h"

#if WITH_DEV_AUTOMATION_TESTS

// Precredit (user decision 2026-10-13): a lesson the player already performed before its card appears counts as done and is
// never forced again; the command history (counts + hold time since the level started) is kept and saved.

#define TUTORIAL_PRECREDIT_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Tutorial." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace TutorialPrecreditTestData
{
	const TCHAR* Steps = TEXT(R"JSON({
		"id": "precredit", "start_beat": "A0",
		"beats": [ { "id": "A0" } ],
		"steps": [
			{ "id": "radio", "beat": "A0", "kind": "radio", "trigger": { "type": "beat_start" }, "lines": [ { "text_id": "WT_X", "duration_s": 5 } ] },
			{ "id": "pan", "beat": "A0", "kind": "hint_card", "text_id": "TUT_PAN", "keys": ["Camera.Pan.Keys"],
			  "trigger": { "type": "after_step", "step": "radio" }, "complete": { "commands": ["Camera.Pan.Keys", "Camera.Pan.Edge"], "hold_s": 1.0 } },
			{ "id": "drag", "beat": "A0", "kind": "hint_card", "text_id": "TUT_DRAG", "keys": ["Camera.Pan.Drag"],
			  "trigger": { "type": "after_step", "step": "pan" }, "complete": { "commands": ["Camera.Pan.Drag"], "hold_s": 0.5 } },
			{ "id": "yaw", "beat": "A0", "kind": "hint_card", "text_id": "TUT_YAW", "keys": ["Camera.Rotate.Keys"],
			  "trigger": { "type": "after_step", "step": "drag" }, "complete": { "commands": ["Camera.Rotate.Keys"], "count": 2 } },
			{ "id": "regroup", "beat": "A0", "kind": "hint_card", "text_id": "TUT_REGROUP", "keys": ["Squad.Regroup"], "precredit": false,
			  "trigger": { "type": "after_step", "step": "yaw" }, "complete": { "commands": ["Squad.Regroup"] } },
			{ "id": "cover", "beat": "A0", "kind": "hint_card", "text_id": "TUT_COVER",
			  "trigger": { "type": "after_step", "step": "regroup" }, "complete": { "event": "CoverEntered" } }
		] })JSON");

	void Pump(FTutorialStateMachine& Machine, float Seconds)
	{
		for (float Left = Seconds; Left > 0.f; Left -= 0.1f)
		{
			Machine.Tick(0.1f);
		}
	}
}

TUTORIAL_PRECREDIT_TEST(FTutorialPrecreditRulesTest, "Precredit.Rules")
bool FTutorialPrecreditRulesTest::RunTest(const FString&)
{
	using namespace TutorialPrecreditTestData;
	FTutorialStepsData Data;
	TArray<FString> Warnings;
	FString Error;
	if (!TestTrue(FString::Printf(TEXT("parse (%s)"), *Error), TutorialRules::ParseSteps(Steps, Data, Warnings, Error)))
	{
		return false;
	}
	TestEqual(TEXT("no warnings (precredit is a known key)"), Warnings.Num(), 0);
	const FTutorialStep* Pan = Data.FindStep(TEXT("pan"));
	const FTutorialStep* Yaw = Data.FindStep(TEXT("yaw"));
	const FTutorialStep* Regroup = Data.FindStep(TEXT("regroup"));
	const FTutorialStep* Cover = Data.FindStep(TEXT("cover"));
	const FTutorialStep* Radio = Data.FindStep(TEXT("radio"));
	if (!Pan || !Yaw || !Regroup || !Cover || !Radio)
	{
		return false;
	}
	TestTrue(TEXT("default on"), Pan->bPrecredit && TutorialRules::CanPrecredit(*Pan));
	TestFalse(TEXT("\"precredit\": false opts out"), Regroup->bPrecredit || TutorialRules::CanPrecredit(*Regroup));
	TestFalse(TEXT("event completions are never precredited"), TutorialRules::CanPrecredit(*Cover));
	TestFalse(TEXT("radio steps are never precredited"), TutorialRules::CanPrecredit(*Radio));

	TMap<FName, FTutorialCommandUse> History;
	TestFalse(TEXT("empty history"), TutorialRules::IsPrecreditSatisfied(*Pan, History));
	History.Add(PlayerCommands::CameraPanKeys, { 1, 0.4f });
	TestFalse(TEXT("one use, 0.4 s held: not yet (hold_s 1.0)"), TutorialRules::IsPrecreditSatisfied(*Pan, History));
	History.Add(PlayerCommands::CameraPanEdge, { 1, 0.7f });
	TestTrue(TEXT("hold time sums over the matching commands (0.4 + 0.7 >= 1.0)"), TutorialRules::IsPrecreditSatisfied(*Pan, History));
	History.Add(PlayerCommands::CameraRotateKeys, { 1, 0.f });
	TestFalse(TEXT("yaw needs two presses"), TutorialRules::IsPrecreditSatisfied(*Yaw, History));
	History[PlayerCommands::CameraRotateKeys].Count = 2;
	TestTrue(TEXT("two presses satisfy it"), TutorialRules::IsPrecreditSatisfied(*Yaw, History));
	History.Add(PlayerCommands::Regroup, { 5, 0.f });
	TestFalse(TEXT("opted out: never satisfied by history"), TutorialRules::IsPrecreditSatisfied(*Regroup, History));
	TestEqual(TEXT("history uses of a step's commands"), TutorialRules::CountHistoryUses(*Yaw, History), 2);
	FTutorialStep Remedial = *Yaw;
	Remedial.bOnce = false;
	TestFalse(TEXT("a repeatable (remedial) card is never precredited"), TutorialRules::CanPrecredit(Remedial));

	// Save round trip of the history.
	FTutorialProgress Progress;
	Progress.History = History;
	const TSharedRef<FJsonObject> Saved = TutorialRules::SaveProgress(Progress, true);
	FTutorialProgress Loaded;
	TestTrue(TEXT("load"), TutorialRules::LoadProgress(*Saved, Loaded));
	TestEqual(TEXT("history entries saved"), Loaded.History.Num(), History.Num());
	TestTrue(TEXT("count and hold time survive"), Loaded.History.Contains(PlayerCommands::CameraRotateKeys)
		&& Loaded.History[PlayerCommands::CameraRotateKeys].Count == 2 && FMath::IsNearlyEqual(Loaded.History[PlayerCommands::CameraPanEdge].HeldSeconds, 0.7f, 0.01f));
	return true;
}

TUTORIAL_PRECREDIT_TEST(FTutorialPrecreditMachineTest, "Precredit.StateMachine")
bool FTutorialPrecreditMachineTest::RunTest(const FString&)
{
	using namespace TutorialPrecreditTestData;
	FTutorialStepsData Data;
	TArray<FString> Warnings;
	FString Error;
	if (!TestTrue(FString::Printf(TEXT("parse (%s)"), *Error), TutorialRules::ParseSteps(Steps, Data, Warnings, Error)))
	{
		return false;
	}
	FTutorialStateMachine Machine;
	Machine.Load(Data);
	Machine.StartBeat(TEXT("A0"));
	Pump(Machine, 0.5f);
	TestEqual(TEXT("the radio plays, no card yet"), Machine.GetActiveSpeech(), FName(TEXT("radio")));
	// The player pans with WASD and MMB-drags while HQ talks: the history records it although no card counts it.
	Machine.HandleCommand(PlayerCommands::CameraPanKeys);
	for (int32 Tick = 0; Tick < 12; ++Tick)
	{
		Machine.HandleCommandHold(PlayerCommands::CameraPanKeys, 0.1f);
	}
	Machine.HandleCommand(PlayerCommands::CameraPanDrag);
	for (int32 Tick = 0; Tick < 6; ++Tick)
	{
		Machine.HandleCommandHold(PlayerCommands::CameraPanDrag, 0.1f);
	}
	Machine.HandleCommand(PlayerCommands::CameraRotateKeys); // only ONE Q press: the yaw card still needs the second
	Machine.HandleCommand(PlayerCommands::Regroup); // regroup opted out: it must still show
	TestEqual(TEXT("history: pan uses"), Machine.GetCommandUse(PlayerCommands::CameraPanKeys).Count, 1);
	TestTrue(TEXT("history: pan hold 1.2 s"), FMath::IsNearlyEqual(Machine.GetCommandUse(PlayerCommands::CameraPanKeys).HeldSeconds, 1.2f, 0.01f));
	TestEqual(TEXT("pan still idle while the radio plays"), Machine.GetState(TEXT("pan")), ETutorialStepState::Idle);
	Machine.CompleteStep(TEXT("radio"));
	TArray<FTutorialTransition> Transitions = Machine.ConsumeTransitions();
	int32 Precredited = 0;
	for (const FTutorialTransition& Transition : Transitions)
	{
		Precredited += Transition.bPrecredited ? 1 : 0;
	}
	TestEqual(TEXT("pan and drag precredited (never shown)"), Machine.GetState(TEXT("pan")), ETutorialStepState::Completed);
	TestEqual(TEXT("drag precredited"), Machine.GetState(TEXT("drag")), ETutorialStepState::Completed);
	TestEqual(TEXT("two precredit transitions"), Precredited, 2);
	TestTrue(TEXT("the precredited steps never went Active"), !Transitions.ContainsByPredicate([](const FTutorialTransition& T)
		{ return (T.StepId == FName(TEXT("pan")) || T.StepId == FName(TEXT("drag"))) && T.To == ETutorialStepState::Active; }));
	Pump(Machine, 0.5f);
	TestEqual(TEXT("yaw (one press short) is the first card shown"), Machine.GetActiveCard(), FName(TEXT("yaw")));
	TestEqual(TEXT("partial credit: the earlier Q press seeds 1 of 2"), Machine.GetProgress(TEXT("yaw")), 1);
	Machine.HandleCommand(PlayerCommands::CameraRotateKeys);
	TestEqual(TEXT("the second press completes it normally"), Machine.GetState(TEXT("yaw")), ETutorialStepState::Completed);
	Pump(Machine, 2.f);
	TestEqual(TEXT("regroup opted out: shown although it was pressed before"), Machine.GetActiveCard(), FName(TEXT("regroup")));
	Machine.HandleCommand(PlayerCommands::Regroup);
	Pump(Machine, 2.f);
	TestEqual(TEXT("the event card follows and is never precredited"), Machine.GetActiveCard(), FName(TEXT("cover")));

	// Export / import keeps the history; a re-triggered command card is precredited from the restored history.
	const FTutorialProgress Progress = Machine.ExportProgress();
	TestEqual(TEXT("exported history has the regroup uses"), Progress.History.Contains(PlayerCommands::Regroup) ? Progress.History[PlayerCommands::Regroup].Count : 0, 2);
	FTutorialStateMachine Restored;
	Restored.Load(Data);
	Restored.ImportProgress(Progress);
	TestEqual(TEXT("history restored"), Restored.GetCommandUse(PlayerCommands::CameraPanKeys).Count, 1);
	TestTrue(TEXT("replaying the pan card after a load: precredited again"), Restored.ReplayStep(TEXT("pan")));
	TestEqual(TEXT("done without showing"), Restored.GetState(TEXT("pan")), ETutorialStepState::Completed);
	Restored.ResetProgress();
	TestEqual(TEXT("reset forgets the history"), Restored.GetCommandUse(PlayerCommands::CameraPanKeys).Count, 0);
	return true;
}

#undef TUTORIAL_PRECREDIT_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
