#include "Misc/AutomationTest.h"
#include "Characters/FirePostureRules.h"
#include "Core/PlayerCommands.h"
#include "Tutorial/TutorialRules.h"
#include "Tutorial/TutorialStateMachine.h"

#if WITH_DEV_AUTOMATION_TESTS

// Demo 01, game designer play-test 2026-10-19 (beat sheet decision block "2026-10-19 b"): the A3 -> A4 lessons on the real
// demo_01_steps.json - the posture lesson right after the trap disarm (with its A4 twin when the wire was never spotted), the
// squad entering the complex Defensive ("set_posture" action).

#define TUTORIAL_AMBUSH_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Tutorial.AmbushLesson." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace TutorialAmbushTestData
{
	bool LoadDemo(FAutomationTestBase& Test, FTutorialStepsData& Out)
	{
		TArray<FString> Warnings;
		FString Error;
		const bool bOk = TutorialRules::LoadStepsFile(TEXT("demo_01_steps.json"), Out, Warnings, Error);
		Test.TestTrue(FString::Printf(TEXT("demo steps load (%s)"), *Error), bOk);
		for (const FString& Warning : Warnings)
		{
			Test.AddWarning(Warning);
		}
		return bOk;
	}

	/** Ticks the machine long enough for delays, breathers and short speech lines (lines run in the runtime, not here). */
	void Settle(FTutorialStateMachine& Machine, int32 Ticks = 8)
	{
		for (int32 Index = 0; Index < Ticks; ++Index)
		{
			Machine.Tick(0.5f);
		}
	}

	/** A0 -> A3 with the cover field lesson done (the posture lesson "requires" A3_cover). */
	void IntoTheField(FTutorialStateMachine& Machine)
	{
		Machine.StartBeat(TEXT("A0"));
		Machine.StartBeat(TEXT("A3"));
		Machine.TriggerStep(TEXT("A3_cover"));
		Machine.SkipStep(TEXT("A3_cover"));
		Settle(Machine);
		Machine.SkipStep(TEXT("A3_peek")); // the corner peek card follows the cover card
		Settle(Machine);
	}

	bool PostureFree(const FTutorialStateMachine& Machine)
	{
		return !Machine.IsCommandLocked(PlayerCommands::PosturePassive) && !Machine.IsCommandLocked(PlayerCommands::PostureDefensive)
			&& !Machine.IsCommandLocked(PlayerCommands::PostureAggressive);
	}
}

