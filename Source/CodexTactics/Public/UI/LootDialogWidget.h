#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Interactables/LootRules.h"
#include "LootDialogWidget.generated.h"

class ALootCrateActor;
class UTextBlock;
class UUniformGridPanel;
class ULootDialogWidget;

/** Button of one loot line: remembers which item it takes. */
UCLASS()
class CODEXTACTICS_API ULootEntryButton : public UButton
{
	GENERATED_BODY()

public:
	void Setup(ULootDialogWidget* InOwner, ELootItem InItem);

	ELootItem GetItem() const { return Item; }

private:
	UFUNCTION()
	void HandleClicked();

	TWeakObjectPtr<ULootDialogWidget> Owner;
	ELootItem Item = ELootItem::Medkit;
};

/**
 * Loot dialog of an opened supply crate: title (crate name), hint, one button per item (two columns),
 * «📦 Take ALL» and «✖ Close». Built in C++; a Widget Blueprint subclass can restyle it by naming its widgets
 * TitleText, SubtitleText, ItemsGrid, LootAllButton, LootAllText, CloseButton, CloseText.
 * Sprint 13 (two-way crates): a storable line can be dragged onto an operative (model / portrait) or the inventory
 * drawer (take-out with the split dialog and capacity clamp); an inventory line dropped on the window stores it in the
 * crate; the list refreshes when the crate's stash changes.
 * Godot reference: Scenes/ui/inventory/loot_dialog.gd (LootDialogController, 540 x 420, green frame).
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API ULootDialogWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Shows / refreshes the dialog for Crate. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Loot")
	void ShowCrate(ALootCrateActor* Crate);

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Loot")
	void HideDialog();

	/** An item button was pressed. */
	void HandleItem(ELootItem Item);

	/** The crate on show (null when hidden). */
	ALootCrateActor* GetShownCrate() const { return ShownCrate.Get(); }

	/** Sprint 13: the take-out drag payload of a loot line (null for the bonus weapon / clothing or an empty line). */
	class UInventoryDragDropOperation* CreateDragOperation(ELootItem Item);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnPreviewMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation) override;
	virtual void NativeOnDragCancelled(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Loot", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Loot", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SubtitleText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Loot", meta = (BindWidgetOptional))
	TObjectPtr<UUniformGridPanel> ItemsGrid;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Loot", meta = (BindWidgetOptional))
	TObjectPtr<UButton> LootAllButton;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Loot", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LootAllText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Loot", meta = (BindWidgetOptional))
	TObjectPtr<UButton> CloseButton;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Loot", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CloseText;

private:
	void BuildDefaultLayout();
	UTextBlock* MakeText(const FName& Name, int32 Size, const FLinearColor& Color);

	UFUNCTION()
	void HandleLootAll();

	UFUNCTION()
	void HandleClose();

	/** Entry button under an absolute screen position. */
	ULootEntryButton* FindEntryAt(const FVector2D& ScreenPosition) const;

	UPROPERTY()
	TArray<TObjectPtr<ULootEntryButton>> EntryButtons;

	TWeakObjectPtr<ALootCrateActor> ShownCrate;
	int32 ShownRevision = -1;
	TWeakObjectPtr<ULootEntryButton> PressedEntry;
};
