#pragma once

#include "CoreMinimal.h"
#include "UI/Frontend/CodexActivatableScreen.h"
#include "CodexPauseMenuScreen.generated.h"

class UTextBlock;

/**
 * In-game PAUSE menu (Esc; GameMenu layer; the world is paused while it is open): RESUME / SAVE GAME (disabled with the
 * reason, e.g. "Saving is disabled during combat") / LOAD GAME / OPTIONS / QUIT TO MAIN MENU (confirm) / QUIT GAME
 * (confirm). Back = RESUME. The tactical pause (Space) is a separate game mechanic and is not touched.
 * Widget names (WBP_PauseMenu): ResumeButton, SaveButton, LoadButton, OptionsButton, QuitToMenuButton, QuitGameButton
 * (EntryId Resume / Save / Load / Options / QuitToMenu / QuitGame), DescriptionText, StatusText (latest save line), TitleText.
 * Godot reference (behaviour of the old menu it replaces): Scenes/ui/pause/pause_menu_dialog.gd, main.gd _open_pause_menu.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API UCodexPauseMenuScreen : public UCodexActivatableScreen
{
	GENERATED_BODY()

public:
	UCodexPauseMenuScreen();

	/** Closes the pause menu (and whatever it opened on the GameMenu layer) and resumes the world. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|UI")
	void Resume();

	UFUNCTION(BlueprintPure, Category = "CodexTactics|UI")
	bool IsSaveEnabled() const;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|UI")
	bool IsLoadEnabled() const;

	/** Why SAVE GAME is disabled ("Saving is disabled during combat"); empty while it is enabled. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|UI")
	FText GetSaveHint() const;

	/** Latest-save line ("Latest save: <title> (<date>)" or "No saved games"). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|UI")
	FText GetStatusLine() const;

	virtual void BuildDefaultTree(UWidgetTree& Tree) const override;

protected:
	virtual void BindScreenWidgets() override;
	virtual void NativeOnActivated() override;
	virtual void RefreshScreen() override;
	virtual void OnEntryActivated(FName EntryId) override;
	virtual bool NativeOnHandleBackAction() override;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|UI", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;
};