TUTORIAL_AMBUSH_TEST(FTutorialAmbushPostureSchemaTest, "PostureSchema")
bool FTutorialAmbushPostureSchemaTest::RunTest(const FString&)
{
	FTutorialStepsData Data;
	if (!TutorialAmbushTestData::LoadDemo(*this, Data))
	{
		return false;
	}
	// The squad enters the complex Defensive (the A3 approach lines carry the action).
	const FTutorialStep* Approach = Data.FindStep(TEXT("A3_wt_approach"));
	TestTrue(TEXT("A3_wt_approach sets the squad DEFENSIVE"), Approach && Approach->Actions.ContainsByPredicate([](const FTutorialAction& Action)
	{
		ESquadFirePosture Posture = ESquadFirePosture::Aggressive;
		return Action.Type == FName(TEXT("set_posture")) && FirePostureRules::ParseName(Action.Posture, Posture) && Posture == ESquadFirePosture::Defensive;
	}));
	for (const TCHAR* Id : { TEXT("A3_posture"), TEXT("A4_posture") })
	{
		const FTutorialStep* Card = Data.FindStep(Id);
		if (!TestNotNull(FString::Printf(TEXT("%s exists"), Id), Card))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("%s: a card with the three posture keys"), Id), Card->Kind == ETutorialStepKind::HintCard
			&& Card->Keys.Contains(PlayerCommands::PosturePassive.ToString()) && Card->Keys.Contains(PlayerCommands::PostureDefensive.ToString())
			&& Card->Keys.Contains(PlayerCommands::PostureAggressive.ToString()));
		TestTrue(FString::Printf(TEXT("%s: unlocks Posture.*, done on Posture.Passive, never precredited"), Id), Card->Unlock.Contains(TEXT("Posture.*"))
			&& Card->Complete.Commands.Contains(PlayerCommands::PosturePassive.ToString()) && !Card->bPrecredit);
	}
	const FTutorialStep* A3Card = Data.FindStep(TEXT("A3_posture"));
	TestTrue(TEXT("A3_posture: beat-less (never dropped by the overlook), after the wire lesson, requires the cover field"), A3Card && A3Card->Beat.IsNone()
		&& A3Card->Trigger.Type == ETutorialTriggerType::AfterStep && A3Card->Trigger.Step == FName(TEXT("A3_wire"))
		&& A3Card->Trigger.Requires.Contains(TEXT("A3_cover")) && A3Card->CancelOn.Steps.Contains(TEXT("A4_posture")));
	const FTutorialStep* A4Card = Data.FindStep(TEXT("A4_posture"));
	TestTrue(TEXT("A4_posture: the overlook twin, cancelled by the A3 card"), A4Card && A4Card->Beat == FName(TEXT("A4"))
		&& A4Card->Trigger.Type == ETutorialTriggerType::BeatStart && A4Card->CancelOn.Steps.Contains(TEXT("A3_posture")));
	const FTutorialStep* Radio = Data.FindStep(TEXT("A3_posture_radio"));
	TestTrue(TEXT("the Commander's 'Hold your fire' radio"), Radio && Radio->Kind == ETutorialStepKind::Radio && Radio->Lines.Num() == 1
		&& Radio->Lines[0].Speaker == TEXT("Commander") && Radio->Lines[0].TextId == FName(TEXT("WT_A3_POSTURE")));

	// The action is validated.
	FTutorialStepsData Bad;
	TArray<FString> Warnings;
	FString Error;
	TestFalse(TEXT("set_posture with an unknown posture fails"), TutorialRules::ParseSteps(TEXT(R"JSON({ "steps": [ { "id": "s", "kind": "walk_talk",
		"trigger": { "type": "manual" }, "action": { "type": "set_posture", "posture": "berserk" }, "lines": [ { "text_id": "X" } ] } ] })JSON"), Bad, Warnings, Error));
	TestTrue(TEXT("set_posture is a known action"), TutorialRules::IsKnownAction(TEXT("set_posture")));
	return true;
}

TUTORIAL_AMBUSH_TEST(FTutorialAmbushPostureAfterDisarmTest, "PostureAfterDisarm")
bool FTutorialAmbushPostureAfterDisarmTest::RunTest(const FString&)
{
	using namespace TutorialAmbushTestData;
	FTutorialStepsData Data;
	if (!LoadDemo(*this, Data))
	{
		return false;
	}
	FTutorialStateMachine Machine;
	Machine.Load(Data);
	IntoTheField(Machine);
	TestFalse(TEXT("the posture keys are still locked in the field"), PostureFree(Machine));
	// The wire at the field exit: spotted -> the disarm card -> disarmed.
	Machine.HandleEvent(TEXT("TripwireSpotted"), TEXT("Op3"));
	Settle(Machine);
	TestEqual(TEXT("the wire card is up"), Machine.GetState(TEXT("A3_wire")), ETutorialStepState::Active);
	TestEqual(TEXT("no posture card before the disarm lesson ends"), Machine.GetState(TEXT("A3_posture")), ETutorialStepState::Idle);
	Machine.HandleCommand(PlayerCommands::DisarmTripwire);
	TestEqual(TEXT("disarm: the wire lesson is done"), Machine.GetState(TEXT("A3_wire")), ETutorialStepState::Completed);
	Settle(Machine);
	TestEqual(TEXT("right after it: the posture card"), Machine.GetState(TEXT("A3_posture")), ETutorialStepState::Active);
	TestTrue(TEXT("the posture radio runs with it"), TutorialRules::IsRunning(Machine.GetState(TEXT("A3_posture_radio"))));
	TestTrue(TEXT("the card unlocks , . /"), PostureFree(Machine));
	// The overlook comes before the player pressed it: the beat-less card stays up, its twin waits behind it.
	Machine.StartBeat(TEXT("A4"));
	Settle(Machine);
	TestEqual(TEXT("A4 start: the A3 posture card is NOT dropped"), Machine.GetState(TEXT("A3_posture")), ETutorialStepState::Active);
	TestNotEqual(TEXT("... its A4 twin is not shown"), Machine.GetState(TEXT("A4_posture")), ETutorialStepState::Active);
	Machine.HandleCommand(PlayerCommands::PostureDefensive);
	TestEqual(TEXT("Defensive is not what it asks for"), Machine.GetState(TEXT("A3_posture")), ETutorialStepState::Active);
	Machine.HandleCommand(PlayerCommands::PosturePassive);
	TestEqual(TEXT("Posture.Passive completes it"), Machine.GetState(TEXT("A3_posture")), ETutorialStepState::Completed);
	Settle(Machine);
	TestEqual(TEXT("the A4 twin is cancelled (never shown)"), Machine.GetState(TEXT("A4_posture")), ETutorialStepState::Skipped);
	// The radio lines are played by the runtime (here: finished by hand) - the twin radio is dropped when they are done.
	TestTrue(TEXT("the A3 radio finishes"), Machine.CompleteStep(TEXT("A3_posture_radio")));
	Settle(Machine);
	TestEqual(TEXT("the A4 twin radio is dropped (never played)"), Machine.GetState(TEXT("A4_posture_radio")), ETutorialStepState::Skipped);
	return true;
}

