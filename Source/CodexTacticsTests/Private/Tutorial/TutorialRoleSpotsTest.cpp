#include "Misc/AutomationTest.h"
#include "Tutorial/TutorialRules.h"
#include "Tutorial/TutorialStateMachine.h"

#if WITH_DEV_AUTOMATION_TESTS

// 2026-10-19 (game designer, A3 cover field): "take cover one by one" is role-specific - the Medic-Sapper behind the tall slab, the
// Commander at the middle low wall, the Engineer behind the long low wall in front. "complete": { "event": "CoverEntered", "count": 3,
// "distinct_operatives": true, "role_spots": true }: an operative counts only at HIS role's spot (RoleSpotProvider), any order;
// HandleRoleSpotPresence counts one already in cover there (also when he got there before the card).

#define TUTORIAL_ROLE_SPOTS_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Tutorial.RoleSpots." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace TutorialRoleSpotsTestData
{
	const TCHAR* Steps = TEXT(R"JSON({
		"id": "rolespots", "start_beat": "A3", "beats": [ { "id": "A3" } ],
		"steps": [
			{ "id": "A3_cover", "beat": "A3", "kind": "hint_card", "text_id": "TUT_A3_COVER_EACH", "trigger": { "type": "manual" },
			  "complete": { "event": "CoverEntered", "count": 3, "distinct_operatives": true, "role_spots": true } },
			{ "id": "A3_bound", "beat": "A3", "kind": "objective", "text_id": "OBJ_A6_BOUND", "trigger": { "type": "manual" },
			  "complete": { "event": "CoverEntered", "count": 4 } }
		] })JSON");

	bool Parse(FAutomationTestBase& Test, FTutorialStepsData& Out)
	{
		TArray<FString> Warnings;
		FString Error;
		const bool bOk = TutorialRules::ParseSteps(Steps, Out, Warnings, Error);
		Test.TestTrue(FString::Printf(TEXT("steps parse (%s)"), *Error), bOk);
		Test.TestEqual(TEXT("no warnings"), Warnings.Num(), 0);
		for (const FString& Warning : Warnings)
		{
			Test.AddInfo(Warning);
		}
		return bOk;
	}
}

TUTORIAL_ROLE_SPOTS_TEST(FTutorialRoleSpotsParseTest, "Parse")
bool FTutorialRoleSpotsParseTest::RunTest(const FString&)
{
	FTutorialStepsData Data;
	if (!TutorialRoleSpotsTestData::Parse(*this, Data))
	{
		return false;
	}
	const FTutorialStep* Step = Data.FindStep(TEXT("A3_cover"));
	TestTrue(TEXT("role_spots parsed"), Step && Step->Complete.bRoleSpots && Step->Complete.bDistinctSubjects && Step->Complete.Count == 3);
	const FTutorialStep* Bound = Data.FindStep(TEXT("A3_bound"));
	TestTrue(TEXT("off by default"), Bound && !Bound->Complete.bRoleSpots);

	FTutorialStepsData Bad;
	TArray<FString> Warnings;
	FString Error;
	TutorialRules::ParseSteps(TEXT(R"JSON({ "steps": [ { "id": "s", "kind": "hint_card", "trigger": { "type": "manual" },
		"complete": { "volume": "VOL_X", "role_spots": true } } ] })JSON"), Bad, Warnings, Error);
	TestTrue(TEXT("role_spots without an event warns"), Warnings.ContainsByPredicate([](const FString& W) { return W.Contains(TEXT("role_spots")); }));
	return true;
}

