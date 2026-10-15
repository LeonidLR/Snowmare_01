#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Tutorial/TutorialRules.h"
#include "Tutorial/TutorialStateMachine.h"

#if WITH_DEV_AUTOMATION_TESTS

// Rev 5i (game designer 2026-10-15, "they just run everyone into the hut and skip the tactical entry"): the "confine" step feature -
// the squad is held in an area while a lesson runs (FTutorialConfine, FTutorialStateMachine, TutorialRules::JudgeConfinedMove).

#define TUTORIAL_CONFINE_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Tutorial.Confine." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace TutorialConfineTestData
{
	/** A hut entry in miniature: hold (solo), then left / right spots and the inside, each opened by its card. */
	const TCHAR* Hut = TEXT(R"JSON({
		"id": "confine", "start_beat": "A1", "beats": [ { "id": "A1" }, { "id": "A2" } ],
		"steps": [
			{ "id": "hold", "beat": "A1", "kind": "hint_card", "trigger": { "type": "volume", "volume": "APPROACH" },
			  "confine": { "volume": "APPROACH", "allow": ["LEFT", "RIGHT", "INSIDE", "FREE"], "until": "enter" },
			  "complete": { "commands": ["Squad.Solo"] } },
			{ "id": "left", "beat": "A1", "kind": "hint_card", "trigger": { "type": "after_step", "step": "hold" },
			  "complete": { "event": "CoverEntered", "in_volume": "LEFT" } },
			{ "id": "right", "beat": "A1", "kind": "hint_card", "trigger": { "type": "after_step", "step": "hold" },
			  "complete": { "event": "CoverEntered", "in_volume": "RIGHT", "distinct_from": ["left"] } },
			{ "id": "enter", "beat": "A1", "kind": "hint_card", "trigger": { "type": "after_step", "step": "hold" },
			  "complete": { "volume": "INSIDE", "distinct_from": ["left", "right"] } },
			{ "id": "later", "beat": "A2", "kind": "objective", "trigger": { "type": "volume", "volume": "LATER" }, "complete": { "event": "Never" } }
		] })JSON");

	bool Parse(FAutomationTestBase& Test, const TCHAR* Json, FTutorialStepsData& Out)
	{
		TArray<FString> Warnings;
		FString Error;
		const bool bOk = TutorialRules::ParseSteps(Json, Out, Warnings, Error);
		Test.TestTrue(FString::Printf(TEXT("steps parse (%s)"), *Error), bOk);
		for (const FString& Warning : Warnings)
		{
			Test.AddInfo(Warning);
		}
		return bOk;
	}

	TArray<FName> Areas(const FTutorialStateMachine& Machine)
	{
		FTutorialConfineView Confine;
		return Machine.GetActiveConfine(Confine) ? Confine.Areas : TArray<FName>();
	}

	TArray<FName> Names(std::initializer_list<const TCHAR*> List)
	{
		TArray<FName> Out;
		for (const TCHAR* Name : List)
		{
			Out.Add(FName(Name));
		}
		return Out;
	}
}

