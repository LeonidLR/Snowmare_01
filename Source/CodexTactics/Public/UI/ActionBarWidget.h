#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ActionBarWidget.generated.h"

class UBorder;
class UButton;
class UProgressBar;
class UTextBlock;

/** One squad slot of the action bar: select button with HP and cold bars. */
USTRUCT()
struct FActionBarSquadSlot
{
	GENERATED_BODY()

	/** Border around the button (leader: cyan 3 px). */
	UPROPERTY()
	TObjectPtr<UBorder> Frame;

	UPROPERTY()
	TObjectPtr<UButton> Button;

	UPROPERTY()
	TObjectPtr<UTextBlock> Label;

	UPROPERTY()
	TObjectPtr<UProgressBar> HealthBar;

	UPROPERTY()
	TObjectPtr<UProgressBar> ColdBar;
};

/**
 * Bottom tactical bar: transfer / inventory (not ported yet, disabled), weapon + ammo («[G] Граната»), relocation
 * mode («ПЕР» / «АКТИВ»), stance letter (С / П / Л, click cycles), guard («ОБОР», not ported yet), squad slots
 * [1] КОМ, [2] ИНЖ, [3] МЕД with HP / cold bars, [4] РЕЗ (locked reserve). Refreshes every frame from the squad.
 * Built in C++ (restyle through a Widget Blueprint subclass).
 * Godot reference: movements_demo.tscn UI/TacticalBar, main.gd _create_tactical_command_bar,
 * _update_tactical_command_bar, _cycle_leader_stance, _on_relocate_slot_clicked.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API UActionBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Updates texts, colours and bars from the current squad state. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|ActionBar")
	void Refresh();

	/** Text of the weapon slot (tests / headless checks). */
	FText GetWeaponText() const;

	/** Label of squad slot Index (0..3). */
	FText GetSlotText(int32 Index) const;

	// --- Weapon selector (Godot main.gd _create_weapon_selector_panel / _select_weapon_from_selector) ---

	/** Click on the weapon slot: opens / closes «ВЫБОР ВООРУЖЕНИЯ» above the bar. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|ActionBar")
	void ToggleWeaponSelector();

	UFUNCTION(BlueprintPure, Category = "CodexTactics|ActionBar")
	bool IsWeaponSelectorOpen() const;

	/** Line of selector button Index (0 M16, 1 pistol, 2 grenade, 3 knife) for the leader / active operative. */
	FText GetSelectorText(int32 Index) const;

	/**
	 * Takes weapon WeaponId: in turn-based combat through the combat (active operative), otherwise the leader; posts
	 * «Экипировано» and closes the selector. Outside turn-based combat the grenade starts the throw aim.
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|ActionBar")
	bool SelectWeapon(const FString& WeaponId);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|ActionBar", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> BarWeaponText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|ActionBar", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> BarRelocateText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|ActionBar", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> BarStanceText;

private:
	void BuildDefaultLayout();
	UTextBlock* MakeText(const FName& Name, int32 Size, const FLinearColor& Color);
	UButton* MakeSlotButton(const FName& Name, const FLinearColor& Color, float Width, float Height, UTextBlock* Label, class UHorizontalBox* Row);

	UFUNCTION()
	void HandleRelocate();

	UFUNCTION()
	void HandleWeaponSlot();

	UFUNCTION()
	void HandleGuard();

	UPROPERTY()
	TObjectPtr<UTextBlock> BarGuardText;

	UPROPERTY()
	TObjectPtr<UButton> GuardButton;

	UFUNCTION()
	void HandleSelectM16();

	UFUNCTION()
	void HandleSelectPistol();

	UFUNCTION()
	void HandleSelectGrenade();

	UFUNCTION()
	void HandleSelectKnife();

	UPROPERTY()
	TObjectPtr<UWidget> SelectorPanel;

	UPROPERTY()
	TArray<TObjectPtr<UButton>> SelectorButtons;

	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> SelectorTexts;

	UFUNCTION()
	void HandleStance();

	UFUNCTION()
	void HandleSlot0();

	UFUNCTION()
	void HandleSlot1();

	UFUNCTION()
	void HandleSlot2();

	void SelectSlot(int32 Index);

	UPROPERTY()
	TObjectPtr<UButton> RelocateButton;

	UPROPERTY()
	TArray<FActionBarSquadSlot> Slots;
};
