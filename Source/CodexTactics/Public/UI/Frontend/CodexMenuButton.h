#pragma once

#include "CoreMinimal.h"
#include "CommonButtonBase.h"
#include "UI/Frontend/CodexDefaultTree.h"
#include "CodexMenuButton.generated.h"

class UTextBlock;

/**
 * Menu entry button (CommonUI): a label, a description line the owning screen shows while the entry is hovered /
 * focused, an entry id and a camera anchor id (frontend: the menu camera blends to the ACodexMenuCameraAnchor with that
 * id). Look: the button Style (UCommonButtonStyle Blueprint) and the WBP_MenuButton tree.
 * Widget names: ButtonLabel (UTextBlock or UCommonTextBlock; a UCommonTextBlock gets the style's text style per state).
 * No Godot reference (2026-10-08 user decision: professional frontend).
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API UCodexMenuButton : public UCommonButtonBase, public ICodexDefaultTree
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|UI")
	void SetButtonText(const FText& InText);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|UI")
	FText GetButtonText() const { return ButtonText; }

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|UI")
	void SetDescriptionText(const FText& InText) { DescriptionText = InText; }

	/** The description, or the disabled reason while the entry is disabled with one. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|UI")
	FText GetDisplayedDescription() const;

	/** Enables / disables the entry; a reason replaces the description while disabled ("Saving is disabled during combat"). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|UI")
	void SetEntryEnabled(bool bEnabled, const FText& DisabledReason = FText());

	UFUNCTION(BlueprintPure, Category = "CodexTactics|UI")
	FName GetEntryId() const { return EntryId; }

	/** CameraAnchorId, or the entry id when unset. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|UI")
	FName GetCameraAnchorId() const { return CameraAnchorId.IsNone() ? EntryId : CameraAnchorId; }

	void SetEntryId(FName InId) { EntryId = InId; }

	virtual void BuildDefaultTree(UWidgetTree& Tree) const override;

	/** Label shown on the button. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|UI", meta = (ExposeOnSpawn = true))
	FText ButtonText;

	/** Line shown by the screen (bottom-left) while this entry is hovered / focused. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|UI", meta = (MultiLine = true, ExposeOnSpawn = true))
	FText DescriptionText;

	/** Which entry this is (main menu: Continue, NewGame, LoadGame, Options, Credits, Quit; pause: Resume, Save, ...). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|UI")
	FName EntryId;

	/** Menu camera anchor while this entry is selected (frontend); empty = EntryId. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|UI")
	FName CameraAnchorId;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativePreConstruct() override;
	virtual void NativeOnCurrentTextStyleChanged() override;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|UI", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ButtonLabel;

private:
	void RefreshLabel();

	FText DisabledReasonText;
};
