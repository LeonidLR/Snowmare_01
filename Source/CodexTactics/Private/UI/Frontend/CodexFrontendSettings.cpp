#include "UI/Frontend/CodexFrontendSettings.h"
#include "UI/Frontend/CodexUITags.h"

namespace CodexFrontendDefaults
{
	TSoftClassPtr<UCodexActivatableScreen> Wbp(const TCHAR* Name)
	{
		return TSoftClassPtr<UCodexActivatableScreen>(FSoftObjectPath(FString::Printf(TEXT("/Game/UI/Frontend/%s.%s_C"), Name, Name)));
	}
}

UCodexFrontendSettings::UCodexFrontendSettings()
{
	// Defaults = the placeholder Widget Blueprints made by Scripts/Editor/create_frontend_assets.py (the user restyles them).
	PrimaryLayoutClass = TSoftClassPtr<UCodexPrimaryLayout>(FSoftObjectPath(TEXT("/Game/UI/Frontend/WBP_PrimaryLayout.WBP_PrimaryLayout_C")));
	ScreenClasses.Add(CodexUITags::Screen_Title, CodexFrontendDefaults::Wbp(TEXT("WBP_TitleScreen")));
	ScreenClasses.Add(CodexUITags::Screen_MainMenu, CodexFrontendDefaults::Wbp(TEXT("WBP_MainMenu")));
	ScreenClasses.Add(CodexUITags::Screen_SaveSlots, CodexFrontendDefaults::Wbp(TEXT("WBP_SaveSlots")));
	ScreenClasses.Add(CodexUITags::Screen_Options, CodexFrontendDefaults::Wbp(TEXT("WBP_Options")));
	ScreenClasses.Add(CodexUITags::Screen_Credits, CodexFrontendDefaults::Wbp(TEXT("WBP_Credits")));
	ScreenClasses.Add(CodexUITags::Screen_Pause, CodexFrontendDefaults::Wbp(TEXT("WBP_PauseMenu")));
	ScreenClasses.Add(CodexUITags::Screen_Confirm, CodexFrontendDefaults::Wbp(TEXT("WBP_ConfirmDialog")));
	FrontendLevel = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Maps/L_MainMenu.L_MainMenu")));
	NewGameLevel = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Maps/L_MovementTest.L_MovementTest")));
}
