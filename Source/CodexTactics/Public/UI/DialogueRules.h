#pragma once

#include "CoreMinimal.h"

/** Look of the speaker card for one speaker. */
struct CODEXTACTICS_API FDialogueSpeakerStyle
{
	/** Portrait: Godot shows an emoji (🎖️ 🔧 🩺 🧔 👤); the UE fonts have none, so a short role tag stands in. */
	FText Portrait;
	FText Role;
	/** Godot sRGB colours (convert with ACodexTacticsHUD::GodotColor). */
	FLinearColor NameColor;
	FLinearColor CardColor;
	FLinearColor CardBorderColor;
};

/** What the dialogue key (Enter) does at one moment of a press (DialogueRules::ResolveDialogueKey). */
enum class EDialogueKeyAction : uint8
{
	None,
	/** Released before the hold threshold: the next line. */
	NextLine,
	/** Held for the threshold: the whole conversation ends (fires once, while the key is still down). */
	SkipAll
};

/**
 * Pure rules of the bottom dialogue window.
 * Godot reference: Scenes/ui/dialogue/bottom_dialogue_dialog.gd _apply_speaker_styling, _update_current_line_display.
 */
namespace DialogueRules
{
	/** Card by speaker name (case-insensitive substrings: susanin / commander / engineer / medic, sapper ...). */
	CODEXTACTICS_API FDialogueSpeakerStyle GetSpeakerStyle(const FString& Speaker);

	/** Next-button text: "Next >" before the last line; on it the custom text, "Join the Squad", "To Battle!" or "Understood!". */
	CODEXTACTICS_API FText GetNextButtonText(bool bLastLine, const FString& CustomFinishText, bool bRecruitment, bool bCombatPhase);

	/** «[N / M]». */
	CODEXTACTICS_API FText GetProgressText(int32 LineIndex, int32 LineCount);

	/** Seconds Enter must be held to skip the whole conversation (user request 2026-10-13: "hold ~0.6 s"). */
	constexpr float SkipHoldSeconds = 0.6f;

	/**
	 * The dialogue key (Enter, UE-only, user request 2026-10-13; story dialogue window and tutorial radio / walk-and-talk):
	 * a tap (released before HoldThreshold) = the next line, a hold of HoldThreshold = skip the whole conversation, fired once
	 * while the key is still down (bSkipFired: it already fired during this press, so the release does nothing).
	 */
	CODEXTACTICS_API EDialogueKeyAction ResolveDialogueKey(float HeldSeconds, bool bReleased, bool bSkipFired, float HoldThreshold = SkipHoldSeconds);

	/** The hint on the dialogue panels: "[Enter] next  ·  hold [Enter] skip" (the key label comes from the command registry). */
	CODEXTACTICS_API FText GetSkipHintText(const FString& NextKeyLabel, const FString& SkipKeyLabel);
}
