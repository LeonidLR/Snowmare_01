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

	/** Pause / preparation / wave banners and the cutscene card (a Widget Blueprint subclass can restyle it). */
	UPROPERTY(EditDefaultsOnly, Category = "CodexTactics|HUD")
	TSubclassOf<UPhaseBannersWidget> PhaseBannersWidgetClass;

	/** Turn-based action panel class (a Widget Blueprint subclass can restyle it). */
	UPROPERTY(EditDefaultsOnly, Category = "CodexTactics|HUD")
	TSubclassOf<UTurnBasedHudWidget> TurnBasedHudWidgetClass;

	/** The bottom action bar (with the weapon selector); null before BeginPlay. */
	UActionBarWidget* GetActionBar() const { return ActionBar; }

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
	TObjectPtr<UPhaseBannersWidget> PhaseBanners;

	UPROPERTY(Transient)
	TObjectPtr<UTurnBasedHudWidget> TurnBasedHud;

	void DrawMessageFeed();
	/** Draws the objective banner; returns its bottom edge (Y). */
	float DrawObjectiveBanner();
	void DrawSquadPanel(float Top);
	void DrawOperativeLabels();
	/** Splits Text into lines no wider than MaxWidth pixels. */
	TArray<FString> WrapText(const FString& Text, UFont* Font, float Scale, float MaxWidth) const;
	FString DescribeOperative(const AOperativeCharacter& Operative, bool bLeader) const;
};
