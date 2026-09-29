#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InventoryDrawerWidget.generated.h"

class UButton;
class UTextBlock;

/** Lines of the drawer, in Godot order. */
UENUM()
enum class EInventoryDrawerSlot : uint8
{
	Turret,
	Barricade,
	Mine,
	Medkit,
	CannedFood,
	Bread,
	Chocolate,
	Matches
};

/**
 * Personal inventory of the leader above the action bar («ИНВ»): engineering items (click: set it up, a squad mate hands
 * one over when needed) and provisions (click / H J K L: use), matches (info). Built in C++ (restyle through a Widget
 * Blueprint subclass).
 * Godot reference: Scenes/ui/inventory/inventory_drawer.gd (layout, update_ui texts), main.gd _toggle_inventory_drawer,
 * _start_placement_for_type, use_squad_item.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API UInventoryDrawerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Inventory")
	void Open();

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Inventory")
	void Close();

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Inventory")
	void Toggle();

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Inventory")
	bool IsOpen() const;

	/** Title / line texts and enabled states from the current leader. */
	void Refresh();

	FText GetTitleText() const;
	FText GetSlotText(EInventoryDrawerSlot Line) const;
	bool IsSlotEnabled(EInventoryDrawerSlot Line) const;

	/** What a click on a line does (set up / use). */
	void Activate(EInventoryDrawerSlot Line);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void BuildDefaultLayout();

	UFUNCTION() void HandleTurret();
	UFUNCTION() void HandleBarricade();
	UFUNCTION() void HandleMine();
	UFUNCTION() void HandleMedkit();
	UFUNCTION() void HandleCannedFood();
	UFUNCTION() void HandleBread();
	UFUNCTION() void HandleChocolate();
	UFUNCTION() void HandleClose();

	UPROPERTY()
	TObjectPtr<UWidget> Panel;

	UPROPERTY()
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY()
	TArray<TObjectPtr<UButton>> SlotButtons;

	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> SlotTexts;
};
