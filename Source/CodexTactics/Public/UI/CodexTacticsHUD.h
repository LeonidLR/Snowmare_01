#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Interactables/ActionMenuTypes.h"
#include "CodexTacticsHUD.generated.h"

class AOperativeCharacter;
class UActionMenuWidget;
class ULootDialogWidget;
class UMissionFailedWidget;
class UMainMenuWidget;
class UDialogueWidget;
class UActionBarWidget;
class UInventoryDrawerWidget;
class UTransferDialogWidget;
class UProfileDialogWidget;
class UVictoryPanelWidget;
class AOperativeCharacter;
class UPauseMenuWidget;
class USaveLoadDialogWidget;
enum class ESaveDialogMode : uint8;
enum class ESquadFirePosture : uint8;
class UPhaseBannersWidget;
class UTurnBasedHudWidget;
class ALootCrateActor;
class UFont;

/**
 * Baseline canvas HUD until the UMG interface is ported: objective banner «ЦЕЛЬ: …» (top left, Godot ObjectivePanel),
 * message feed (top right), squad status panel (below the objective: phase, mode, per-operative stance / health /
 * cold / ammo) and labels above the operatives. Shows the mission-failed screen (UMG) when UMissionSubsystem fails.
 * Godot reference: Scenes/movements/main.gd message panel (`_on_quest_message`) and squad status labels.
 * Toggle the status panel and labels with the console variable CodexTactics.HUD.ShowStatus.
 * Also owns the object action menu widget (UMG) and shows it while UInteractionSubsystem has a menu open.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API ACodexTacticsHUD : public AHUD
{
	GENERATED_BODY()

public:
	ACodexTacticsHUD();

	virtual void BeginPlay() override;
	virtual void DrawHUD() override;

	/** Action menu widget class (a Widget Blueprint subclass can restyle it). */
	UPROPERTY(EditDefaultsOnly, Category = "CodexTactics|HUD")
	TSubclassOf<UActionMenuWidget> ActionMenuWidgetClass;

	/** Loot dialog widget class (a Widget Blueprint subclass can restyle it). */
	UPROPERTY(EditDefaultsOnly, Category = "CodexTactics|HUD")
	TSubclassOf<ULootDialogWidget> LootDialogWidgetClass;

	/** Mission-failed screen class (a Widget Blueprint subclass can restyle it). */
	UPROPERTY(EditDefaultsOnly, Category = "CodexTactics|HUD")
	TSubclassOf<UMissionFailedWidget> MissionFailedWidgetClass;

	/** Start menu class (a Widget Blueprint subclass can restyle it). */
	UPROPERTY(EditDefaultsOnly, Category = "CodexTactics|HUD")
	TSubclassOf<UMainMenuWidget> MainMenuWidgetClass;

	/** Bottom dialogue window class (a Widget Blueprint subclass can restyle it). */
	UPROPERTY(EditDefaultsOnly, Category = "CodexTactics|HUD")
	TSubclassOf<UDialogueWidget> DialogueWidgetClass;

	/** Bottom tactical bar class (a Widget Blueprint subclass can restyle it). */
	UPROPERTY(EditDefaultsOnly, Category = "CodexTactics|HUD")
	TSubclassOf<UActionBarWidget> ActionBarWidgetClass;

	/** Personal inventory drawer class (action bar «ИНВ»; a Widget Blueprint subclass can restyle it). */
	UPROPERTY(EditDefaultsOnly, Category = "CodexTactics|HUD")
	TSubclassOf<UInventoryDrawerWidget> InventoryDrawerWidgetClass;

	/** «ПЕРЕД» hand-over dialog class (a Widget Blueprint subclass can restyle it). */
	UPROPERTY(EditDefaultsOnly, Category = "CodexTactics|HUD")
	TSubclassOf<UTransferDialogWidget> TransferDialogWidgetClass;

	/** Pause menu / save-load dialog classes (Widget Blueprint subclasses can restyle them). */
	UPROPERTY(EditDefaultsOnly, Category = "CodexTactics|HUD")
	TSubclassOf<UPauseMenuWidget> PauseMenuWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "CodexTactics|HUD")
	TSubclassOf<USaveLoadDialogWidget> SaveLoadDialogWidgetClass;

	/** Pause / preparation / wave banners and the cutscene card (a Widget Blueprint subclass can restyle it). */
	UPROPERTY(EditDefaultsOnly, Category = "CodexTactics|HUD")
	TSubclassOf<UPhaseBannersWidget> PhaseBannersWidgetClass;

	/** Turn-based action panel class (a Widget Blueprint subclass can restyle it). */
	UPROPERTY(EditDefaultsOnly, Category = "CodexTactics|HUD")
	TSubclassOf<UTurnBasedHudWidget> TurnBasedHudWidgetClass;

	/** Character profile class (key P; a Widget Blueprint subclass can restyle it). */
	UPROPERTY(EditDefaultsOnly, Category = "CodexTactics|HUD")
	TSubclassOf<UProfileDialogWidget> ProfileDialogWidgetClass;

	UProfileDialogWidget* GetProfileDialog() const { return ProfileDialog; }

	/** Wave-cleared panel class (a Widget Blueprint subclass can restyle it). */
	UPROPERTY(EditDefaultsOnly, Category = "CodexTactics|HUD")
	TSubclassOf<UVictoryPanelWidget> VictoryPanelWidgetClass;

	UVictoryPanelWidget* GetVictoryPanel() const { return VictoryPanel; }

	/** Key P (Godot _toggle_profile_dialog): the leader's profile opens, or the open one closes. */
	void ToggleProfileDialog();

	/** Godot _open_profile_dialog: shows Member's profile (the leader when null). */
	void OpenProfileDialog(AOperativeCharacter* Member);

	/** The bottom action bar (with the weapon selector); null before BeginPlay. */
	UActionBarWidget* GetActionBar() const { return ActionBar; }

	UInventoryDrawerWidget* GetInventoryDrawer() const { return InventoryDrawer; }

	/** Action bar «ИНВ» (Godot _toggle_inventory_drawer): opens / closes the drawer, the weapon selector closes. */
	void ToggleInventoryDrawer();

	UTransferDialogWidget* GetTransferDialog() const { return TransferDialog; }

	/** Action bar «ПЕРЕД» (Godot _toggle_transfer_dialog): the drawer and the weapon selector close. */
	void ToggleTransferDialog();

	UPauseMenuWidget* GetPauseMenu() const { return PauseMenu; }
	USaveLoadDialogWidget* GetSaveLoadDialog() const { return SaveLoadDialog; }

	/** Pause menu -> save / load dialog (the world stays paused). */
	void OpenSaveLoadDialog(ESaveDialogMode Mode);
	/** «Назад в меню»: the dialog closes, the pause menu opens again. */
	void CloseSaveLoadDialog();
	/** Both closed, the world runs again (after a load). */
	void ClosePauseMenus();

	/**
	 * Esc (Godot main.gd KEY_ESCAPE): the overwrite confirmation, the save / load dialog, the pause menu, the inventory
	 * drawer, the profile, the transfer dialog close in that order; with nothing open the pause menu opens (not over the start menu).
	 * Returns false when Esc should go on (e.g. to the dialogue).
	 */
	bool HandleEscape();

	/** Cold of the coldest squad member, 0..100 (Godot max_squad_cold, drives the frost vignette). */
	float GetSquadMaxCold() const;

	/** Godot UI/FrostOverlay: feeds the coldest operative's cold to the full-screen frost vignette (every frame). */
	void UpdateFrostVignette();

	class UFrostVignetteWidget* GetFrostVignette() const { return FrostVignette; }

	/** Messages shown in the feed. */
	UPROPERTY(EditDefaultsOnly, Category = "CodexTactics|HUD", meta = (ClampMin = "1"))
	int32 MaxFeedMessages = 8;

	/** Seconds (real time) a message stays in the feed. */
	UPROPERTY(EditDefaultsOnly, Category = "CodexTactics|HUD", meta = (ClampMin = "1"))
	float FeedMessageLifetime = 25.f;

	/** Feed width as a fraction of the viewport width. */
	UPROPERTY(EditDefaultsOnly, Category = "CodexTactics|HUD", meta = (ClampMin = "0.1", ClampMax = "0.9"))
	float FeedWidthFraction = 0.38f;

	/** Godot Color (sRGB components 0..1) as a linear colour for UMG / canvas; alpha is kept as is. */
	static FLinearColor GodotColor(float R, float G, float B, float A = 1.f)
	{
		FLinearColor Color = FLinearColor::FromSRGBColor(FColor(FMath::RoundToInt(R * 255.f), FMath::RoundToInt(G * 255.f), FMath::RoundToInt(B * 255.f)));
		Color.A = A;
		return Color;
	}

	/** Removes emoji and pictographs the default canvas font cannot render. */
	static FString StripUnsupportedGlyphs(const FString& Text);

