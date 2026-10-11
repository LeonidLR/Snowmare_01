#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TurnBasedHudWidget.generated.h"

class UButton;
class UTextBlock;

/**
 * Turn-based action panel (bottom right, shown while the grid fight runs): «⚔️ SQUAD TURN» / «🐺 ENEMY TURN...»,
 * unit name, AP and HP, «🛑 END SQUAD TURN» [Enter], «⏭️ NEXT OPERATIVE [Tab]», «🛡️ STANCE: … [C]»,
 * «🔄 Turn [R]», «📦 Barrel [F]» (barrel push — disabled until ported); «GET UP (2 AP) [Z]» in the stance entry's place
 * while the active operative lies after a knockdown (user decision 2026-10-11).
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

	/** The «GET UP (2 AP)» entry is on the panel (the active operative lies after a knockdown; smokes). */
	bool IsGetUpShown() const;

	/** The stance entry is on the panel (hidden only while «GET UP» takes its place; smokes). */
	bool IsStanceShown() const;

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

	/** «GET UP (2 AP) [Z]»: the knocked-down active operative gets up (UTurnBasedCombatSubsystem::GetUpActiveUnit). */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|TurnBased", meta = (BindWidgetOptional))
	TObjectPtr<UButton> TbGetUpButton;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|TurnBased", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TbGetUpText;

	/** «Barrel»: push the adjacent barrel (Godot push_barrel_pressed -> _try_push_adjacent_barrel). */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|TurnBased", meta = (BindWidgetOptional))
	TObjectPtr<UButton> TbBarrelButton;

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

	UFUNCTION()
	void HandleBarrel();

	UFUNCTION()
	void HandleGetUp();
};
