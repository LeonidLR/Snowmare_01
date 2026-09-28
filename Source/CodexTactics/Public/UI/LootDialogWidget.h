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

private:
	UFUNCTION()
	void HandleClicked();

	TWeakObjectPtr<ULootDialogWidget> Owner;
	ELootItem Item = ELootItem::Medkit;
};

/**
 * Loot dialog of an opened supply crate: title (crate name), hint, one button per item (two columns),
 * «📦 Забрать ВСЁ» and «✖ Закрыть». Built in C++; a Widget Blueprint subclass can restyle it by naming its widgets
 * TitleText, SubtitleText, ItemsGrid, LootAllButton, LootAllText, CloseButton, CloseText.
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

protected:
	virtual void NativeOnInitialized() override;

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
};
