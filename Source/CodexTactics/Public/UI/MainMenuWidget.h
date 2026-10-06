#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MainMenuWidget.generated.h"

class UButton;
class UTextBlock;

/**
 * Start menu «❄️ COLD GRAD: ТАКТИЧЕСКИЙ РЕЖИМ ❄️» with two modes: «Начать игру» (exploration → combat) and
 * «Начать бой» (tactical preparation). Godot's third button «Начать исследование» is left out (user decision 2026-09-29).
 * Built in C++; a Widget Blueprint subclass can restyle it by naming its widgets MenuTitleText, MenuSubtitleText,
 * MenuGameButton, MenuCombatButton (+ …Text labels).
 * Godot reference: Scenes/movements/movements_demo.tscn UI/StartMenu, main.gd _on_start_*_pressed.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API UMainMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Shows «Начать бой» only on levels whose fight starts by the button: on an ambush level (ULevelEncounterSubsystem —
	 * patrols, level JSON "combat_start") the fight starts when the squad attacks or is detected (user request 2026-10-06).
	 */
	void RefreshModeButtons();

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

private:
	void BuildDefaultLayout();
	UTextBlock* MakeText(const FName& Name, int32 Size, const FLinearColor& Color);
	UButton* MakeButton(const FName& Name, const FName& TextName, const FText& Label, TObjectPtr<UTextBlock>& OutText, class UVerticalBox* Column);

	UFUNCTION()
	void HandleGame();

	UFUNCTION()
	void HandleCombat();
};
