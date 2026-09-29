#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameFlow/GameFlowTypes.h"
#include "VictoryPanelWidget.generated.h"

class UButton;
class UTextBlock;
class UWidget;

/**
 * Wave-cleared panel over a dimmed screen: title, subtitle, the squad kill statistics card, «Запустить следующую волну
 * (N/M)» / «Завершить бой и продолжить исследование» and «Перезапустить уровень (X)». Shown while the flow is in
 * WaveCleared. Built in C++ (restyle through a Widget Blueprint subclass).
 * Godot reference: Scenes/movements/movements_demo.tscn UI/VictoryPanel, main.gd _on_wave_cleared / _on_next_wave_pressed.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API UVictoryPanelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Victory")
	bool IsShown() const;

	/** Current texts (title, subtitle, stats, button) as shown. */
	FString GetShownText() const;

	/** The main button: next wave's rest, or the post-combat sequence after the last wave. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Victory")
	void PressNext();

	void Refresh();

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void BuildDefaultLayout();
	UTextBlock* MakeText(const FName& Name, int32 Size, const FLinearColor& Color);

	/** Shows / hides on the flow's phase change (widgets do not tick in headless runs). */
	UFUNCTION()
	void HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode);

	UFUNCTION()
	void HandleNext();

	UFUNCTION()
	void HandleRestart();

	UPROPERTY()
	TObjectPtr<UWidget> Root;

	UPROPERTY()
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY()
	TObjectPtr<UTextBlock> SubtitleText;

	UPROPERTY()
	TObjectPtr<UTextBlock> StatsText;

	UPROPERTY()
	TObjectPtr<UTextBlock> NextText;
};
