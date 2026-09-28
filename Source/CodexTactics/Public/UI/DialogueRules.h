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

/**
 * Pure rules of the bottom dialogue window.
 * Godot reference: Scenes/ui/dialogue/bottom_dialogue_dialog.gd _apply_speaker_styling, _update_current_line_display.
 */
namespace DialogueRules
{
	/** Card by speaker name (case-insensitive substrings: сусанин / командир / инженер / медик, сапёр …). */
	CODEXTACTICS_API FDialogueSpeakerStyle GetSpeakerStyle(const FString& Speaker);

	/** Next-button text: «Далее ▶» before the last line; on it the custom text, «🤝 Вступить в отряд», «В бой! ▶» or «Понял! ▶». */
	CODEXTACTICS_API FText GetNextButtonText(bool bLastLine, const FString& CustomFinishText, bool bRecruitment, bool bCombatPhase);

	/** «[N / M]». */
	CODEXTACTICS_API FText GetProgressText(int32 LineIndex, int32 LineCount);
}
