#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Interactables/ActionMenuTypes.h"
#include "ActionMenuWidget.generated.h"

class UButton;
class UTextBlock;

/**
 * Object action menu: centred panel with title, description and confirm / relocate / cancel buttons, driven by
 * UInteractionSubsystem. Builds a default layout in C++; a Widget Blueprint subclass can supply its own design
 * by naming its widgets TitleText, DescriptionText, ConfirmButton, ConfirmText, RelocateButton, RelocateText,
 * TrapButton, TrapText, CancelButton, CancelText.
 * Godot reference: movements_demo.tscn UI/ActionMenu (PanelContainer centred, 420 x 160), main.gd `_open_action_menu`.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API UActionMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Fills the panel and shows it. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|ActionMenu")
	void ShowMenu(const FActionMenuSpec& Menu);

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|ActionMenu")
	void HideMenu();

	/** Blueprint hook after the texts are filled (animations, sounds). */
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|ActionMenu", meta = (DisplayName = "On Menu Shown"))
	void ReceiveMenuShown(const FActionMenuSpec& Menu);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeOnInitialized() override;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|ActionMenu", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|ActionMenu", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DescriptionText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|ActionMenu", meta = (BindWidgetOptional))
	TObjectPtr<UButton> ConfirmButton;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|ActionMenu", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ConfirmText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|ActionMenu", meta = (BindWidgetOptional))
	TObjectPtr<UButton> RelocateButton;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|ActionMenu", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> RelocateText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|ActionMenu", meta = (BindWidgetOptional))
	TObjectPtr<UButton> TrapButton;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|ActionMenu", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TrapText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|ActionMenu", meta = (BindWidgetOptional))
	TObjectPtr<UButton> CancelButton;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|ActionMenu", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CancelText;

private:
	void BuildDefaultLayout();
	UButton* MakeButton(const FName& Name, TObjectPtr<UTextBlock>& OutLabel);

	UFUNCTION()
	void HandleConfirm();

	UFUNCTION()
	void HandleRelocate();

	UFUNCTION()
	void HandleCancel();

	UFUNCTION()
	void HandleTrap();
};
