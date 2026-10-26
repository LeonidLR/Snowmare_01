#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UObject/ObjectKey.h"
#include "Characters/FirePostureRules.h"
#include "ActionBarWidget.generated.h"

class AOperativeCharacter;
enum class ETransferItem : uint8;
enum class ETransferRequestOutcome : uint8;
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

	/** 2026-10-22: gold "+N" badge in the slot's top-right corner while the operative has unspent points; a click opens his profile. */
	UPROPERTY()
	TObjectPtr<UButton> Badge;

	UPROPERTY()
	TObjectPtr<UTextBlock> BadgeText;
};

/**
 * Bottom tactical bar: inventory «INV» (Sprint 13: the old transfer button is gone — hand-overs are dragged from the
 * drawer; a squad slot is a drop target, NativeOnDrop), weapon + ammo («[G] Grenade»), relocation
 * mode («MOVE» / «ACTIVE»), stance letter (S / C / P, click cycles), guard («GUARD» / «HELD»), squad slots
 * [1] CMD, [2] ENG, [3] MED with HP / cold bars, [4] RES (locked reserve). Refreshes every frame from the squad.
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

	/** Click on the weapon slot: opens / closes «WEAPON SELECT» above the bar. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|ActionBar")
	void ToggleWeaponSelector();

	UFUNCTION(BlueprintPure, Category = "CodexTactics|ActionBar")
	bool IsWeaponSelectorOpen() const;

	/** Line of selector button Index (0 M16, 1 pistol, 2 grenade, 3 knife) for the leader / active operative. */
	FText GetSelectorText(int32 Index) const;

	/**
	 * Takes weapon WeaponId: in turn-based combat through the combat (active operative), otherwise the leader; posts
	 * «Equipped» and closes the selector. Outside turn-based combat the grenade starts the throw aim.
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|ActionBar")
	bool SelectWeapon(const FString& WeaponId);

	/** Squad member shown in slot Index (roster order), null for an empty slot. */
	AOperativeCharacter* GetSlotMember(int32 Index) const;

	/** 2026-10-22: the unspent-points badge of slot Index is shown, and its text ("+3"); smokes. */
	bool IsSlotBadgeShown(int32 Index) const;
	FString GetSlotBadgeText(int32 Index) const;

	/** The badge's click: the profile of slot Index's operative opens (to spend his points; the leader stays). */
	void OpenSlotProfile(int32 Index);

	/** Stealth lure buttons (smokes): the WHISTLE / THROW label and whether it is enabled. */
	FText GetStealthButtonText(bool bThrow) const;
	bool IsStealthButtonEnabled(bool bThrow) const;

	/** SUPPRESS button (2026-10-26; smokes): its label ("SUPPRESS [7]" / "AIM... [7]" / "STOP SUPP. [7]") and whether it is enabled. */
	FText GetSuppressButtonText() const;
	bool IsSuppressButtonEnabled() const;
	/** The SUPPRESS button's click (the 7 key: ACodexTacticsPlayerController::SuppressKey). */
	UFUNCTION()
	void HandleSuppress();

	/** Sprint 13: an inventory drag dropped on squad slot Index -> ACodexTacticsHUD::HandleTransferDropOnActor. */
	ETransferRequestOutcome HandleTransferDropOnSlot(int32 Index, AOperativeCharacter* Sender, ETransferItem Item);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|ActionBar", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> BarWeaponText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|ActionBar", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> BarRelocateText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|ActionBar", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> BarStanceText;