TUTORIAL_CONFINE_TEST(FTutorialConfineParseTest, "Parse")
bool FTutorialConfineParseTest::RunTest(const FString&)
{
	FTutorialStepsData Data;
	const TCHAR* Json = TEXT(R"JSON({ "id": "p", "beats": [ { "id": "A" } ], "steps": [
		{ "id": "s", "beat": "A", "kind": "hint_card", "trigger": { "type": "manual" }, "complete": { "commands": ["Squad.Solo"] },
		  "confine": { "volume": "V", "allow": ["W", "X"], "until": "t", "stop": true, "span": true, "stance": "crouched",
		    "exit": { "stance": "prone", "one_at_a_time": "CrawlGapPassed" } } },
		{ "id": "t", "beat": "A", "kind": "hint_card", "trigger": { "type": "manual" }, "complete": { "commands": ["Squad.Solo"] } } ] })JSON");
	if (!TutorialConfineTestData::Parse(*this, Json, Data))
	{
		return false;
	}
	const FTutorialConfine& Confine = Data.Steps[0].Confine;
	TestTrue(TEXT("every key read"), Confine.IsSet() && Confine.Volume == FName(TEXT("V")) && Confine.Allow == TutorialConfineTestData::Names({ TEXT("W"), TEXT("X") })
		&& Confine.Until == FName(TEXT("t")) && Confine.bStop && Confine.bSpan && Confine.Stance == FName(TEXT("crouch"))
		&& Confine.ExitStance == FName(TEXT("prone")) && Confine.ExitEvent == TEXT("CrawlGapPassed") && Confine.HasExit());
	TestFalse(TEXT("a step without confine has none"), Data.Steps[1].Confine.IsSet());

	TArray<FString> Warnings;
	FString Error;
	FTutorialStepsData Bad;
	TestFalse(TEXT("confine without a volume fails"), TutorialRules::ParseSteps(TEXT(R"JSON({ "steps": [ { "id": "s", "confine": { "allow": ["W"] } } ] })JSON"),
		Bad, Warnings, Error));
	TestFalse(TEXT("an unknown 'until' step fails"), TutorialRules::ParseSteps(TEXT(R"JSON({ "steps": [ { "id": "s", "confine": { "volume": "V", "until": "nope" } } ] })JSON"),
		Bad, Warnings, Error));
	TestFalse(TEXT("an unknown stance fails"), TutorialRules::ParseSteps(TEXT(R"JSON({ "steps": [ { "id": "s", "confine": { "volume": "V", "stance": "fly" } } ] })JSON"),
		Bad, Warnings, Error));
	return true;
}

TUTORIAL_CONFINE_TEST(FTutorialConfineJudgeTest, "JudgeMove")
bool FTutorialConfineJudgeTest::RunTest(const FString&)
{
	using namespace TutorialRules;
	const FName Stand(TEXT("stand"));
	const FName Crouch(TEXT("crouch"));
	const FName Prone(TEXT("prone"));
	TestTrue(TEXT("stance: crouch is satisfied by crouched / prone, not standing"), StanceSatisfies(Crouch, Crouch) && StanceSatisfies(Prone, Crouch)
		&& !StanceSatisfies(Stand, Crouch));
	TestTrue(TEXT("stance: prone only by prone"), StanceSatisfies(Prone, Prone) && !StanceSatisfies(Crouch, Prone));
	TestTrue(TEXT("no stance asked: anything goes"), StanceSatisfies(Stand, NAME_None));

	FTutorialConfineView Hold;
	Hold.Spec.Volume = TEXT("V");
	TestEqual(TEXT("plain hold: inside allowed"), JudgeConfinedMove(Hold, TEXT("Op1"), Stand, true), ETutorialConfineVerdict::Allowed);
	TestEqual(TEXT("plain hold: outside refused"), JudgeConfinedMove(Hold, TEXT("Op1"), Prone, false), ETutorialConfineVerdict::Outside);

	FTutorialConfineView Low = Hold;
	Low.Spec.Stance = Crouch;
	TestEqual(TEXT("crouch first: a standing operative is refused even inside"), JudgeConfinedMove(Low, TEXT("Op1"), Stand, true), ETutorialConfineVerdict::Stance);
	TestEqual(TEXT("crouched inside: allowed"), JudgeConfinedMove(Low, TEXT("Op1"), Crouch, true), ETutorialConfineVerdict::Allowed);
	TestEqual(TEXT("crouched outside: still outside"), JudgeConfinedMove(Low, TEXT("Op1"), Crouch, false), ETutorialConfineVerdict::Outside);

	FTutorialConfineView Fence = Hold;
	Fence.Spec.ExitStance = Prone;
	Fence.Spec.ExitEvent = TEXT("CrawlGapPassed");
	TestEqual(TEXT("fence: standing inside allowed"), JudgeConfinedMove(Fence, TEXT("Op1"), Stand, true), ETutorialConfineVerdict::Allowed);
	TestEqual(TEXT("fence: crouched past the fence refused (exit stance)"), JudgeConfinedMove(Fence, TEXT("Op1"), Crouch, false), ETutorialConfineVerdict::ExitStance);
	TestEqual(TEXT("fence: prone past the fence, nobody crossing: allowed"), JudgeConfinedMove(Fence, TEXT("Op1"), Prone, false), ETutorialConfineVerdict::Allowed);
	Fence.ExitHolder = TEXT("Op1");
	TestEqual(TEXT("fence: Op1 crossing - Op2 must wait"), JudgeConfinedMove(Fence, TEXT("Op2"), Prone, false), ETutorialConfineVerdict::NotYourTurn);
	TestEqual(TEXT("fence: Op1 may go on"), JudgeConfinedMove(Fence, TEXT("Op1"), Prone, false), ETutorialConfineVerdict::Allowed);
	Fence.Passed.Add(TEXT("Op3"));
	TestEqual(TEXT("fence: one who got through moves freely, any stance"), JudgeConfinedMove(Fence, TEXT("Op3"), Stand, false), ETutorialConfineVerdict::Allowed);
	TestEqual(TEXT("verdict names"), FString(ConfineVerdictName(ETutorialConfineVerdict::NotYourTurn)), FString(TEXT("not_your_turn")));
	return true;
}