TUTORIAL_ROLE_SPOTS_TEST(FTutorialRoleSpotsFlowTest, "EachAtHisOwnSpot")
bool FTutorialRoleSpotsFlowTest::RunTest(const FString&)
{
	FTutorialStepsData Data;
	if (!TutorialRoleSpotsTestData::Parse(*this, Data))
	{
		return false;
	}
	// Where each operative stands now: true = at his own role's spot.
	TMap<FName, bool> AtSpot = { { TEXT("Medic"), false }, { TEXT("Commander"), false }, { TEXT("Engineer"), false } };
	FTutorialStateMachine Machine;
	Machine.RoleSpotProvider = [&AtSpot](FName StepId, FName SubjectId)
	{
		const bool* At = AtSpot.Find(SubjectId);
		return StepId == FName(TEXT("A3_cover")) && At && *At;
	};
	Machine.Load(Data);
	Machine.StartBeat(TEXT("A3"));
	Machine.TriggerStep(TEXT("A3_cover"));
	Machine.TriggerStep(TEXT("A3_bound"));
	Machine.Tick(0.1f);
	TestEqual(TEXT("the cover card is up"), Machine.GetState(TEXT("A3_cover")), ETutorialStepState::Active);

	Machine.HandleEvent(TEXT("CoverEntered"), TEXT("Medic"));
	TestEqual(TEXT("the Medic-Sapper in someone else's cover: not counted"), Machine.GetProgress(TEXT("A3_cover")), 0);
	TestEqual(TEXT("... the plain objective still counts any cover"), Machine.GetProgress(TEXT("A3_bound")), 1);
	AtSpot[TEXT("Commander")] = true;
	Machine.HandleEvent(TEXT("CoverEntered"), TEXT("Commander"));
	TestEqual(TEXT("the Commander at his spot (first, any order): 1 / 3"), Machine.GetProgress(TEXT("A3_cover")), 1);
	Machine.HandleEvent(TEXT("CoverEntered"), TEXT("Commander"));
	TestEqual(TEXT("the same man again: still 1"), Machine.GetProgress(TEXT("A3_cover")), 1);
	TestFalse(TEXT("presence does not count him twice"), Machine.HandleRoleSpotPresence(TEXT("Commander")));
	TestFalse(TEXT("presence away from his spot: nothing"), Machine.HandleRoleSpotPresence(TEXT("Medic")));

	// The Engineer was already in cover at his spot (took it before the event reached the card): presence counts him.
	AtSpot[TEXT("Engineer")] = true;
	const int32 BoundBefore = Machine.GetProgress(TEXT("A3_bound"));
	TestTrue(TEXT("the Engineer already in cover at his spot counts"), Machine.HandleRoleSpotPresence(TEXT("Engineer")));
	TestEqual(TEXT("2 / 3"), Machine.GetProgress(TEXT("A3_cover")), 2);
	TestEqual(TEXT("presence never counts for the other steps listening to CoverEntered"), Machine.GetProgress(TEXT("A3_bound")), BoundBefore);

	AtSpot[TEXT("Medic")] = true;
	Machine.HandleEvent(TEXT("CoverEntered"), TEXT("Medic"));
	Machine.Tick(0.1f);
	TestEqual(TEXT("the Medic-Sapper behind HIS slab: done"), Machine.GetState(TEXT("A3_cover")), ETutorialStepState::Completed);
	const TSet<FName>* Who = Machine.GetContributors(TEXT("A3_cover"));
	TestTrue(TEXT("all three counted"), Who && Who->Num() == 3);
	return true;
}

TUTORIAL_ROLE_SPOTS_TEST(FTutorialRoleSpotsNoProviderTest, "NoSpotsAnyCover")
bool FTutorialRoleSpotsNoProviderTest::RunTest(const FString&)
{
	FTutorialStepsData Data;
	if (!TutorialRoleSpotsTestData::Parse(*this, Data))
	{
		return false;
	}
	FTutorialStateMachine Machine; // no provider (a map without role markers): any cover counts
	Machine.Load(Data);
	Machine.StartBeat(TEXT("A3"));
	Machine.TriggerStep(TEXT("A3_cover"));
	Machine.Tick(0.1f);
	Machine.HandleEvent(TEXT("CoverEntered"), TEXT("A"));
	Machine.HandleEvent(TEXT("CoverEntered"), TEXT("B"));
	Machine.HandleEvent(TEXT("CoverEntered"), TEXT("C"));
	Machine.Tick(0.1f);
	TestEqual(TEXT("three different operatives anywhere: done"), Machine.GetState(TEXT("A3_cover")), ETutorialStepState::Completed);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
