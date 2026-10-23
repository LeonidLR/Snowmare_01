#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Tutorial/TutorialRules.h"
#include "Tutorial/TutorialStateMachine.h"

#if WITH_DEV_AUTOMATION_TESTS

// 2026-10-23 (game designer play-test of the hut tactical entry: "I place one at the wall - he's fixed, can't be moved back; place the
// second - fixed"): the step-level "lock_in_place" - the operatives who complete a step are held where they stand until its "until"
// step finishes (FTutorialPlaceLock, FTutorialStateMachine::IsLockedInPlace, TutorialRules::IsPlaceLockedMove).

#define TUTORIAL_PLACE_LOCK_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Tutorial.PlaceLock." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace TutorialPlaceLockTestData
{
	/** The hut entry in miniature: held at the approach, left / right of the door lock their operative until the third is inside. */
	const TCHAR* LockHut = TEXT(R"JSON({
		"id": "lock", "start_beat": "A1", "beats": [ { "id": "A1" }, { "id": "A2" } ],
		"steps": [
			{ "id": "hold", "beat": "A1", "kind": "hint_card", "text_id": "T", "trigger": { "type": "volume", "volume": "APPROACH" },
			  "confine": { "volume": "APPROACH", "allow": ["LEFT", "RIGHT", "INSIDE"], "until": "enter" },
			  "complete": { "commands": ["Squad.Solo"] } },
			{ "id": "left", "beat": "A1", "kind": "hint_card", "text_id": "T", "trigger": { "type": "after_step", "step": "hold" },
			  "lock_in_place": { "until": "enter", "text_id": "HOLD" },
			  "complete": { "event": "CoverEntered", "in_volume": "LEFT" } },
			{ "id": "right", "beat": "A1", "kind": "hint_card", "text_id": "T", "trigger": { "type": "after_step", "step": "hold" },
			  "lock_in_place": true,
			  "complete": { "event": "CoverEntered", "in_volume": "RIGHT", "distinct_from": ["left"] } },
			{ "id": "enter", "beat": "A1", "kind": "hint_card", "text_id": "T", "trigger": { "type": "after_step", "step": "hold" },
			  "complete": { "volume": "INSIDE", "distinct_from": ["left", "right"] } }
		] })JSON");

	bool Parse(FAutomationTestBase& Test, const TCHAR* Json, FTutorialStepsData& Out, TArray<FString>* OutWarnings = nullptr)
	{
		TArray<FString> Warnings;
		FString Error;
		const bool bOk = TutorialRules::ParseSteps(Json, Out, Warnings, Error);
		Test.TestTrue(FString::Printf(TEXT("steps parse (%s)"), *Error), bOk);
		if (OutWarnings)
		{
			*OutWarnings = Warnings;
		}
		return bOk;
	}

	/** Runs the miniature hut up to both sides of the door taken (Op1 left, Op2 right). */
	void RunToBothSides(FTutorialStateMachine& Machine)
	{
		Machine.StartBeat(TEXT("A1"));
		Machine.HandleVolume(TEXT("APPROACH"), NAME_None, NAME_None, TEXT("Op1"));
		Machine.HandleCommand(TEXT("Squad.Solo"));
		Machine.Tick(2.f);
		Machine.HandleEvent(TEXT("CoverEntered"), TEXT("Op1"), { TEXT("LEFT") });
		Machine.Tick(2.f);
		Machine.HandleEvent(TEXT("CoverEntered"), TEXT("Op2"), { TEXT("RIGHT") });
		Machine.Tick(2.f);
	}
}

