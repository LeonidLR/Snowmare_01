#include "Misc/AutomationTest.h"
#include "Core/ActorLabelTag.h"
#include "Tutorial/TutorialRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Packaged-build fix 2026-10-23: actor labels are editor-only data, so the cook stamps them as "Label=<label>" tags and a
// packaged game reads them back (the tutorial's route / enemy / volume selectors). UE-only, no Godot reference.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorLabelTagRoundTripTest, "CodexTactics.Core.ActorLabelTag.RoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FActorLabelTagRoundTripTest::RunTest(const FString&)
{
	TestTrue(TEXT("empty label -> no tag"), ActorLabelTag::MakeTag(FString()).IsNone());
	const FName Tag = ActorLabelTag::MakeTag(TEXT("PatrolRoute_A4_Hounds"));
	TestEqual(TEXT("tag text"), Tag.ToString(), FString(TEXT("Label=PatrolRoute_A4_Hounds")));

	const TArray<FName> Tags = { FName(TEXT("Enemy")), Tag, FName(TEXT("Hut")) };
	TestEqual(TEXT("label read back among other tags"), ActorLabelTag::FindInTags(Tags), FString(TEXT("PatrolRoute_A4_Hounds")));
	TestEqual(TEXT("no label tag -> empty"), ActorLabelTag::FindInTags({ FName(TEXT("Enemy")) }), FString());

	// The label read back from the tag matches the selector, the raw tag alone does not (no accidental tag match).
	const FString Label = ActorLabelTag::FindInTags(Tags);
	TestTrue(TEXT("selector matches the cooked label"),
		TutorialRules::MatchesActorSelector(TEXT("PatrolRoute_A4_Hounds"), FName(TEXT("PatrolRouteActor_3")), Label, Tags));
	TestFalse(TEXT("selector does not match without the label"),
		TutorialRules::MatchesActorSelector(TEXT("PatrolRoute_A4_Hounds"), FName(TEXT("PatrolRouteActor_3")), FString(), Tags));
	return true;
}

#endif
