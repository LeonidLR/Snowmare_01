#pragma once

#include "CoreMinimal.h"
#include "UI/Frontend/CodexActivatableScreen.h"
#include "UI/Frontend/CodexFrontendRules.h"
#include "CodexConfirmDialog.generated.h"

class UTextBlock;

/**
 * Modal Yes / No (or OK) dialog over a blurred, dimmed background (Modal layer). Back = No (or OK).
 * Show it with UCodexUISubsystem::ShowConfirm (C++ / Blueprint) or the async node «Show Confirm Dialog».
 * Widget names (WBP_ConfirmDialog): BackgroundBlur (UBackgroundBlur), DimBorder, TitleText, MessageText,
 * YesButton / NoButton / OkButton (UCodexMenuButton, EntryId Yes / No / Ok).
 * No Godot reference (2026-10-08 user decision: professional frontend).
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API UCodexConfirmDialog : public UCodexActivatableScreen
{
	GENERATED_BODY()

public:
	UCodexConfirmDialog();

	/** Called by the UI subsystem before the dialog activates. */
	void Setup(ECodexConfirmType InType, const FText& InTitle, const FText& InMessage, TFunction<void(ECodexConfirmResult)> InOnResult);

	/** YES / OK. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|UI")
	void Confirm() { Finish(ECodexConfirmResult::Confirmed); }

	/** NO / back. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|UI")
	void Cancel() { Finish(Type == ECodexConfirmType::Ok ? ECodexConfirmResult::Confirmed : ECodexConfirmResult::Cancelled); }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|UI")
	FText GetTitle() const { return TitleValue; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|UI")
	FText GetMessage() const { return MessageValue; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|UI")
	ECodexConfirmType GetConfirmType() const { return Type; }

	virtual void BuildDefaultTree(UWidgetTree& Tree) const override;

protected:
	virtual void BindScreenWidgets() override;
	virtual void RefreshScreen() override;
	virtual void OnEntryActivated(FName EntryId) override;
	virtual bool NativeOnHandleBackAction() override;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|UI", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|UI", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MessageText;

private:
	void Finish(ECodexConfirmResult Result);

	ECodexConfirmType Type = ECodexConfirmType::YesNo;
	FText TitleValue;
	FText MessageValue;
	TFunction<void(ECodexConfirmResult)> OnResult;
	bool bFinished = false;
};
