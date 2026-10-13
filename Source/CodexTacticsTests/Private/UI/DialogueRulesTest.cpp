#include "Misc/AutomationTest.h"
#include "UI/DialogueRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Bottom dialogue window parity with Godot Scenes/ui/dialogue/bottom_dialogue_dialog.gd.

#define DIALOGUE_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Dialogue." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

DIALOGUE_TEST(FDialogueSpeakerStyleTest, "SpeakerStyleByName")
bool FDialogueSpeakerStyleTest::RunTest(const FString&)
{
	using namespace DialogueRules;
	TestEqual(TEXT("Commander role"), GetSpeakerStyle(TEXT("Commander")).Role.ToString(), FString(TEXT("Squad Commander")));
	TestEqual(TEXT("Engineer by surname"), GetSpeakerStyle(TEXT("Engineer Vetrov")).Role.ToString(), FString(TEXT("Field Engineer")));
	TestEqual(TEXT("Medic-sapper"), GetSpeakerStyle(TEXT("Medic-Sapper")).Role.ToString(), FString(TEXT("Medic-Sapper")));
	TestEqual(TEXT("Susanin first"), GetSpeakerStyle(TEXT("Ivan Susanin")).Role.ToString(), FString(TEXT("Local Resident")));
	TestEqual(TEXT("Anyone else"), GetSpeakerStyle(TEXT("HQ")).Role.ToString(), FString(TEXT("Speaker")));
	TestTrue(TEXT("Commander name colour"), GetSpeakerStyle(TEXT("commander")).NameColor.Equals(FLinearColor(0.3f, 0.85f, 1.f)));
	return true;
}

DIALOGUE_TEST(FDialogueButtonTextTest, "NextButtonText")
bool FDialogueButtonTextTest::RunTest(const FString&)
{
	using namespace DialogueRules;
	TestEqual(TEXT("Middle line"), GetNextButtonText(false, TEXT("Custom"), true, true).ToString(), FString(TEXT("Next ▶")));
	TestEqual(TEXT("Custom wins"), GetNextButtonText(true, TEXT("Forward"), true, true).ToString(), FString(TEXT("Forward")));
	TestEqual(TEXT("Recruitment"), GetNextButtonText(true, FString(), true, false).ToString(), FString(TEXT("🤝 Join the Squad")));
	TestEqual(TEXT("Combat"), GetNextButtonText(true, FString(), false, true).ToString(), FString(TEXT("To Battle! ▶")));
	TestEqual(TEXT("Exploration"), GetNextButtonText(true, FString(), false, false).ToString(), FString(TEXT("Understood! ▶")));
	TestEqual(TEXT("Progress"), GetProgressText(0, 4).ToString(), FString(TEXT("[1 / 4]")));
	return true;
}

DIALOGUE_TEST(FDialogueKeyTest, "SkipKey")
bool FDialogueKeyTest::RunTest(const FString&)
{
	// User request 2026-10-13: Enter tap = next line (on the release), hold 0.6 s = skip the whole conversation (once, while held).
	using namespace DialogueRules;
	TestEqual(TEXT("threshold"), SkipHoldSeconds, 0.6f);
	TestTrue(TEXT("still held, short: nothing yet"), ResolveDialogueKey(0.2f, false, false) == EDialogueKeyAction::None);
	TestTrue(TEXT("tap released: next line"), ResolveDialogueKey(0.2f, true, false) == EDialogueKeyAction::NextLine);
	TestTrue(TEXT("held to the threshold: skip all"), ResolveDialogueKey(0.6f, false, false) == EDialogueKeyAction::SkipAll);
	TestTrue(TEXT("released after a long hold that has not fired yet: skip all"), ResolveDialogueKey(1.f, true, false) == EDialogueKeyAction::SkipAll);
	TestTrue(TEXT("the release after the skip fired: nothing (no extra next line)"), ResolveDialogueKey(1.f, true, true) == EDialogueKeyAction::None);
	TestTrue(TEXT("held on after the skip: nothing"), ResolveDialogueKey(2.f, false, true) == EDialogueKeyAction::None);
	TestTrue(TEXT("custom threshold"), ResolveDialogueKey(0.3f, false, false, 0.25f) == EDialogueKeyAction::SkipAll);
	const FString Hint = GetSkipHintText(TEXT("Enter"), TEXT("Hold Enter")).ToString();
	TestTrue(FString::Printf(TEXT("hint names both keys: '%s'"), *Hint), Hint.Contains(TEXT("[Enter] next")) && Hint.Contains(TEXT("[Hold Enter] skip")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
