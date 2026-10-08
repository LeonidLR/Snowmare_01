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

#endif // WITH_DEV_AUTOMATION_TESTS
