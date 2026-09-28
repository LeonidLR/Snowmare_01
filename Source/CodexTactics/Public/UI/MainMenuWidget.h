#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MainMenuWidget.generated.h"

class UButton;
class UTextBlock;

/**
 * Start menu «❄️ COLD GRAD: ТАКТИЧЕСКИЙ РЕЖИМ ❄️» with three modes: «Начать игру» (exploration → combat),
 * «Начать бой» (tactical preparation), «Начать исследование» (quests and cold).
 * Built in C++; a Widget Blueprint subclass can restyle it by naming its widgets MenuTitleText, MenuSubtitleText,
 * MenuGameButton, MenuCombatButton, MenuExplorationButton (+ …Text labels).
 * Godot reference: Scenes/movements/movements_demo.tscn UI/StartMenu, main.gd _on_start_*_pressed.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API UMainMenuWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Menu", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MenuTitleText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Menu", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MenuSubtitleText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Menu", meta = (BindWidgetOptional))
	TObjectPtr<UButton> MenuGameButton;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Menu", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MenuGameText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Menu", meta = (BindWidgetOptional))
	TObjectPtr<UButton> MenuCombatButton;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Menu", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MenuCombatText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Menu", meta = (BindWidgetOptional))
	TObjectPtr<UButton> MenuExplorationButton;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Menu", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MenuExplorationText;

private:
	void BuildDefaultLayout();
	UTextBlock* MakeText(const FName& Name, int32 Size, const FLinearColor& Color);
	UButton* MakeButton(const FName& Name, const FName& TextName, const FText& Label, TObjectPtr<UTextBlock>& OutText, class UVerticalBox* Column);

	UFUNCTION()
	void HandleGame();

	UFUNCTION()
	void HandleCombat();

	UFUNCTION()
	void HandleExploration();
};
