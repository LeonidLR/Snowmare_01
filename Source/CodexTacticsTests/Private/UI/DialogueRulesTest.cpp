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
	TestEqual(TEXT("Commander role"), GetSpeakerStyle(TEXT("Командир")).Role.ToString(), FString(TEXT("Командир отряда")));
	TestEqual(TEXT("Engineer by surname"), GetSpeakerStyle(TEXT("Инженер Ветров")).Role.ToString(), FString(TEXT("Инженер-техник")));
	TestEqual(TEXT("Medic-sapper"), GetSpeakerStyle(TEXT("Медик-сапёр")).Role.ToString(), FString(TEXT("Медик-сапёр")));
	TestEqual(TEXT("Susanin first"), GetSpeakerStyle(TEXT("Иван Сусанин")).Role.ToString(), FString(TEXT("Местный житель")));
	TestEqual(TEXT("Anyone else"), GetSpeakerStyle(TEXT("ШТАБ")).Role.ToString(), FString(TEXT("Собеседник")));
	TestTrue(TEXT("Commander name colour"), GetSpeakerStyle(TEXT("командир")).NameColor.Equals(FLinearColor(0.3f, 0.85f, 1.f)));
	return true;
}

DIALOGUE_TEST(FDialogueButtonTextTest, "NextButtonText")
bool FDialogueButtonTextTest::RunTest(const FString&)
{
	using namespace DialogueRules;
	TestEqual(TEXT("Middle line"), GetNextButtonText(false, TEXT("Custom"), true, true).ToString(), FString(TEXT("Далее ▶")));
	TestEqual(TEXT("Custom wins"), GetNextButtonText(true, TEXT("Вперёд"), true, true).ToString(), FString(TEXT("Вперёд")));
	TestEqual(TEXT("Recruitment"), GetNextButtonText(true, FString(), true, false).ToString(), FString(TEXT("🤝 Вступить в отряд")));
	TestEqual(TEXT("Combat"), GetNextButtonText(true, FString(), false, true).ToString(), FString(TEXT("В бой! ▶")));
	TestEqual(TEXT("Exploration"), GetNextButtonText(true, FString(), false, false).ToString(), FString(TEXT("Понял! ▶")));
	TestEqual(TEXT("Progress"), GetProgressText(0, 4).ToString(), FString(TEXT("[1 / 4]")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
