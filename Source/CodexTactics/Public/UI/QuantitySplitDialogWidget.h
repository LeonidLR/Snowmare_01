#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Characters/SquadTransferSubsystem.h"
#include "Characters/TransferRules.h"
#include "QuantitySplitDialogWidget.generated.h"

class AOperativeCharacter;
class UButton;
class USlider;
class UTextBlock;

/**
 * Sprint 13: compact «how many?» dialog after a dragged item was dropped (on a squad mate, the ground, a crate, or a
 * crate line onto an operative) and more than one quantity is possible. Quantity on a slider and [-] / [+] by TransferRules::GetItemQuantityStep (ammo 5, items 1), «ALL» (the whole
 * stack), «CONFIRM» (USquadTransferSubsystem::RequestTransfer), «CANCEL» / Esc (ACodexTacticsHUD::HandleEscape).
 * The maximum is min(sender stock, recipient capacity) and is shown. Built in C++ (restyle through a Widget Blueprint
 * subclass). No Godot counterpart.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API UQuantitySplitDialogWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Shows the dialog for Item from Sender to Recipient; starts at the whole stack (MaxQuantity). */
	void OpenFor(AOperativeCharacter* Sender, AOperativeCharacter* Recipient, ETransferItem InItem, int32 MaxQuantity);

	/** Any Sprint 13 request (give / drop / store / take); the title follows the action. */
	void OpenForRequest(const FTransferRequest& InRequest, int32 MaxQuantity);

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Transfer")
	void Close();

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Transfer")
	bool IsOpen() const;

	/** Snaps Value to the item's grid (TransferRules::QuantizeQuantity) and shows it. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Transfer")
	void SetQuantity(int32 Value);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Transfer")
	int32 GetQuantity() const { return Quantity; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Transfer")
	int32 GetMaxQuantity() const { return MaxQuantity; }

	/** [+] / [-] one step; «ALL». */
	void Increment();
	void Decrement();
	void SelectAll();

	/** «CONFIRM»: closes and runs the request with the chosen quantity (Failed when the dialog was not open). */
	ETransferRequestOutcome Confirm();

	/** «CANCEL» / Esc: closes, nothing changes hands. */
	void Cancel();

	ETransferItem GetItem() const { return Request.Item; }
	ETransferAction GetAction() const { return Request.Action; }
	AOperativeCharacter* GetSender() const { return Request.Operative.Get(); }
	AOperativeCharacter* GetRecipient() const { return Request.Recipient.Get(); }

	FText GetTitleText() const;
	FText GetQuantityText() const;

protected:
	virtual void NativeOnInitialized() override;

private:
	void BuildDefaultLayout();
	void RefreshTexts();

	UFUNCTION() void HandleMinus();
	UFUNCTION() void HandlePlus();
	UFUNCTION() void HandleAll();
	UFUNCTION() void HandleConfirm();
	UFUNCTION() void HandleCancel();
	UFUNCTION() void HandleSlider(float Value);

	UPROPERTY()
	TObjectPtr<UWidget> Panel;

	UPROPERTY()
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY()
	TObjectPtr<UTextBlock> QuantityText;

	UPROPERTY()
	TObjectPtr<USlider> QuantitySlider;

	FTransferRequest Request;
	int32 Quantity = 0;
	int32 MaxQuantity = 0;
};