TUTORIAL_CONFINE_TEST(FTutorialConfineFlowTest, "Flow")
bool FTutorialConfineFlowTest::RunTest(const FString&)
{
	using namespace TutorialConfineTestData;
	FTutorialStepsData Data;
	if (!Parse(*this, Hut, Data))
	{
		return false;
	}
	FTutorialStateMachine Machine;
	Machine.Load(Data);
	Machine.RegisterVolumeBeat(TEXT("LATER"), TEXT("A2"));
	Machine.StartBeat(TEXT("A1"));
	TestFalse(TEXT("nothing held before the approach"), Machine.IsConfined());
	const int32 SerialBefore = Machine.GetConfineSerial();
	Machine.HandleVolume(TEXT("APPROACH"), NAME_None, NAME_None, TEXT("Op1"));
	TestTrue(TEXT("arriving starts the confinement"), Machine.IsConfined() && Machine.GetConfineSerial() != SerialBefore);
	TestTrue(TEXT("only the holding area and the ungated spot are open"), Areas(Machine) == Names({ TEXT("APPROACH"), TEXT("FREE") }));

	// The route does not move while held: a later beat's volume neither starts it nor passes anything.
	Machine.HandleVolume(TEXT("LATER"), NAME_None, NAME_None, TEXT("Op2"));
	TestEqual(TEXT("held: the route does not move (beat stays A1)"), Machine.GetCurrentBeat(), FName(TEXT("A1")));
	TestEqual(TEXT("held: the lesson card is not passed"), Machine.GetState(TEXT("hold")), ETutorialStepState::Active);
	// Inside early: the 'go in' step is not running yet - nothing counts.
	Machine.HandleVolume(TEXT("INSIDE"), NAME_None, NAME_None, TEXT("Op3"));
	TestEqual(TEXT("inside early counts for nothing"), Machine.GetState(TEXT("enter")), ETutorialStepState::Idle);

	Machine.HandleCommand(TEXT("Squad.Solo"));
	Machine.Tick(2.f);
	TestEqual(TEXT("left card up"), Machine.GetActiveCard(), FName(TEXT("left")));
	TestTrue(TEXT("LEFT opens with its card"), Areas(Machine) == Names({ TEXT("APPROACH"), TEXT("LEFT"), TEXT("FREE") }));
	Machine.HandleEvent(TEXT("CoverEntered"), TEXT("Op2"), { TEXT("RIGHT") });
	TestEqual(TEXT("right before its card: held (lesson order)"), Machine.GetState(TEXT("right")), ETutorialStepState::Queued);
	Machine.HandleVolume(TEXT("INSIDE"), NAME_None, NAME_None, TEXT("Op3"));
	TestEqual(TEXT("inside while 'go in' is only queued: held"), Machine.GetState(TEXT("enter")), ETutorialStepState::Queued);
	Machine.HandleEvent(TEXT("CoverEntered"), TEXT("Op1"), { TEXT("LEFT") });
	TestEqual(TEXT("left done"), Machine.GetState(TEXT("left")), ETutorialStepState::Completed);
	Machine.Tick(2.f);
	TestTrue(TEXT("RIGHT opens next, LEFT stays open"), Areas(Machine) == Names({ TEXT("APPROACH"), TEXT("LEFT"), TEXT("RIGHT"), TEXT("FREE") }));

	// Save round trip mid-lesson: the confinement holds again after the load.
	FTutorialProgress Loaded;
	TestTrue(TEXT("the save object loads"), TutorialRules::LoadProgress(*TutorialRules::SaveProgress(Machine.ExportProgress(), true), Loaded));
	TestTrue(TEXT("the save lists the confinement"), Loaded.Confines.Num() == 1 && Loaded.Confines[0].Owner == FName(TEXT("hold")) && !Loaded.Confines[0].bReleased);
	FTutorialStateMachine Restored;
	Restored.Load(Data);
	Restored.ImportProgress(Loaded);
	TestTrue(TEXT("after the load: still held, the same areas open"), Restored.IsConfined() && Areas(Restored) == Areas(Machine));

	Machine.HandleEvent(TEXT("CoverEntered"), TEXT("Op2"), { TEXT("RIGHT") });
	Machine.Tick(2.f);
	TestEqual(TEXT("'go in' up"), Machine.GetActiveCard(), FName(TEXT("enter")));
	TestTrue(TEXT("INSIDE opens with it"), Areas(Machine).Contains(FName(TEXT("INSIDE"))));
	Machine.HandleVolume(TEXT("INSIDE"), NAME_None, NAME_None, TEXT("Op3"));
	TestEqual(TEXT("the third inside completes it"), Machine.GetState(TEXT("enter")), ETutorialStepState::Completed);
	TestFalse(TEXT("the 'until' step finished: released"), Machine.IsConfined());
	Machine.HandleVolume(TEXT("LATER"), NAME_None, NAME_None, TEXT("Op2"));
	TestEqual(TEXT("released: the route moves again"), Machine.GetCurrentBeat(), FName(TEXT("A2")));

	// Explicit release (tutorial off / skipped / a fight) survives a save.
	Restored.ReleaseConfines();
	TestFalse(TEXT("explicit release"), Restored.IsConfined());
	FTutorialProgress Released;
	TutorialRules::LoadProgress(*TutorialRules::SaveProgress(Restored.ExportProgress(), true), Released);
	FTutorialStateMachine Again;
	Again.Load(Data);
	Again.ImportProgress(Released);
	TestFalse(TEXT("a released confinement stays released after a load"), Again.IsConfined());

	// A later beat releases an earlier beat's confinement.
	FTutorialStateMachine Beat;
	Beat.Load(Data);
	Beat.StartBeat(TEXT("A1"));
	Beat.HandleVolume(TEXT("APPROACH"), NAME_None, NAME_None, TEXT("Op1"));
	TestTrue(TEXT("held"), Beat.IsConfined());
	Beat.StartBeat(TEXT("A2"));
	TestFalse(TEXT("a later beat (started by a script) ends it"), Beat.IsConfined());
	return true;
}