private:
	/**
	 * Sets the tooltip only when its text changed: UWidget::SetToolTipText builds a new SToolTip on every call and Refresh
	 * runs every frame (audit 2026-10-11 P8).
	 */
	void SetToolTipIfChanged(UWidget* Widget, const FText& Text);
	/** The tooltip text last applied per widget. */
	TMap<TObjectKey<UWidget>, FString> AppliedToolTips;

	void BuildDefaultLayout();
	UTextBlock* MakeText(const FName& Name, int32 Size, const FLinearColor& Color);
	UButton* MakeSlotButton(const FName& Name, const FLinearColor& Color, float Width, float Height, UTextBlock* Label, class UHorizontalBox* Row);

	UFUNCTION()
	void HandleRelocate();

	UFUNCTION()
	void HandleWeaponSlot();

	UFUNCTION()
	void HandleGuard();

	UFUNCTION()
	void HandleInventory();

	UPROPERTY()
	TObjectPtr<UTextBlock> BarGuardText;

	UPROPERTY()
	TObjectPtr<UButton> GuardButton;

	/** Commander Mode switch «AUTO ON / OFF» (Ctrl + T on screen). */
	UFUNCTION()
	void HandleAutonomy();

	/** APC P1: «BOARD [Y]» near a vehicle / «EXIT [Y]» aboard (hidden otherwise). */
	UFUNCTION()
	void HandleVehicle();

	UPROPERTY()
	TObjectPtr<UTextBlock> BarVehicleText;

	UPROPERTY()
	TObjectPtr<UButton> VehicleButton;

	/**
	 * Stealth lure (user decisions 2026-10-18): «WHISTLE [5]» / «THROW [6]» with the cooldown («WHISTLE 4s»), disabled with the
	 * reason as the tooltip when fighting / detected; hovering WHISTLE previews its hearing circle round the leader and the
	 * enemies that would hear it.
	 */
	UFUNCTION()
	void HandleStealthWhistle();

	UFUNCTION()
	void HandleStealthThrow();

	UPROPERTY()
	TObjectPtr<UButton> StealthWhistleButton;

	UPROPERTY()
	TObjectPtr<UTextBlock> StealthWhistleText;

	UPROPERTY()
	TObjectPtr<UButton> StealthThrowButton;

	UPROPERTY()
	TObjectPtr<UTextBlock> StealthThrowText;

	/** Updates both lure buttons (Refresh). */
	void RefreshStealthButtons(const class AOperativeCharacter& Leader);

	/** Suppressive fire (2026-10-26): the SUPPRESS button (key 7). */
	UPROPERTY()
	TObjectPtr<UButton> SuppressButton;

	UPROPERTY()
	TObjectPtr<UTextBlock> SuppressText;

	/** Updates the SUPPRESS button (Refresh): armed aim / the selection suppressing / off in turn-based combat. */
	void RefreshSuppressButton();

	UPROPERTY()
	TObjectPtr<UTextBlock> BarAutonomyText;

	UPROPERTY()
	TObjectPtr<UButton> AutonomyButton;

	/** Fire posture buttons «PAS [,]» / «DEF [.]» / «AGG [/]» (user request 2026-10-06). */
	UFUNCTION()
	void HandlePosturePassive() { ApplyPosture(ESquadFirePosture::Passive); }

	UFUNCTION()
	void HandlePostureDefensive() { ApplyPosture(ESquadFirePosture::Defensive); }

	UFUNCTION()
	void HandlePostureAggressive() { ApplyPosture(ESquadFirePosture::Aggressive); }

	void ApplyPosture(ESquadFirePosture Posture);

	UPROPERTY()
	TArray<TObjectPtr<UButton>> PostureButtons;

	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> PostureTexts;

	UFUNCTION()
	void HandleSelectM16();

	UFUNCTION()
	void HandleSelectPistol();

	UFUNCTION()
	void HandleSelectGrenade();

	UFUNCTION()
	void HandleSelectKnife();

	/** Sprint 15: the selector's fifth line, the signal flare (aim mode; the weapon in hands stays). */
	UFUNCTION()
	void HandleSelectFlare();

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

	UFUNCTION()
	void HandleSlot3();

	void SelectSlot(int32 Index);

	UFUNCTION()
	void HandleBadge0() { OpenSlotProfile(0); }

	UFUNCTION()
	void HandleBadge1() { OpenSlotProfile(1); }

	UFUNCTION()
	void HandleBadge2() { OpenSlotProfile(2); }

	UFUNCTION()
	void HandleBadge3() { OpenSlotProfile(3); }

	UPROPERTY()
	TObjectPtr<UButton> RelocateButton;

	UPROPERTY()
	TArray<FActionBarSquadSlot> Slots;
};
