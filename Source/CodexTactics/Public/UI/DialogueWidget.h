#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DialogueWidget.generated.h"

class UBorder;
class UButton;
class UTextBlock;

/**
 * Bottom dialogue window: speaker card (portrait tag, name, role, role colours), «РАЗГОВОР» badge, [N / M] progress,
 * the line, hint, «Пропустить» / «Далее» buttons. Mirrors UDialogueSubsystem; a click on the panel also advances.
 * Built in C++; a Widget Blueprint subclass can restyle it by naming its widgets DialogCard, DialogPortraitText,
 * DialogNameText, DialogRoleText, DialogProgressText, DialogSpeechText, DialogSkipButton, DialogNextButton, DialogNextText.
 * Godot reference: Scenes/ui/dialogue/bottom_dialogue_dialog.gd (860 x 185, 20 px above the bottom edge).
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API UDialogueWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Shows the current line of the dialogue subsystem, or hides the window when none is open. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Dialogue")
	void Refresh();

protected:
	virtual void NativeOnInitialized() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Dialogue", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> DialogCard;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Dialogue", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> DialogCardFrame;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Dialogue", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DialogPortraitText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Dialogue", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DialogNameText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Dialogue", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DialogRoleText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Dialogue", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DialogProgressText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Dialogue", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DialogSpeechText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Dialogue", meta = (BindWidgetOptional))
	TObjectPtr<UButton> DialogSkipButton;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Dialogue", meta = (BindWidgetOptional))
	TObjectPtr<UButton> DialogNextButton;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Dialogue", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DialogNextText;

private:
	void BuildDefaultLayout();
	UTextBlock* MakeText(const FName& Name, int32 Size, const FLinearColor& Color, bool bCenter);

	UFUNCTION()
	void HandleSkip();

	UFUNCTION()
	void HandleNext();
};