TUTORIAL_PLACE_LOCK_TEST(FTutorialPlaceLockParseTest, "Parse")
bool FTutorialPlaceLockParseTest::RunTest(const FString&)
{
	using namespace TutorialPlaceLockTestData;
	FTutorialStepsData Data;
	TArray<FString> Warnings;
	if (!Parse(*this, LockHut, Data, &Warnings))
	{
		return false;
	}
	TestEqual(TEXT("the miniature hut parses without warnings"), Warnings.Num(), 0);
	const FTutorialStep* Left = Data.FindStep(TEXT("left"));
	const FTutorialStep* Right = Data.FindStep(TEXT("right"));
	TestTrue(TEXT("object form: until + text_id read"), Left && Left->LockInPlace.IsSet() && Left->LockInPlace.Until == FName(TEXT("enter"))
		&& Left->LockInPlace.TextId == FName(TEXT("HOLD")));
	TestTrue(TEXT("true form: on, no until / text"), Right && Right->LockInPlace.IsSet() && Right->LockInPlace.Until.IsNone() && Right->LockInPlace.TextId.IsNone());
	TestFalse(TEXT("a step without the key is not locking"), Data.FindStep(TEXT("enter"))->LockInPlace.IsSet());

	FString Error;
	FTutorialStepsData Bad;
	TArray<FString> BadWarnings;
	TestFalse(TEXT("a number is an error"), TutorialRules::ParseSteps(TEXT(R"JSON({ "steps": [ { "id": "s", "text_id": "T",
		"complete": { "event": "E" }, "lock_in_place": 3 } ] })JSON"), Bad, BadWarnings, Error));
	TestTrue(TEXT("... naming the key"), Error.Contains(TEXT("lock_in_place")));
	TestFalse(TEXT("an unknown 'until' step is an error"), TutorialRules::ParseSteps(TEXT(R"JSON({ "steps": [ { "id": "s", "text_id": "T",
		"complete": { "event": "E" }, "lock_in_place": { "until": "nope" } } ] })JSON"), Bad, BadWarnings, Error));
	TestFalse(TEXT("'until' naming the step itself is an error (it would never hold)"), TutorialRules::ParseSteps(TEXT(R"JSON({ "steps": [ { "id": "s",
		"text_id": "T", "complete": { "event": "E" }, "lock_in_place": { "until": "s" } } ] })JSON"), Bad, BadWarnings, Error));

	BadWarnings.Reset();
	TestTrue(TEXT("unknown keys / no subject only warn"), TutorialRules::ParseSteps(TEXT(R"JSON({ "steps": [ { "id": "s", "text_id": "T",
		"complete": { "commands": ["Squad.Solo"] }, "lock_in_place": { "untill": "x" } } ] })JSON"), Bad, BadWarnings, Error));
	TestTrue(TEXT("the misspelt key is warned about"), BadWarnings.ContainsByPredicate([](const FString& W) { return W.Contains(TEXT("untill")); }));
	TestTrue(TEXT("a command completion has nobody to lock: warned"), BadWarnings.ContainsByPredicate([](const FString& W) { return W.Contains(TEXT("nobody is locked")); }));
	BadWarnings.Reset();
	TutorialRules::ParseSteps(TEXT(R"JSON({ "steps": [ { "id": "s", "text_id": "T", "complete": { "event": "E" }, "lock_in_place": false } ] })JSON"),
		Bad, BadWarnings, Error);
	TestFalse(TEXT("false = off"), Bad.Steps.Num() == 1 && Bad.Steps[0].LockInPlace.IsSet());
	TestEqual(TEXT("false warns nothing"), BadWarnings.Num(), 0);
	return true;
}

TUTORIAL_PLACE_LOCK_TEST(FTutorialPlaceLockMoveRuleTest, "MoveRule")
bool FTutorialPlaceLockMoveRuleTest::RunTest(const FString&)
{
	const FVector At(1000.f, 500.f, 90.f);
	TestFalse(TEXT("not locked: any order goes"), TutorialRules::IsPlaceLockedMove(false, At, At + FVector(800.f, 0.f, 0.f)));
	TestTrue(TEXT("locked: a walk away is refused"), TutorialRules::IsPlaceLockedMove(true, At, At + FVector(250.f, 0.f, 0.f)));
	TestTrue(TEXT("locked: a shimmy along the wall (60 cm) is refused"), TutorialRules::IsPlaceLockedMove(true, At, At + FVector(0.f, 60.f, 0.f)));
	TestFalse(TEXT("locked: an order for the spot he holds (within the tolerance) is no move"),
		TutorialRules::IsPlaceLockedMove(true, At, At + FVector(TutorialRules::PlaceLockToleranceCm - 5.f, 0.f, 0.f)));
	TestFalse(TEXT("locked: height does not count (2D)"), TutorialRules::IsPlaceLockedMove(true, At, At + FVector(0.f, 0.f, 300.f)));
	TestEqual(TEXT("verdict name for logs / telemetry"), FString(TutorialRules::ConfineVerdictName(ETutorialConfineVerdict::LockedInPlace)), FString(TEXT("locked_in_place")));
	return true;
}

