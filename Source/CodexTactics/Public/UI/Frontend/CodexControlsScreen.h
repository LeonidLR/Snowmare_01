#pragma once

#include "CoreMinimal.h"
#include "UI/Frontend/CodexActivatableScreen.h"
#include "CodexControlsScreen.generated.h"

class UScrollBox;

/**
 * CONTROLS reference (F1 anywhere in the game, or CONTROLS in the pause menu; GameMenu layer): every player command with
 * its key(s) in sections - generated from the PlayerCommands registry through CodexControlsRules, so it never diverges
 * from it (a rebound key shows its current label). Opened with F1 it pauses the game (world pause, like the Esc menu) and
 * resumes on close; opened over the pause menu the world is already paused and BACK returns to the menu. F1 or Esc
 * (the back action) closes it.
 * Widget names (WBP_Controls): TitleText, ControlsList (a UScrollBox; the rows are added at run time - section headers
 * and key / description lines), BackButton (EntryId Back). The artist restyles the frame; the list contents stay in C++.
 * No Godot reference (2026-10-11 user decision: tester demo controls reference).
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API UCodexControlsScreen : public UCodexActivatableScreen
{
	GENERATED_BODY()

public:
	UCodexControlsScreen();

	virtual void BuildDefaultTree(UWidgetTree& Tree) const override;

	/** Section titles as listed (what the smoke / tests check). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|UI")
	TArray<FString> GetSectionTitles() const { return BuiltSectionTitles; }

	/** Number of command rows listed. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|UI")
	int32 GetRowCount() const { return BuiltRowCount; }

	/** True while this screen paused the world itself (opened with F1 outside the pause menu). */
	bool PausedTheWorld() const { return bPausedWorld; }

protected:
	virtual void BindScreenWidgets() override;
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;
	virtual void RefreshScreen() override;
	virtual bool NativeOnHandleBackAction() override;

	/** Fills ControlsList from the registry (rebuilt when the key labels changed since the last build). */
	void PopulateList();

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|UI", meta = (BindWidgetOptional))
	TObjectPtr<UScrollBox> ControlsList;

private:
	TArray<FString> BuiltSectionTitles;
	int32 BuiltRowCount = 0;
	FString BuiltSignature;
	bool bPausedWorld = false;
};
