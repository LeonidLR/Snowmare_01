#include "Misc/AutomationTest.h"
#include "Blueprint/WidgetTree.h"
#include "UI/Frontend/CodexConfirmDialog.h"
#include "UI/Frontend/CodexFrontendRules.h"
#include "UI/Frontend/CodexFrontendSettings.h"
#include "UI/Frontend/CodexInfoScreens.h"
#include "UI/Frontend/CodexMainMenuScreen.h"
#include "UI/Frontend/CodexMenuButton.h"
#include "UI/Frontend/CodexPauseMenuScreen.h"
#include "UI/Frontend/CodexPrimaryLayout.h"
#include "UI/Frontend/CodexSaveSlotsScreen.h"
#include "UI/Frontend/CodexTitleScreen.h"
#include "UI/Frontend/CodexUISubsystem.h"
#include "UI/Frontend/CodexUITags.h"

#if WITH_DEV_AUTOMATION_TESTS

// Frontend framework (2026-10-08 user decision, no Godot reference): pure menu rules and the widget-name contract
// between the C++ screens and their placeholder trees (the WBP_* assets carry the same trees).

#define FRONTEND_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.UI.Frontend." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

FRONTEND_TEST(FFrontendEntryRulesTest, "EntryRules")
bool FFrontendEntryRulesTest::RunTest(const FString&)
{
	using namespace CodexFrontendRules;
	for (const ECodexMainMenuEntry Entry : { ECodexMainMenuEntry::Continue, ECodexMainMenuEntry::NewGame, ECodexMainMenuEntry::LoadGame,
		ECodexMainMenuEntry::Options, ECodexMainMenuEntry::Credits, ECodexMainMenuEntry::Quit })
	{
		ECodexMainMenuEntry Parsed = ECodexMainMenuEntry::Quit;
		TestTrue(*FString::Printf(TEXT("%s round-trips"), *GetEntryId(Entry).ToString()), ParseEntryId(GetEntryId(Entry), Parsed) && Parsed == Entry);
	}
	ECodexMainMenuEntry Unused;
	TestFalse(TEXT("Unknown id"), ParseEntryId(TEXT("StartBattle"), Unused));
	TestEqual(TEXT("Anchor id of NEW GAME"), GetEntryId(ECodexMainMenuEntry::NewGame), FName(TEXT("NewGame")));
	TestFalse(TEXT("CONTINUE needs a save"), IsEntryEnabled(ECodexMainMenuEntry::Continue, false));
	TestTrue(TEXT("CONTINUE with a save"), IsEntryEnabled(ECodexMainMenuEntry::Continue, true));
	TestTrue(TEXT("LOAD GAME without saves stays enabled"), IsEntryEnabled(ECodexMainMenuEntry::LoadGame, false));
	TestTrue(TEXT("NEW GAME always"), IsEntryEnabled(ECodexMainMenuEntry::NewGame, false));
	return true;
}

FRONTEND_TEST(FFrontendFormatRulesTest, "FormatAndBlend")
bool FFrontendFormatRulesTest::RunTest(const FString&)
{
	using namespace CodexFrontendRules;
	TestEqual(TEXT("Unknown play time"), FormatPlayTime(-1.f), FString(TEXT("--:--")));
	TestEqual(TEXT("Seconds"), FormatPlayTime(9.9f), FString(TEXT("0:09")));
	TestEqual(TEXT("Minutes"), FormatPlayTime(725.f), FString(TEXT("12:05")));
	TestEqual(TEXT("Hours"), FormatPlayTime(3600.f * 2 + 61.f), FString(TEXT("2:01:01")));
	TestEqual(TEXT("Anchor override"), ResolveBlendTime(0.4f, 1.2f), 0.4f);
	TestEqual(TEXT("Zero override = cut"), ResolveBlendTime(0.f, 1.2f), 0.f);
	TestEqual(TEXT("Negative override = default"), ResolveBlendTime(-1.f, 1.2f), 1.2f);
	TestEqual(TEXT("Never negative"), ResolveBlendTime(-1.f, -3.f), 0.f);
	TestEqual(TEXT("New slot"), GetSaveButtonLabel(false).ToString(), FString(TEXT("SAVE")));
	TestEqual(TEXT("Existing slot"), GetSaveButtonLabel(true).ToString(), FString(TEXT("OVERWRITE")));
	return true;
}