TUTORIAL_CONFINE_TEST(FTutorialConfineExitTurnTest, "ExitTurn")
bool FTutorialConfineExitTurnTest::RunTest(const FString&)
{
	using namespace TutorialConfineTestData;
	const TCHAR* Json = TEXT(R"JSON({ "id": "fence", "start_beat": "A3", "beats": [ { "id": "A3" } ], "steps": [
		{ "id": "crawl", "beat": "A3", "kind": "hint_card", "trigger": { "type": "volume", "volume": "FENCE" },
		  "confine": { "volume": "FENCE", "stop": true, "exit": { "stance": "prone", "one_at_a_time": "CrawlGapPassed" } },
		  "complete": { "event": "CrawlGapPassed", "count": 3, "distinct_operatives": true } } ] })JSON");
	FTutorialStepsData Data;
	if (!Parse(*this, Json, Data))
	{
		return false;
	}
	FTutorialStateMachine Machine;
	Machine.Load(Data);
	Machine.StartBeat(TEXT("A3"));
	Machine.HandleVolume(TEXT("FENCE"), NAME_None, NAME_None, TEXT("Op1"));
	FTutorialConfineView View;
	TestTrue(TEXT("held at the fence"), Machine.GetActiveConfine(View) && View.Spec.bStop && View.ExitHolder.IsNone());
	Machine.TakeConfineExitTurn(TEXT("Op1"));
	Machine.TakeConfineExitTurn(TEXT("Op2"));
	Machine.GetActiveConfine(View);
	TestEqual(TEXT("the first one ordered past takes the turn"), View.ExitHolder, FName(TEXT("Op1")));
	TestEqual(TEXT("Op2 must wait"), TutorialRules::JudgeConfinedMove(View, TEXT("Op2"), TEXT("prone"), false), ETutorialConfineVerdict::NotYourTurn);
	Machine.HandleEvent(TEXT("CrawlGapPassed"), TEXT("Op1"));
	Machine.GetActiveConfine(View);
	TestTrue(TEXT("Op1 through: the turn is free, he is listed"), View.ExitHolder.IsNone() && View.Passed.Contains(FName(TEXT("Op1"))));
	Machine.TakeConfineExitTurn(TEXT("Op2"));
	Machine.ReturnConfineExitTurn(TEXT("Op2"));
	Machine.GetActiveConfine(View);
	TestTrue(TEXT("Op2 called back into the area gives the turn back"), View.ExitHolder.IsNone());
	Machine.TakeConfineExitTurn(TEXT("Op2"));
	FTutorialProgress Loaded;
	TutorialRules::LoadProgress(*TutorialRules::SaveProgress(Machine.ExportProgress(), true), Loaded);
	FTutorialStateMachine Restored;
	Restored.Load(Data);
	Restored.ImportProgress(Loaded);
	FTutorialConfineView RestoredView;
	TestTrue(TEXT("the turn and who got through survive a save"), Restored.GetActiveConfine(RestoredView) && RestoredView.ExitHolder == FName(TEXT("Op2"))
		&& RestoredView.Passed == TArray<FName>{ FName(TEXT("Op1")) });
	Machine.HandleEvent(TEXT("CrawlGapPassed"), TEXT("Op2"));
	Machine.HandleEvent(TEXT("CrawlGapPassed"), TEXT("Op3"));
	TestEqual(TEXT("three through: the lesson completes"), Machine.GetState(TEXT("crawl")), ETutorialStepState::Completed);
	TestFalse(TEXT("and the squad is free"), Machine.IsConfined());
	return true;
}

