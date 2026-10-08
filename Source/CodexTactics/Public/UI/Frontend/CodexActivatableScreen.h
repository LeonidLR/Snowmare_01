#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "GameplayTagContainer.h"
#include "UI/Frontend/CodexDefaultTree.h"
#include "CodexActivatableScreen.generated.h"

class UCodexMenuButton;
class UTextBlock;

/**
 * Base of every frontend / pause screen (CommonUI activatable widget on a UCodexPrimaryLayout layer).
 * - Placeholder layout: BuildDefaultTree (runtime fallback for the bare C++ class; the WBP_* assets carry the same tree).
 * - Focus: the widget named by DesiredFocusName, else the first enabled UCodexMenuButton.
 * - Menu buttons: hover / keyboard focus selects an entry (OnEntrySelected), click activates it (OnEntryActivated).
 * - Input config stays the game's (no CommonUI input-mode switch): the in-game Enhanced Input / HUD keep working, Esc is
 *   routed by CommonUI's back action (bIsBackHandler) or by the HUD (ACodexTacticsHUD::HandleEscape -> RequestBack).
 * No Godot reference (2026-10-08 user decision: professional frontend).
 */
UCLASS(Abstract, Blueprintable)
class CODEXTACTICS_API UCodexActivatableScreen : public UCommonActivatableWidget, public ICodexDefaultTree
{
	GENERATED_BODY()

public:
	UCodexActivatableScreen();

	/** Closes this screen (deactivates it: the layer stack removes it and re-activates the one below). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|UI")
	void CloseScreen();

	/** Runs the back action (Esc / gamepad B) as CommonUI would: returns false when the screen does not handle back. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|UI")
	bool RequestBack();

	/** The tag it was pushed with (Codex.UI.Screen.*). */
	FGameplayTag GetScreenTag() const { return ScreenTag; }
	void SetScreenTag(FGameplayTag InTag) { ScreenTag = InTag; }

	/** Entry id under the cursor / focus (last one selected). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|UI")
	FName GetSelectedEntryId() const { return SelectedEntryId; }

	/** Selects an entry as hovering / focusing its button would (description line, frontend camera anchor). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|UI")
	void SelectEntry(FName EntryId);

	/** Activates an entry as clicking its button would (no effect for a disabled entry). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|UI")
	void ActivateEntry(FName EntryId);

	/** Menu button of an entry id in this screen, or null. */
	UCodexMenuButton* FindEntryButton(FName EntryId) const;

	/** Text of the description line (DescriptionText widget). */
	FText GetDescriptionLine() const;

	virtual void BuildDefaultTree(UWidgetTree& Tree) const override {}

	/** Button class the placeholder tree uses for its entries (WBP_MenuButton in the WBP_* assets). */
	UPROPERTY(EditDefaultsOnly, Category = "CodexTactics|UI")
	TSubclassOf<UCodexMenuButton> EntryButtonClass;

	/** Widget focused on activation (by name); empty = the first enabled menu button. */
	UPROPERTY(EditDefaultsOnly, Category = "CodexTactics|UI")
	FName DesiredFocusName;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeOnActivated() override;
	virtual UWidget* NativeGetDesiredFocusTarget() const override;

	/** After the tree exists: find the named widgets (BindWidgetOptional does it for WBPs, this for the bare class). */
	virtual void BindScreenWidgets() {}

	/** An entry was selected (hover / focus). Base: shows its description on DescriptionText. */
	virtual void OnEntrySelected(UCodexMenuButton& Button);

	/** An enabled entry was activated (click / Enter / gamepad A). */
	virtual void OnEntryActivated(FName EntryId) {}

	/** Refresh texts / enabled states each time the screen becomes the active one. */
	virtual void RefreshScreen() {}

	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|UI", meta = (DisplayName = "On Entry Selected"))
	void BP_OnEntrySelected(FName EntryId);

	/** Finds a named widget of the tree into a member (keeps a BindWidgetOptional binding). */
	template <typename T>
	void BindNamed(TObjectPtr<T>& Member, const TCHAR* Name)
	{
		if (!Member)
		{
			Member = Cast<T>(GetWidgetFromName(Name));
		}
	}

	/** Helpers for the placeholder trees of subclasses. */
	UCodexMenuButton* MakeEntryButton(UWidgetTree& Tree, FName WidgetName, FName EntryId, const FText& Label, const FText& Description) const;

	/** Bottom-left description line. */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|UI", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DescriptionText;

private:
	void HookEntryButtons();

	UPROPERTY(Transient)
	TArray<TObjectPtr<UCodexMenuButton>> EntryButtons;

	FGameplayTag ScreenTag;
	FName SelectedEntryId;
};