TUTORIAL_AMBUSH_TEST(FTutorialAmbushPostureFallbackTest, "PostureAtTheOverlookWithoutTheWire")
bool FTutorialAmbushPostureFallbackTest::RunTest(const FString&)
{
	using namespace TutorialAmbushTestData;
	FTutorialStepsData Data;
	if (!LoadDemo(*this, Data))
	{
		return false;
	}
	// The wire never spotted (gone round it / tripped it): the lesson comes at the overlook.
	FTutorialStateMachine Machine;
	Machine.Load(Data);
	IntoTheField(Machine);
	Machine.StartBeat(TEXT("A4"));
	Settle(Machine);
	TestEqual(TEXT("the cone card first"), Machine.GetState(TEXT("A4_cones")), ETutorialStepState::Active);
	Machine.HandleCommand(PlayerCommands::ToggleVisionCones);
	Settle(Machine);
	TestEqual(TEXT("then the posture card at the overlook"), Machine.GetState(TEXT("A4_posture")), ETutorialStepState::Active);
	TestTrue(TEXT("it unlocks , . /"), PostureFree(Machine));
	TestNotEqual(TEXT("the tripwire card waits behind it"), Machine.GetState(TEXT("A4_tripwire")), ETutorialStepState::Active);
	Machine.HandleCommand(PlayerCommands::PosturePassive);
	TestEqual(TEXT("done on Posture.Passive"), Machine.GetState(TEXT("A4_posture")), ETutorialStepState::Completed);
	Settle(Machine);
	TestEqual(TEXT("the tripwire card follows"), Machine.GetState(TEXT("A4_tripwire")), ETutorialStepState::Active);
	// The wire spotted later after all: no second posture lesson.
	Machine.HandleEvent(TEXT("TripwireSpotted"), TEXT("Op3"));
	Machine.SkipStep(TEXT("A3_wire"));
	Settle(Machine);
	TestNotEqual(TEXT("no second posture card"), Machine.GetState(TEXT("A3_posture")), ETutorialStepState::Active);
	TestTrue(TEXT("the A3 card is cancelled or never armed"), Machine.GetState(TEXT("A3_posture")) == ETutorialStepState::Skipped
		|| Machine.GetState(TEXT("A3_posture")) == ETutorialStepState::Idle);

	// A wire spotted before the cover field (the player wandered off the route) does not arm it.
	FTutorialStateMachine Early;
	Early.Load(Data);
	Early.StartBeat(TEXT("A0"));
	Early.StartBeat(TEXT("A2"));
	Early.HandleEvent(TEXT("TripwireSpotted"), TEXT("Op1"));
	Settle(Early);
	Early.SkipStep(TEXT("A3_wire"));
	Settle(Early);
	TestEqual(TEXT("an early wire: no posture lesson (requires A3_cover)"), Early.GetState(TEXT("A3_posture")), ETutorialStepState::Idle);
	TestFalse(TEXT("... and the keys stay locked"), PostureFree(Early));
	return true;
}

#undef TUTORIAL_AMBUSH_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
