#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Characters/TransferRules.h"
#include "InventoryDrawerWidget.generated.h"

class AOperativeCharacter;
class UButton;
class UInventoryDragDropOperation;
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
	Matches,
	/** Sprint 09: tripwire mine, 2 grenades of the squad. */
	Tripwire,
	/** Sprint 13: ammo reserves (drag to a squad mate to hand over; the special ammo lines only show when carried). */
	RifleAmmo,
	PistolAmmo,
	ShotgunAmmo,
	FlameFuel,
	CryoAmmo,
	PlasmaAmmo
};

/**
 * Personal inventory of the leader above the action bar («ИНВ»): engineering items (click: set it up, a squad mate hands
 * one over when needed) and provisions (click / H J K L: use), matches and ammo reserves (info). Built in C++ (restyle
 * through a Widget Blueprint subclass).
 * Sprint 13: the single access point for hand-overs — LMB held on a line carrying stock and dragged starts an
 * UInventoryDragDropOperation; dropped on a squad mate's 3D model (NativeOnDragCancelled: nothing in the UI took it,
 * the world under the cursor is traced) or on his action bar portrait (UActionBarWidget::NativeOnDrop) it goes to
 * ACodexTacticsHUD::HandleTransferDropOnActor. A press released without dragging still clicks the line.
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

	/** What a click on a line does (set up / use; matches and ammo are information only). */
	void Activate(EInventoryDrawerSlot Line);

	/** Hand-over item of a line; false for the tripwire (squad grenades, not personal stock). */
	static bool GetTransferItem(EInventoryDrawerSlot Line, ETransferItem& OutItem);

	/** Line shown at all (special ammo only when carried). */
	bool IsSlotVisible(EInventoryDrawerSlot Line) const;

	/** The drag payload of a line for the leader (null when the line has no transfer item or no stock). */
	UInventoryDragDropOperation* CreateDragOperation(EInventoryDrawerSlot Line);

	/**
	 * A drag nobody in the UI accepted, released at ScreenPosition (absolute desktop pixels): a drop back on the drawer is
	 * ignored, otherwise the world under the cursor is traced and the HUD hands over to the squad mate there.
	 */
	void HandleWorldDrop(UInventoryDragDropOperation* Operation, const FVector2D& ScreenPosition);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnPreviewMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation) override;
	virtual void NativeOnDragCancelled(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	/** A crate line dropped on the open drawer: the leader takes it. */
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;

private:
	void BuildDefaultLayout();

	/** Index of the visible line button under an absolute screen position, INDEX_NONE when none. */
	int32 FindSlotAt(const FVector2D& ScreenPosition) const;

	/** Line pressed with LMB (drag candidate / click on release). */
	int32 PressedSlot = INDEX_NONE;

	UFUNCTION() void HandleTurret();
	UFUNCTION() void HandleBarricade();
	UFUNCTION() void HandleMine();
	UFUNCTION() void HandleMedkit();
	UFUNCTION() void HandleCannedFood();
	UFUNCTION() void HandleBread();
	UFUNCTION() void HandleChocolate();
	UFUNCTION() void HandleTripwire();
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