// Rev 5i item 3 (game designer 2026-10-15): at the A3 approach the squad stops and moves up to the low ridge only crouched.
TUTORIAL_CONFINE_TEST(FTutorialConfineDemoA3LowTest, "DemoA3Low")
bool FTutorialConfineDemoA3LowTest::RunTest(const FString&)
{
	TestEqual(TEXT("stat squad_not_low_in.<volume> parses"), TutorialRules::ParseNotLowInStat(TEXT("squad_not_low_in.VOL_A3_RIDGE")), FName(TEXT("VOL_A3_RIDGE")));
	TestTrue(TEXT("other stats are not it"), TutorialRules::ParseNotLowInStat(TEXT("squad_cold_max")).IsNone() && TutorialRules::IsKnownStat(TEXT("squad_not_low_in.X")));
	FTutorialStepsData Data;
	TArray<FString> Warnings;
	FString Error;
	if (!TestTrue(FString::Printf(TEXT("demo file loads (%s)"), *Error), TutorialRules::LoadStepsFile(TEXT("demo_01_steps.json"), Data, Warnings, Error)))
	{
		return false;
	}
	const FTutorialStep* Low = Data.FindStep(TEXT("A3_low"));
	TestTrue(TEXT("A3_low: a card on VOL_A3_APPROACH, unlocks the stances, holds crouched in the corridor approach -> ridge, stops the squad"), Low
		&& Low->Kind == ETutorialStepKind::HintCard && Low->Trigger.Type == ETutorialTriggerType::Volume && Low->Trigger.Volume == FName(TEXT("VOL_A3_APPROACH"))
		&& Low->Unlock.Contains(TEXT("Stance.*")) && Low->Confine.Volume == FName(TEXT("VOL_A3_APPROACH")) && Low->Confine.Allow.Contains(FName(TEXT("VOL_A3_RIDGE")))
		&& Low->Confine.bSpan && Low->Confine.bStop && Low->Confine.Stance == FName(TEXT("crouch")) && Low->Complete.Conditions.Num() == 1
		&& Low->Complete.Conditions[0].Stat == FName(TEXT("squad_not_low_in.VOL_A3_RIDGE")) && !Low->Complete.Conditions[0].bAtLeast && Low->Complete.TimeoutSeconds == 0.f);
	TestTrue(TEXT("its keys: crouch / squad crouch"), Low && Low->Keys.Contains(TEXT("Stance.Crouch")) && Low->Keys.Contains(TEXT("Stance.Squad.Crouch")));

	float NotLow = 3.f;
	FTutorialStateMachine Machine;
	Machine.Load(Data);
	Machine.StatProvider = [&NotLow](FName Stat) { return Stat == FName(TEXT("squad_not_low_in.VOL_A3_RIDGE")) ? NotLow : 0.f; };
	Machine.RegisterVolumeBeat(TEXT("VOL_A3_APPROACH"), TEXT("A3"));
	Machine.StartBeat(TEXT("A0"));
	Machine.StartBeat(TEXT("A2"));
	TestTrue(TEXT("stances still locked in A2"), Machine.IsCommandLocked(TEXT("Stance.Crouch")));
	Machine.HandleVolume(TEXT("VOL_A3_APPROACH"), NAME_None, TEXT("A3"), TEXT("Op1"));
	Machine.Tick(0.1f);
	TestEqual(TEXT("the approach starts A3"), Machine.GetCurrentBeat(), FName(TEXT("A3")));
	TestTrue(TEXT("the squad is held at the approach, crouched"), Machine.IsConfined());
	TestEqual(TEXT("the 'stay low' card shows at once"), Machine.GetActiveCard(), FName(TEXT("A3_low")));
	TestFalse(TEXT("crouch unlocked by the card"), Machine.IsCommandLocked(TEXT("Stance.Crouch")) || Machine.IsCommandLocked(TEXT("Stance.Squad.Crouch")));
	Machine.HandleVolume(TEXT("VOL_A3_WIND"), NAME_None, NAME_None, TEXT("Op1"));
	Machine.Tick(1.f);
	TestEqual(TEXT("the wind strip's stance card waits behind it"), Machine.GetActiveCard(), FName(TEXT("A3_low")));
	NotLow = 1.f;
	Machine.Tick(0.5f);
	TestEqual(TEXT("one still standing / outside: not done"), Machine.GetState(TEXT("A3_low")), ETutorialStepState::Active);
	NotLow = 0.f;
	Machine.Tick(0.5f);
	TestEqual(TEXT("everyone crouched at the ridge: done"), Machine.GetState(TEXT("A3_low")), ETutorialStepState::Completed);
	TestFalse(TEXT("and free"), Machine.IsConfined());

	// The fence reached first (the approach skipped): the card is passed, nothing holds the squad, the stances are free.
	FTutorialStateMachine Fence;
	Fence.Load(Data);
	Fence.StartBeat(TEXT("A0"));
	Fence.StartBeat(TEXT("A2"));
	Fence.HandleVolume(TEXT("VOL_A3_FENCE"), NAME_None, NAME_None, TEXT("Op1"));
	TestEqual(TEXT("fence first: A3_low passed"), Fence.GetState(TEXT("A3_low")), ETutorialStepState::Skipped);
	TestFalse(TEXT("fence first: the stances unlocked on the way"), Fence.IsCommandLocked(TEXT("Stance.Crouch")));
	FTutorialConfineView View;
	TestTrue(TEXT("fence first: not held at the approach"), !Fence.GetActiveConfine(View) || View.Spec.Volume != FName(TEXT("VOL_A3_APPROACH")));
	return true;
}

#undef TUTORIAL_CONFINE_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
