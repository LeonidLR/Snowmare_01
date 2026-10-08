#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Gameplay tags of the frontend UI framework (no Godot reference: the Godot menus were plain scene nodes).
 * Layers (UCodexPrimaryLayout stacks, bottom to top): Game < GameMenu < Menu < Modal.
 * Screens: keys of UCodexFrontendSettings::ScreenClasses (Project Settings > Game > Codex Frontend).
 */
namespace CodexUITags
{
	CODEXTACTICS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Layer_Game);
	CODEXTACTICS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Layer_GameMenu);
	CODEXTACTICS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Layer_Menu);
	CODEXTACTICS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Layer_Modal);

	CODEXTACTICS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Screen_Title);
	CODEXTACTICS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Screen_MainMenu);
	CODEXTACTICS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Screen_SaveSlots);
	CODEXTACTICS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Screen_Options);
	CODEXTACTICS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Screen_Credits);
	CODEXTACTICS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Screen_Pause);
	CODEXTACTICS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Screen_Confirm);
}
