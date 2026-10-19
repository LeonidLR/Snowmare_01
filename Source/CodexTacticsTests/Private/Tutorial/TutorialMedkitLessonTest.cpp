#include "Misc/AutomationTest.h"
#include "Characters/PersonalItemRules.h"
#include "Tutorial/TutorialRules.h"
#include "Tutorial/TutorialStateMachine.h"

#if WITH_DEV_AUTOMATION_TESTS

// Rev 2026-10-19 (game designer play-test): the medkit lesson the first time an operative is badly hurt - a beat-less, condition-
// triggered card ("squad_injury_max" at least a data threshold: the health share lost), focused on the wounded operative ("focus": "wounded_operative"),
// completed only when a medkit heals HIM ("complete.subject": "focus").

#define TUTORIAL_MEDKIT_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Tutorial.MedkitLesson." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace TutorialMedkitTestData
{
	const TCHAR* Steps = TEXT(R"JSON({
		"id": "medkit", "start_beat": "A1", "beats": [ { "id": "A1" }, { "id": "A2" } ],
		"steps": [
			{ "id": "a1_card", "beat": "A1", "kind": "hint_card", "text_id": "TUT_A1_SOLO", "trigger": { "type": "manual" }, "complete": { "commands": ["Squad.Solo"] } },
			{ "id": "medkit", "kind": "hint_card", "text_id": "TUT_MEDKIT", "keys": ["Select.Member", "H"], "focus": "wounded_operative",
			  "unlock": ["Item.Use", "Inventory.Open"], "precredit": false,
			  "trigger": { "type": "condition" }, "condition": { "stat": "squad_injury_max", "at_least": 30 },
			  "complete": { "event": "ItemUsed.MEDKIT", "subject": "focus", "timeout_s": 120 } }
		] })JSON");

	bool Parse(FAutomationTestBase& Test, const TCHAR* Json, FTutorialStepsData& Out)
	{
		TArray<FString> Warnings;
		FString Error;
		const bool bOk = TutorialRules::ParseSteps(Json, Out, Warnings, Error);
		Test.TestTrue(FString::Printf(TEXT("steps parse (%s)"), *Error), bOk);
		Test.TestEqual(TEXT("no warnings"), Warnings.Num(), 0);
		for (const FString& Warning : Warnings)
		{
			Test.AddInfo(Warning);
		}
		return bOk;
	}
}

TUTORIAL_MEDKIT_TEST(FTutorialMedkitParseTest, "Parse")
bool FTutorialMedkitParseTest::RunTest(const FString&)
{
	FTutorialStepsData Data;
	if (!TutorialMedkitTestData::Parse(*this, TutorialMedkitTestData::Steps, Data))
	{
		return false;
	}
	const FTutorialStep* Step = Data.FindStep(TEXT("medkit"));
	if (!TestNotNull(TEXT("the medkit step"), Step))
	{
		return false;
	}
	TestTrue(TEXT("no beat (any beat)"), Step->Beat.IsNone());
	TestTrue(TEXT("condition trigger"), Step->Trigger.Type == ETutorialTriggerType::Condition);
	TestEqual(TEXT("focus"), Step->Focus, FName(TEXT("wounded_operative")));
	TestTrue(TEXT("subject: focus"), Step->Complete.bFocusSubject);
	TestTrue(TEXT("known stat"), TutorialRules::IsKnownStat(TEXT("squad_injury_max")));
	// Unit tests / headless runs without a squad read every stat as 0: "no one is hurt" (the lesson never fires by accident).
	FTutorialStateMachine Quiet;
	Quiet.Load(Data);
	Quiet.StartBeat(TEXT("A1"));
	Quiet.Tick(0.1f);
	TestEqual(TEXT("no stat provider: no medkit card"), Quiet.GetState(TEXT("medkit")), ETutorialStepState::Idle);
	TestTrue(TEXT("known focus"), TutorialRules::IsKnownFocus(TEXT("wounded_operative")));

	FTutorialStepsData Bad;
	TArray<FString> Warnings;
	FString Error;
	TestFalse(TEXT("an unknown subject fails"), TutorialRules::ParseSteps(TEXT(R"JSON({ "steps": [ { "id": "s", "kind": "hint_card",
		"trigger": { "type": "manual" }, "complete": { "event": "ItemUsed.MEDKIT", "subject": "leader" } } ] })JSON"), Bad, Warnings, Error));

	// The item ids of OnItemUsed the lesson listens to (Godot inventory_drawer.gd item_used).
	TestEqual(TEXT("medkit id"), FString(PersonalItemRules::GetEventId(EPersonalItem::Medkit)), FString(TEXT("MEDKIT")));
	TestEqual(TEXT("heat pack id"), FString(PersonalItemRules::GetEventId(EPersonalItem::HeatPack)), FString(TEXT("HEAT_PACK")));
	TestEqual(TEXT("canned food id"), FString(PersonalItemRules::GetEventId(EPersonalItem::CannedFood)), FString(TEXT("CANNED_FOOD")));
	return true;
}

