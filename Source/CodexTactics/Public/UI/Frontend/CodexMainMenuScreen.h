#pragma once

#include "CoreMinimal.h"
#include "UI/Frontend/CodexActivatableScreen.h"
#include "UI/Frontend/CodexFrontendRules.h"
#include "CodexMainMenuScreen.generated.h"

/**
 * Frontend main menu: CONTINUE (latest save; disabled without one) / NEW GAME / LOAD GAME / OPTIONS / CREDITS / QUIT
 * (confirm). Selecting an entry shows its description bottom-left and blends the menu camera to the entry's anchor
 * (ACodexMenuCameraAnchor with the button's CameraAnchorId). Back returns to the title screen.
 * Widget names (WBP_MainMenu): ContinueButton, NewGameButton, LoadGameButton, OptionsButton, CreditsButton, QuitButton
 * (UCodexMenuButton with EntryId Continue / NewGame / LoadGame / Options / Credits / Quit), DescriptionText,
 * BackHintText, GameTitleText.
 * No Godot reference (replaces the Godot-style start menu «Start game / Start battle», user decision 2026-10-08).
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API UCodexMainMenuScreen : public UCodexActivatableScreen
{
	GENERATED_BODY()

public:
	UCodexMainMenuScreen();

	UFUNCTION(BlueprintPure, Category = "CodexTactics|UI")
	bool IsEntryEnabled(ECodexMainMenuEntry Entry) const;

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|UI")
	void SelectMenuEntry(ECodexMainMenuEntry Entry) { SelectEntry(CodexFrontendRules::GetEntryId(Entry)); }

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|UI")
	void ActivateMenuEntry(ECodexMainMenuEntry Entry) { ActivateEntry(CodexFrontendRules::GetEntryId(Entry)); }

	virtual void BuildDefaultTree(UWidgetTree& Tree) const override;

protected:
	virtual void RefreshScreen() override;
	virtual void OnEntrySelected(UCodexMenuButton& Button) override;
	virtual void OnEntryActivated(FName EntryId) override;
	virtual bool NativeOnHandleBackAction() override;
};
