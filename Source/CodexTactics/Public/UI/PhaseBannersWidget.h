#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PhaseBannersWidget.generated.h"

class UBorder;
class UButton;
class UTextBlock;

/**
 * Game-phase banners, refreshed every frame from the game flow:
 * - pause banner (top centre): «⏱️ РЕЖИМ ПРИКАЗОВ | Зарядов в волне: N/3 | Время планирования: T с [ПРОБЕЛ — исполнить]»;
 * - combat banner (below it): «⏱ ПОДГОТОВКА К БОЮ: NN сек | [ПРОБЕЛ] — НИЖНЕЕ МЕНЮ» + «⚔️ Начать бой» during the
 *   preparation, «⚔️ ВОЛНА N | ВРАГОВ ОСТАЛОСЬ: M» during a wave;
 * - pre-combat cutscene: letterbox bars, card «[КАТ-СЦЕНА: ПРОРЫВ В КАРАНТИННЫЙ ДВОР]», countdown; a click skips it.
 * Godot reference: movements_demo.tscn UI/PauseBanner, UI/CombatBanner, UI/CutscenePanel; main.gd _process banner
 * texts, _update_pause_banner_ui, _end_cutscene_and_start_pause.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API UPhaseBannersWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Banners")
	void Refresh();

	/** Current texts (headless checks). */
	FText GetPauseText() const;
	FText GetCombatText() const;
	bool IsCutsceneShown() const;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Banners", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> PauseBanner;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Banners", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PauseText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Banners", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> CombatBanner;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Banners", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CombatText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Banners", meta = (BindWidgetOptional))
	TObjectPtr<UButton> FinishPrepButton;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Banners", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> CutscenePanel;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Banners", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CutsceneSkipText;

private:
	void BuildDefaultLayout();
	UTextBlock* MakeText(const FName& Name, int32 Size, const FLinearColor& Color);

	UFUNCTION()
	void HandleFinishPreparation();
};