TUTORIAL_MEDKIT_TEST(FTutorialMedkitFlowTest, "Flow")
bool FTutorialMedkitFlowTest::RunTest(const FString&)
{
	FTutorialStepsData Data;
	if (!TutorialMedkitTestData::Parse(*this, TutorialMedkitTestData::Steps, Data))
	{
		return false;
	}
	float Injury = 0.f;
	FName Focus = NAME_None;
	FTutorialStateMachine Machine;
	Machine.StatProvider = [&Injury](FName Stat) { return Stat == FName(TEXT("squad_injury_max")) ? Injury : 0.f; };
	Machine.FocusSubjectProvider = [&Focus](FName StepId) { return StepId == FName(TEXT("medkit")) ? Focus : NAME_None; };
	Machine.Load(Data);
	Machine.StartBeat(TEXT("A1"));
	Machine.Tick(0.1f);
	TestEqual(TEXT("everyone healthy: no card"), Machine.GetState(TEXT("medkit")), ETutorialStepState::Idle);
	Injury = 25.f;
	Machine.Tick(0.1f);
	TestEqual(TEXT("75 % health: still no card (threshold 70)"), Machine.GetState(TEXT("medkit")), ETutorialStepState::Idle);
	Injury = 99.f; // a lesson down leaves him at 1 HP
	Machine.Tick(0.1f);
	Machine.Tick(0.1f);
	TestEqual(TEXT("wounded: the card is up"), Machine.GetState(TEXT("medkit")), ETutorialStepState::Active);
	TestFalse(TEXT("its unlock applies"), Machine.IsCommandLocked(TEXT("Item.Use")));

	// A later beat never drops a beat-less lesson.
	Machine.StartBeat(TEXT("A2"));
	Machine.Tick(0.1f);
	TestEqual(TEXT("a later beat keeps it"), Machine.GetState(TEXT("medkit")), ETutorialStepState::Active);

	Focus = TEXT("Engineer");
	Machine.HandleEvent(TEXT("ItemUsed.MEDKIT"), TEXT("Commander"));
	TestEqual(TEXT("a medkit on someone else does not count"), Machine.GetState(TEXT("medkit")), ETutorialStepState::Active);
	Machine.HandleEvent(TEXT("ItemUsed.HEAT_PACK"), TEXT("Engineer"));
	TestEqual(TEXT("another item does not count"), Machine.GetState(TEXT("medkit")), ETutorialStepState::Active);
	Machine.HandleEvent(TEXT("ItemUsed.MEDKIT"), TEXT("Engineer"));
	TestEqual(TEXT("a medkit on HIM completes it"), Machine.GetState(TEXT("medkit")), ETutorialStepState::Completed);

	// Once: hurt again -> no second card.
	Injury = 0.f;
	Machine.Tick(0.1f);
	Injury = 90.f;
	Machine.Tick(0.1f);
	TestEqual(TEXT("shown once"), Machine.GetState(TEXT("medkit")), ETutorialStepState::Completed);
	return true;
}

TUTORIAL_MEDKIT_TEST(FTutorialMedkitNoFocusTest, "NoFocusCountsAnyone")
bool FTutorialMedkitNoFocusTest::RunTest(const FString&)
{
	FTutorialStepsData Data;
	if (!TutorialMedkitTestData::Parse(*this, TutorialMedkitTestData::Steps, Data))
	{
		return false;
	}
	FTutorialStateMachine Machine;
	Machine.StatProvider = [](FName) { return 80.f; };
	Machine.Load(Data); // no FocusSubjectProvider: no focus chosen
	Machine.StartBeat(TEXT("A1"));
	Machine.Tick(0.1f);
	Machine.Tick(0.1f);
	Machine.HandleEvent(TEXT("ItemUsed.MEDKIT"), TEXT("Medic"));
	TestEqual(TEXT("no focus known: any medkit counts"), Machine.GetState(TEXT("medkit")), ETutorialStepState::Completed);
	return true;
}

TUTORIAL_MEDKIT_TEST(FTutorialMedkitDemoFileTest, "DemoFile")
bool FTutorialMedkitDemoFileTest::RunTest(const FString&)
{
	FTutorialStepsData Data;
	TArray<FString> Warnings;
	FString Error;
	if (!TestTrue(FString::Printf(TEXT("demo steps load (%s)"), *Error), TutorialRules::LoadStepsFile(TEXT("demo_01_steps.json"), Data, Warnings, Error)))
	{
		return false;
	}
	const FTutorialStep* Step = Data.FindStep(TEXT("X_medkit"));
	if (!TestNotNull(TEXT("Demo 01 has the medkit lesson"), Step))
	{
		return false;
	}
	TestTrue(TEXT("beat-less"), Step->Beat.IsNone());
	TestTrue(TEXT("condition trigger on the wounded stat"), Step->Trigger.Type == ETutorialTriggerType::Condition && Step->Conditions.Num() == 1
		&& Step->Conditions[0].Stat == FName(TEXT("squad_injury_max")) && Step->Conditions[0].bAtLeast && Step->Conditions[0].Value > 0.f
		&& Step->Conditions[0].Value <= 100.f);
	TestTrue(TEXT("completes on a medkit on him"), Step->Complete.Event == TEXT("ItemUsed.MEDKIT") && Step->Complete.bFocusSubject);
	TestTrue(TEXT("unlocks what it teaches"), Step->Unlock.Contains(TEXT("Item.Use")) && Step->Unlock.Contains(TEXT("Inventory.Open")));
	TestTrue(TEXT("once"), Step->bOnce);
	return true;
}

#endif
