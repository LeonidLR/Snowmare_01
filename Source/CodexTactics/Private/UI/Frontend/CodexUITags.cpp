#include "UI/Frontend/CodexUITags.h"

namespace CodexUITags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Layer_Game, "Codex.UI.Layer.Game", "In-game HUD screens (lowest layer)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Layer_GameMenu, "Codex.UI.Layer.GameMenu", "In-game menus over the HUD (pause menu)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Layer_Menu, "Codex.UI.Layer.Menu", "Frontend / full-screen menus");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Layer_Modal, "Codex.UI.Layer.Modal", "Confirm dialogs over everything");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Screen_Title, "Codex.UI.Screen.Title", "Press any key");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Screen_MainMenu, "Codex.UI.Screen.MainMenu", "Frontend main menu");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Screen_SaveSlots, "Codex.UI.Screen.SaveSlots", "Save / load slot list");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Screen_Options, "Codex.UI.Screen.Options", "Options (placeholder in phase 1)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Screen_Credits, "Codex.UI.Screen.Credits", "Credits");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Screen_Pause, "Codex.UI.Screen.Pause", "In-game pause menu (Esc)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Screen_Confirm, "Codex.UI.Screen.Confirm", "Yes / No or OK dialog");
}