private:
	UFUNCTION()
	void HandleActionMenuChanged(bool bOpen, const FActionMenuSpec& Menu);

	UPROPERTY(Transient)
	TObjectPtr<UActionMenuWidget> ActionMenu;

	UFUNCTION()
	void HandleLootDialogChanged(bool bOpen, ALootCrateActor* Crate);

	UPROPERTY(Transient)
	TObjectPtr<ULootDialogWidget> LootDialog;

	UFUNCTION()
	void HandleMissionFailed(const FText& Reason);

	UPROPERTY(Transient)
	TObjectPtr<UMissionFailedWidget> MissionFailed;

	UFUNCTION()
	void HandleMainMenuChanged(bool bOpen);

	UPROPERTY(Transient)
	TObjectPtr<UMainMenuWidget> MainMenu;

	UFUNCTION()
	void HandleDialogueChanged(bool bOpen);

	UPROPERTY(Transient)
	TObjectPtr<UDialogueWidget> Dialogue;

	UPROPERTY(Transient)
	TObjectPtr<UActionBarWidget> ActionBar;

	UPROPERTY(Transient)
	TObjectPtr<UInventoryDrawerWidget> InventoryDrawer;

	UPROPERTY(Transient)
	TObjectPtr<UTransferDialogWidget> TransferDialog;

	UPROPERTY(Transient)
	TObjectPtr<UProfileDialogWidget> ProfileDialog;

	UPROPERTY(Transient)
	TObjectPtr<UVictoryPanelWidget> VictoryPanel;

	/** Godot wave-clear victory: the profile of the first member with free points opens. */
	UFUNCTION()
	void HandleWaveCleared(int32 WaveIndex);

	UPROPERTY(Transient)
	TObjectPtr<UPauseMenuWidget> PauseMenu;

	UPROPERTY(Transient)
	TObjectPtr<USaveLoadDialogWidget> SaveLoadDialog;

	UPROPERTY(Transient)
	TObjectPtr<UPhaseBannersWidget> PhaseBanners;

	UPROPERTY(Transient)
	TObjectPtr<UTurnBasedHudWidget> TurnBasedHud;

	void DrawMessageFeed();
	/** Turn-based attack mode: the hovered cell's hit chance (Godot hit_chance_label). */
	void DrawHitChanceLabel();
	/** The box being dragged to select squad members (Godot selection_box_canvas). */
	void DrawSelectionBox();
	/** Sprint 10: a green shield «РУБЕЖ» over every defended object / point (UDefenseMarkerSubsystem). */
	void DrawDefenseMarkers();
	UPROPERTY(Transient)
	TObjectPtr<class UFrostVignetteWidget> FrostVignette;
	/** Draws the objective banner; returns its bottom edge (Y). */
	float DrawObjectiveBanner();
	void DrawSquadPanel(float Top);
	void DrawOperativeLabels();
	/** Floating combat texts (UFloatingTextSubsystem; Godot Label3D _spawn_floating_combat_text). */
	void DrawFloatingTexts();
	/** Space-hold charge bar in the screen centre (Godot gorky17_combat_hud charge_bar_container). */
	void DrawSpaceCharge();
	/** Top centre: the combat time mode label with its Space hints, and the selected operative's fire posture with the , . / hints. */
	void DrawCombatModeBadge();
	/** Per-operative fire posture marker over every living operative (letter П / О / А in the posture colour). */
	void DrawPostureMarkers();
	/** Horde warning (UHordeSubsystem): «ОРДА!» banner, distance, and an arrow at the screen edge towards it. */
	void DrawHordeWarning();
	/** Colour of a fire posture on the HUD (Passive grey, Defensive amber, Aggressive red). */
	static FLinearColor PostureMarkerColor(ESquadFirePosture Posture);
	/** Overhead labels of enemies and deployables / the generator (Godot overhead Label3D). */
	void DrawWorldLabels();
	/** Splits Text into lines no wider than MaxWidth pixels. */
	TArray<FString> WrapText(const FString& Text, UFont* Font, float Scale, float MaxWidth) const;
	FString DescribeOperative(const AOperativeCharacter& Operative, bool bLeader) const;
};
