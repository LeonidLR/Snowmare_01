#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TurnBasedHudWidget.generated.h"

class UButton;
class UTextBlock;

/**
 * Turn-based action panel (bottom right, shown while the grid fight runs): «⚔️ ХОД ОТРЯДА» / «🐺 ХОД ПРОТИВНИКА...»,
 * unit name, AP and HP, «🛑 КОНЕЦ ХОДА ОТРЯДА» [Enter], «⏭️ СЛЕДУЮЩИЙ БОЕЦ [Tab]», «🛡️ СТОЙКА: … [C]»,
 * «🔄 Поворот [R]», «📦 Бочка [F]» (barrel push — disabled until ported).
 * Built in C++ (restyle through a Widget Blueprint subclass). Refreshes on UTurnBasedCombatSubsystem::OnStateChanged.
 * Godot reference: Scripts/tactics/gorky17_combat_hud.gd (_create_action_panel, update_unit_info).
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API UTurnBasedHudWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|TurnBased")
	void Refresh();

	FText GetPhaseText() const;
	FText GetApText() const;

protected:
	virtual void NativeOnInitialized() override;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|TurnBased", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TbPhaseText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|TurnBased", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TbUnitText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|TurnBased", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TbApText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|TurnBased", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TbHpText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|TurnBased", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TbStanceText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|TurnBased", meta = (BindWidgetOptional))
	TObjectPtr<UButton> TbPassButton;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|TurnBased", meta = (BindWidgetOptional))
	TObjectPtr<UButton> TbNextButton;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|TurnBased", meta = (BindWidgetOptional))
	TObjectPtr<UButton> TbStanceButton;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|TurnBased", meta = (BindWidgetOptional))
	TObjectPtr<UButton> TbTurnButton;

private:
	void BuildDefaultLayout();
	UTextBlock* MakeText(const FName& Name, int32 Size, const FLinearColor& Color);
	UButton* MakeButton(const FName& Name, const FText& Label, float Height, class UPanelWidget* Parent, UTextBlock** OutText = nullptr);

	UFUNCTION()
	void HandleStateChanged();

	UFUNCTION()
	void HandlePass();

	UFUNCTION()
	void HandleNext();

	UFUNCTION()
	void HandleStance();

	UFUNCTION()
	void HandleTurn();
};
