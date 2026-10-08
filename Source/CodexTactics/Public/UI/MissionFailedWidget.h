#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MissionFailedWidget.generated.h"

class UButton;
class UTextBlock;

/**
 * «❄️ MISSION FAILED ❄️» screen: dark red dim, centred panel with the reason, a tip and «🔄 Restart».
 * Built in C++; a Widget Blueprint subclass can restyle it by naming its widgets FailedTitleText, FailedReasonText,
 * FailedTipText, FailedRestartButton, FailedRestartText.
 * Godot reference: Scenes/movements/movements_demo.tscn UI/GameOverPanel, main.gd _trigger_game_over / _on_restart_pressed.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API UMissionFailedWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Mission")
	void ShowFailure(const FText& Reason);

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Mission")
	void HideScreen();

protected:
	virtual void NativeOnInitialized() override;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Mission", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> FailedTitleText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Mission", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> FailedReasonText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Mission", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> FailedTipText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Mission", meta = (BindWidgetOptional))
	TObjectPtr<UButton> FailedRestartButton;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Mission", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> FailedRestartText;

private:
	void BuildDefaultLayout();
	UTextBlock* MakeText(const FName& Name, int32 Size, const FLinearColor& Color);

	UFUNCTION()
	void HandleRestart();
};