TUTORIAL_PLACE_LOCK_TEST(FTutorialPlaceLockFlowTest, "HutFlow")
bool FTutorialPlaceLockFlowTest::RunTest(const FString&)
{
	using namespace TutorialPlaceLockTestData;
	FTutorialStepsData Data;
	if (!Parse(*this, LockHut, Data))
	{
		return false;
	}
	FTutorialStateMachine Machine;
	Machine.Load(Data);
	Machine.StartBeat(TEXT("A1"));
	Machine.HandleVolume(TEXT("APPROACH"), NAME_None, NAME_None, TEXT("Op1"));
	Machine.HandleCommand(TEXT("Squad.Solo"));
	Machine.Tick(2.f);
	TestFalse(TEXT("nobody locked before his part is done"), Machine.IsLockedInPlace(TEXT("Op1")) || Machine.IsLockedInPlace(TEXT("Op2")));
	const int32 SerialBefore = Machine.GetPlaceLockSerial();
	Machine.HandleEvent(TEXT("CoverEntered"), TEXT("Op1"), { TEXT("LEFT") });
	FName LockedBy;
	TestTrue(TEXT("left done: Op1 is locked by it"), Machine.IsLockedInPlace(TEXT("Op1"), &LockedBy) && LockedBy == FName(TEXT("left")));
	TestTrue(TEXT("... the serial moved (the runtime marks him)"), Machine.GetPlaceLockSerial() != SerialBefore);
	TestFalse(TEXT("the others stay free (the next operative can be ordered)"), Machine.IsLockedInPlace(TEXT("Op2")) || Machine.IsLockedInPlace(TEXT("Op3")));
	Machine.Tick(2.f);
	Machine.HandleEvent(TEXT("CoverEntered"), TEXT("Op2"), { TEXT("RIGHT") });
	TestTrue(TEXT("right done: Op2 locked too, Op1 still"), Machine.IsLockedInPlace(TEXT("Op2"), &LockedBy) && LockedBy == FName(TEXT("right"))
		&& Machine.IsLockedInPlace(TEXT("Op1")));
	const FTutorialPlaceLockState* RightLock = Machine.GetPlaceLocks().FindByPredicate([](const FTutorialPlaceLockState& L) { return L.Operative == FName(TEXT("Op2")); });
	TestTrue(TEXT("'lock_in_place': true follows the confinement's 'until' (enter)"), RightLock && RightLock->Until == FName(TEXT("enter")));

	// Save round trip with both held.
	FTutorialProgress Loaded;
	TestTrue(TEXT("the save loads"), TutorialRules::LoadProgress(*TutorialRules::SaveProgress(Machine.ExportProgress(), true), Loaded));
	TestEqual(TEXT("the save lists both place locks"), Loaded.PlaceLocks.Num(), 2);
	FTutorialStateMachine Restored;
	Restored.Load(Data);
	Restored.ImportProgress(Loaded);
	TestTrue(TEXT("after the load both are still held"), Restored.IsLockedInPlace(TEXT("Op1")) && Restored.IsLockedInPlace(TEXT("Op2"))
		&& Restored.GetPlaceLocks() == Machine.GetPlaceLocks());

	// The 'go in' card: the third walks in -> the lesson is over, both are released.
	Machine.Tick(2.f);
	TestEqual(TEXT("'go in' up"), Machine.GetActiveCard(), FName(TEXT("enter")));
	TestTrue(TEXT("still held while the third goes in"), Machine.IsLockedInPlace(TEXT("Op1")) && Machine.IsLockedInPlace(TEXT("Op2")));
	Machine.HandleVolume(TEXT("INSIDE"), NAME_None, NAME_None, TEXT("Op3"));
	TestEqual(TEXT("the third inside completes it"), Machine.GetState(TEXT("enter")), ETutorialStepState::Completed);
	TestTrue(TEXT("released: nobody held"), Machine.GetPlaceLocks().IsEmpty());

	// Skipping the 'go in' card releases them too (the card's Skip ends the lesson).
	Restored.Tick(2.f);
	TestTrue(TEXT("skip the 'go in' card"), Restored.SkipStep(TEXT("enter")));
	TestTrue(TEXT("skipped: released"), Restored.GetPlaceLocks().IsEmpty());
	return true;
}

