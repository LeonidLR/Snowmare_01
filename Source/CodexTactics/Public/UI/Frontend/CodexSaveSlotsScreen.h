#pragma once

#include "CoreMinimal.h"
#include "UI/Frontend/CodexActivatableScreen.h"
#include "UI/Frontend/CodexFrontendRules.h"
#include "UI/Frontend/CodexMenuButton.h"
#include "UI/Frontend/CodexSaveBridge.h"
#include "CodexSaveSlotsScreen.generated.h"

class UEditableTextBox;
class UPanelWidget;
class UTextBlock;

/**
 * One save slot line (button): title, date, level / stage, play time.
 * Widget names (WBP_SaveSlotEntry): SlotTitleText, SlotDateText, SlotLevelText, SlotPlayTimeText.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API UCodexSaveSlotEntry : public UCodexMenuButton
{
	GENERATED_BODY()

public:
	void SetSlot(const FCodexSaveSlotView& InSlot);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|UI")
	FString GetSlotName() const { return SlotData.SlotName; }

	virtual void BuildDefaultTree(UWidgetTree& Tree) const override;

	/** Blueprint hook after the slot data changed (for extra visuals like a thumbnail). */
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|UI")
	void OnSlotSet(const FString& SlotName, const FString& Title, const FString& DateTime, const FString& Level, const FString& PlayTime);

protected:
	virtual void NativeOnInitialized() override;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|UI", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SlotTitleText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|UI", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SlotDateText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|UI", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SlotLevelText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|UI", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SlotPlayTimeText;

private:
	void ApplySlot();

	FCodexSaveSlotView SlotData;
};

/**
 * SAVE GAME / LOAD GAME slot list (frontend LOAD GAME and the pause menu SAVE / LOAD).
 * Load: clicking a slot loads it (frontend: opens its level, then loads). Save: clicking a slot puts its name in the
 * name box; SAVE writes a new slot, OVERWRITE (existing name) asks first. DELETE removes the selected slot (confirm).
 * Widget names (WBP_SaveSlots): TitleText, SlotList (any panel; entries of SlotEntryClass are added), EmptyText,
 * SlotNameBox (UEditableTextBox; hidden in Load mode), SaveButton (EntryId Save), DeleteButton (EntryId Delete),
 * BackButton (EntryId Back), StatusText, DescriptionText.
 * Godot reference (behaviour of the old dialog it replaces): Scenes/ui/pause/save_load_dialog.gd.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API UCodexSaveSlotsScreen : public UCodexActivatableScreen
{
	GENERATED_BODY()

public:
	UCodexSaveSlotsScreen();

	void SetMode(ECodexSaveScreenMode InMode) { Mode = InMode; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|UI")
	ECodexSaveScreenMode GetMode() const { return Mode; }

	/** Rebuilds the slot list from the save system. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|UI")
	void RefreshSlots();

	/** Load: loads the slot; Save: picks its name. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|UI")
	void ChooseSlot(const FString& SlotName);

	/** Save mode: the SAVE / OVERWRITE button. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|UI")
	void PressSave();

	/** Asks, then deletes the selected slot. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|UI")
	void PressDelete();

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|UI")
	void SetSlotName(const FString& Name);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|UI")
	FString GetSlotName() const;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|UI")
	int32 GetSlotCount() const { return Slots.Num(); }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|UI")
	FText GetStatusLine() const { return StatusValue; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|UI")
	FText GetSaveButtonLabel() const;

	virtual void BuildDefaultTree(UWidgetTree& Tree) const override;

	/** Line widget for each slot (WBP_SaveSlotEntry). */
	UPROPERTY(EditDefaultsOnly, Category = "CodexTactics|UI")
	TSubclassOf<UCodexSaveSlotEntry> SlotEntryClass;

protected:
	virtual void BindScreenWidgets() override;
	virtual void RefreshScreen() override;
	virtual void OnEntryActivated(FName EntryId) override;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|UI", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|UI", meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> SlotList;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|UI", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> EmptyText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|UI", meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> SlotNameBox;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|UI", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;

private:
	void SetStatus(const FText& Text);
	void SaveNow(const FString& SlotName);
	void UpdateSaveButton();

	UFUNCTION()
	void HandleSlotNameChanged(const FText& Text);

	ECodexSaveScreenMode Mode = ECodexSaveScreenMode::Load;
	TArray<FCodexSaveSlotView> Slots;
	FString SelectedSlot;
	/** Slot name when the screen has no SlotNameBox. */
	FString PendingName;
	FText StatusValue;
};
