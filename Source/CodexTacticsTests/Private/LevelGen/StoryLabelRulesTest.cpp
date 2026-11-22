// CodexTactics.Debug.StoryLabels.* - the story-showcase labels (designer test 2026-11-22, item 6; Debug/StoryLabelRules, the dev overlay
// UStoryLabelSubsystem): house titles, door words, the switch, the cutaway hiding.

#include "Debug/StoryLabelRules.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#define STORY_LABEL_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Debug.StoryLabels." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

STORY_LABEL_TEST(FStoryLabelTitlesTest, "Titles")
bool FStoryLabelTitlesTest::RunTest(const FString&)
{
	using namespace StoryLabelRules;
	TestEqual(TEXT("story 3"), StoryNumber(TEXT("locked_door")), 3);
	TestEqual(TEXT("story 10"), StoryNumber(TEXT("snow_drift")), 10);
	TestEqual(TEXT("unknown"), StoryNumber(TEXT("nope")), 0);
	TestTrue(TEXT("the designer's form: '3 · <name>'"), StoryTitle(TEXT("locked_door")).StartsWith(TEXT("3 · ")));
	TestEqual(TEXT("an unknown story shows its id"), StoryTitle(TEXT("nope")), FString(TEXT("nope")));
	const FString Two = HouseLabel({ TEXT("locked_door"), TEXT("broken_back_windows") });
	TestTrue(TEXT("two stories joined"), Two.StartsWith(TEXT("3 · ")) && Two.Contains(TEXT(" + 9 · ")));
	TestTrue(TEXT("no story: the open house"), HouseLabel({}).StartsWith(TEXT("1 · ")));
	return true;
}

STORY_LABEL_TEST(FStoryLabelDoorsTest, "Doors")
bool FStoryLabelDoorsTest::RunTest(const FString&)
{
	using namespace StoryLabelRules;
	const FString Locked = DoorLabel(TEXT("locked"), FString(), 3, false);
	const FString Closed = DoorLabel(TEXT("closed"), FString(), 0, false);
	TestFalse(TEXT("a locked door is labelled"), Locked.IsEmpty());
	TestFalse(TEXT("a closed door is labelled"), Closed.IsEmpty());
	TestNotEqual(TEXT("locked and closed differ"), Locked, Closed);
	TestTrue(TEXT("the story that locked it"), Locked.EndsWith(TEXT("3")));
	TestTrue(TEXT("an untouched open doorway: no label"), DoorLabel(TEXT("open"), FString(), 0, false).IsEmpty());
	TestFalse(TEXT("an opened door (doors on) says so"), DoorLabel(TEXT("open"), TEXT("steel"), 0, true).IsEmpty());
	TestTrue(TEXT("the strength is shown"), DoorLabel(TEXT("locked"), TEXT("steel"), 0, true).Len() > DoorLabel(TEXT("locked"), FString(), 0, true).Len());
	TMap<FString, int32> DoorStory;
	DoorStoriesFromBricks(TEXT("broken_back_windows"), { TEXT("window_state(west:0,broken)"), TEXT("door_state(S09_d20,locked)") }, DoorStory);
	TestEqual(TEXT("door_state bricks name their door"), DoorStory.FindRef(TEXT("S09_d20")), 9);
	TestEqual(TEXT("window bricks do not"), DoorStory.Num(), 1);
	return true;
}

STORY_LABEL_TEST(FStoryLabelSwitchTest, "SwitchAndCutaway")
bool FStoryLabelSwitchTest::RunTest(const FString&)
{
	using namespace StoryLabelRules;
	TestTrue(TEXT("default: on the showcase"), ShouldShow(-1, TEXT("L_Gen_StoryShowcase_Kit")));
	TestFalse(TEXT("default: off elsewhere"), ShouldShow(-1, TEXT("L_Gen_Depot_1")));
	TestTrue(TEXT("1: on everywhere"), ShouldShow(1, TEXT("L_Gen_Depot_1")));
	TestFalse(TEXT("0: off"), ShouldShow(0, TEXT("L_Gen_StoryShowcase")));
	TestTrue(TEXT("inside: a door above his storey (cut away) is hidden"), IsDoorLabelHidden(true, 2, 0, false));
	TestTrue(TEXT("inside: a door below his floor is hidden"), IsDoorLabelHidden(true, 0, 1, true));
	TestFalse(TEXT("inside: his storey's doors show"), IsDoorLabelHidden(true, 1, 1, false));
	TestFalse(TEXT("outside: the ground storey's doors show"), IsDoorLabelHidden(false, 0, 0, false));
	TestTrue(TEXT("outside: an upper flat door is hidden (no clutter)"), IsDoorLabelHidden(false, 2, 0, false));
	TestFalse(TEXT("outside: a door a story set always shows"), IsDoorLabelHidden(false, 1, INDEX_NONE, true));
	TestTrue(TEXT("in no house: upper doors hidden"), IsDoorLabelHidden(true, 2, INDEX_NONE, false));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