TUTORIAL_PLACE_LOCK_TEST(FTutorialPlaceLockReleaseTest, "Release")
bool FTutorialPlaceLockReleaseTest::RunTest(const FString&)
{
	using namespace TutorialPlaceLockTestData;
	FTutorialStepsData Data;
	if (!Parse(*this, LockHut, Data))
	{
		return false;
	}
	// UnlockAll (tutorial off / SKIP TUTORIAL / a fight) releases, and stays released after a save.
	FTutorialStateMachine Machine;
	Machine.Load(Data);
	RunToBothSides(Machine);
	TestEqual(TEXT("both held"), Machine.GetPlaceLocks().Num(), 2);
	TestTrue(TEXT("release all"), Machine.ReleasePlaceLocks());
	TestFalse(TEXT("nothing left to release"), Machine.ReleasePlaceLocks());
	FTutorialProgress Loaded;
	TutorialRules::LoadProgress(*TutorialRules::SaveProgress(Machine.ExportProgress(), true), Loaded);
	FTutorialStateMachine Again;
	Again.Load(Data);
	Again.ImportProgress(Loaded);
	TestTrue(TEXT("released stays released after a load"), Again.GetPlaceLocks().IsEmpty());

	// A later beat releases the locks of an earlier beat's steps.
	FTutorialStateMachine Beat;
	Beat.Load(Data);
	RunToBothSides(Beat);
	Beat.StartBeat(TEXT("A2"));
	TestTrue(TEXT("a later beat ends them"), Beat.GetPlaceLocks().IsEmpty());

	// A skipped part locks nobody (nobody stands there in person).
	FTutorialStateMachine Skip;
	Skip.Load(Data);
	Skip.StartBeat(TEXT("A1"));
	Skip.HandleVolume(TEXT("APPROACH"), NAME_None, NAME_None, TEXT("Op1"));
	Skip.HandleCommand(TEXT("Squad.Solo"));
	Skip.Tick(2.f);
	TestTrue(TEXT("skip the left card"), Skip.SkipStep(TEXT("left")));
	TestTrue(TEXT("skipped: nobody locked"), Skip.GetPlaceLocks().IsEmpty());

	// A save naming a step the file no longer has (or with no lock_in_place) is dropped; one entry per operative.
	TArray<FTutorialPlaceLockState> Saved;
	Saved.Add({ TEXT("Op1"), TEXT("left"), TEXT("enter") });
	Saved.Add({ TEXT("Op2"), TEXT("gone"), NAME_None });
	Saved.Add({ TEXT("Op3"), TEXT("enter"), NAME_None });
	Saved.Add({ TEXT("Op1"), TEXT("right"), TEXT("enter") });
	const TArray<FTutorialPlaceLockState> Kept = TutorialRules::FilterPlaceLocks(Data, Saved);
	TestTrue(TEXT("only Op1 kept, by his latest lock"), Kept.Num() == 1 && Kept[0].Operative == FName(TEXT("Op1")) && Kept[0].Step == FName(TEXT("right")));
	return true;
}

TUTORIAL_PLACE_LOCK_TEST(FTutorialPlaceLockDemoFileTest, "DemoFile")
bool FTutorialPlaceLockDemoFileTest::RunTest(const FString&)
{
	// The shipped Demo 01 hut lesson: both sides of the door lock their operative until the third is inside.
	FTutorialStepsData Data;
	TArray<FString> Warnings;
	FString Error;
	if (!TestTrue(TEXT("demo steps load"), TutorialRules::LoadStepsFile(TEXT("demo_01_steps.json"), Data, Warnings, Error)))
	{
		AddError(Error);
		return false;
	}
	for (const TCHAR* Side : { TEXT("A1_hut_stack_l"), TEXT("A1_hut_stack_r") })
	{
		const FTutorialStep* Step = Data.FindStep(Side);
		TestTrue(FString::Printf(TEXT("%s locks its operative until A1_hut_enter with TUT_A1_HUT_HOLD"), Side), Step && Step->LockInPlace.IsSet()
			&& Step->LockInPlace.Until == FName(TEXT("A1_hut_enter")) && Step->LockInPlace.TextId == FName(TEXT("TUT_A1_HUT_HOLD")));
	}
	TestFalse(TEXT("the 'go in' step itself locks nobody"), Data.FindStep(TEXT("A1_hut_enter"))->LockInPlace.IsSet());
	return true;
}

#endif
