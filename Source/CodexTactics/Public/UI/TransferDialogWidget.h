#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Characters/TransferRules.h"
#include "TransferDialogWidget.generated.h"

class UButton;
class UTextBlock;

/**
 * «ПЕРЕД» dialog: the leader's engineering items, provisions, matches and ammo packs; a click starts the hand-over
 * (USquadTransferSubsystem). Lines without stock are disabled; bread and the special ammo only show when carried.
 * Built in C++ (restyle through a Widget Blueprint subclass).
 * Godot reference: Scenes/ui/inventory/transfer_dialog.gd (layout, item order), main.gd _update_transfer_dialog_ui,
 * _toggle_transfer_dialog.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API UTransferDialogWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Transfer")
	void Open();

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Transfer")
	void Close();

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Transfer")
	void Toggle();

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Transfer")
	bool IsOpen() const;

	void Refresh();
	FText GetTitleText() const;
	FText GetItemText(ETransferItem Item) const;
	bool IsItemEnabled(ETransferItem Item) const;
	bool IsItemVisible(ETransferItem Item) const;

	/** A click on a line: closes the dialog and starts the hand-over. */
	void Choose(ETransferItem Item);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void BuildDefaultLayout();

	UFUNCTION() void HandleButton0();
	UFUNCTION() void HandleButton1();
	UFUNCTION() void HandleButton2();
	UFUNCTION() void HandleButton3();
	UFUNCTION() void HandleButton4();
	UFUNCTION() void HandleButton5();
	UFUNCTION() void HandleButton6();
	UFUNCTION() void HandleButton7();
	UFUNCTION() void HandleButton8();
	UFUNCTION() void HandleButton9();
	UFUNCTION() void HandleButton10();
	UFUNCTION() void HandleButton11();
	UFUNCTION() void HandleButton12();
	UFUNCTION() void HandleButton13();
	UFUNCTION() void HandleClose();

	UPROPERTY()
	TObjectPtr<UWidget> Panel;

	UPROPERTY()
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY()
	TArray<TObjectPtr<UButton>> ItemButtons;

	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> ItemTexts;
};
