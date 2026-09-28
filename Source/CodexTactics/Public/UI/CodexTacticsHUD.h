#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "CodexTacticsHUD.generated.h"

class AOperativeCharacter;
class UFont;

/**
 * Baseline canvas HUD until the UMG interface is ported: message feed (top right), squad status panel
 * (top left: phase, mode, per-operative stance / health / cold / ammo) and labels above the operatives.
 * Godot reference: Scenes/movements/main.gd message panel (`_on_quest_message`) and squad status labels.
 * Toggle the status panel and labels with the console variable CodexTactics.HUD.ShowStatus.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API ACodexTacticsHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	/** Messages shown in the feed. */
	UPROPERTY(EditDefaultsOnly, Category = "CodexTactics|HUD", meta = (ClampMin = "1"))
	int32 MaxFeedMessages = 8;

	/** Seconds (real time) a message stays in the feed. */
	UPROPERTY(EditDefaultsOnly, Category = "CodexTactics|HUD", meta = (ClampMin = "1"))
	float FeedMessageLifetime = 25.f;

	/** Feed width as a fraction of the viewport width. */
	UPROPERTY(EditDefaultsOnly, Category = "CodexTactics|HUD", meta = (ClampMin = "0.1", ClampMax = "0.9"))
	float FeedWidthFraction = 0.38f;

	/** Removes emoji and pictographs the default canvas font cannot render. */
	static FString StripUnsupportedGlyphs(const FString& Text);

private:
	void DrawMessageFeed();
	void DrawSquadPanel();
	void DrawOperativeLabels();
	/** Splits Text into lines no wider than MaxWidth pixels. */
	TArray<FString> WrapText(const FString& Text, UFont* Font, float Scale, float MaxWidth) const;
	FString DescribeOperative(const AOperativeCharacter& Operative, bool bLeader) const;
};