FRONTEND_TEST(FFrontendSettingsTest, "SettingsDefaults")
bool FFrontendSettingsTest::RunTest(const FString&)
{
	const UCodexFrontendSettings& Settings = UCodexFrontendSettings::Get();
	for (const FGameplayTag& Tag : { CodexUITags::Screen_Title.GetTag(), CodexUITags::Screen_MainMenu.GetTag(), CodexUITags::Screen_SaveSlots.GetTag(),
		CodexUITags::Screen_Options.GetTag(), CodexUITags::Screen_Credits.GetTag(), CodexUITags::Screen_Pause.GetTag(), CodexUITags::Screen_Confirm.GetTag() })
	{
		const TSoftClassPtr<UCodexActivatableScreen>* Configured = Settings.ScreenClasses.Find(Tag);
		TestTrue(*FString::Printf(TEXT("%s has a Widget Blueprint"), *Tag.ToString()), Configured && !Configured->IsNull());
		TestNotNull(*FString::Printf(TEXT("%s has a C++ fallback"), *Tag.ToString()), UCodexUISubsystem::GetNativeScreenClass(Tag).Get());
	}
	TestFalse(TEXT("Frontend level"), Settings.FrontendLevel.IsNull());
	TestFalse(TEXT("New game level"), Settings.NewGameLevel.IsNull());
	TestTrue(TEXT("Blend time not negative"), Settings.CameraBlendTime >= 0.f);
	return true;
}

namespace FrontendTreeTest
{
	/** Builds the class's placeholder tree into a fresh tree and returns the names it holds. */
	TSet<FName> BuildNames(UClass* Class)
	{
		TSet<FName> Names;
		UWidgetTree* Tree = NewObject<UWidgetTree>(GetTransientPackage());
		if (const ICodexDefaultTree* Builder = Cast<ICodexDefaultTree>(Class->GetDefaultObject()))
		{
			Builder->BuildDefaultTree(*Tree);
		}
		Tree->ForEachWidget([&Names](UWidget* Widget) { Names.Add(Widget->GetFName()); });
		return Names;
	}
}

FRONTEND_TEST(FFrontendTreeContractTest, "DefaultTreeWidgetNames")
bool FFrontendTreeContractTest::RunTest(const FString&)
{
	// The names the C++ looks up (BindWidgetOptional / GetWidgetFromName); the artist keeps them in the WBP_* assets.
	struct FContract
	{
		UClass* Class;
		TArray<const TCHAR*> Names;
	};
	const FContract Contracts[] = {
		{ UCodexPrimaryLayout::StaticClass(), { TEXT("GameLayer"), TEXT("GameMenuLayer"), TEXT("MenuLayer"), TEXT("ModalLayer") } },
		{ UCodexTitleScreen::StaticClass(), { TEXT("TitleText"), TEXT("PressAnyKeyText") } },
		{ UCodexMainMenuScreen::StaticClass(), { TEXT("ContinueButton"), TEXT("NewGameButton"), TEXT("LoadGameButton"), TEXT("OptionsButton"),
			TEXT("CreditsButton"), TEXT("QuitButton"), TEXT("DescriptionText"), TEXT("BackHintText") } },
		{ UCodexPauseMenuScreen::StaticClass(), { TEXT("ResumeButton"), TEXT("SaveButton"), TEXT("LoadButton"), TEXT("OptionsButton"),
			TEXT("QuitToMenuButton"), TEXT("QuitGameButton"), TEXT("DescriptionText"), TEXT("StatusText") } },
		{ UCodexSaveSlotsScreen::StaticClass(), { TEXT("TitleText"), TEXT("SlotList"), TEXT("EmptyText"), TEXT("SlotNameBox"), TEXT("SaveButton"),
			TEXT("DeleteButton"), TEXT("BackButton"), TEXT("StatusText") } },
		{ UCodexConfirmDialog::StaticClass(), { TEXT("BackgroundBlur"), TEXT("TitleText"), TEXT("MessageText"), TEXT("YesButton"), TEXT("NoButton"), TEXT("OkButton") } },
		{ UCodexOptionsScreen::StaticClass(), { TEXT("TitleText"), TEXT("BackButton") } },
		{ UCodexCreditsScreen::StaticClass(), { TEXT("TitleText"), TEXT("CreditsText"), TEXT("BackButton") } },
		{ UCodexMenuButton::StaticClass(), { TEXT("ButtonLabel") } },
		{ UCodexSaveSlotEntry::StaticClass(), { TEXT("SlotTitleText"), TEXT("SlotDateText"), TEXT("SlotLevelText"), TEXT("SlotPlayTimeText") } },
	};
	for (const FContract& Contract : Contracts)
	{
		const TSet<FName> Names = FrontendTreeTest::BuildNames(Contract.Class);
		for (const TCHAR* Name : Contract.Names)
		{
			TestTrue(*FString::Printf(TEXT("%s tree has %s"), *Contract.Class->GetName(), Name), Names.Contains(FName(Name)));
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
