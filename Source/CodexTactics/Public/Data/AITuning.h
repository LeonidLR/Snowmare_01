#pragma once

#include "CoreMinimal.h"

/**
 * AI tuning chosen by the Jev AI coach (Scripts/Tools/jev_ai_coach.py, UE-only, no Godot reference): the coach runs bot
 * batches over candidate Codex.* console variables, has TypeSafe Jev judge the fights and writes the winner to
 * Content/Data/AI/ai_tuning.json ({"cvars": {"Codex.Marksman.RetreatCooldown": 10, ...}}). The game mode applies it
 * at StartPlay with game-setting priority, so a -dpcvars= value (the coach's own experiments) still wins; the
 * -NoAITuning argument skips the file.
 */
namespace AITuning
{
	/** Content/Data/AI/ai_tuning.json. */
	CODEXTACTICS_API FString GetDefaultPath();
	/** Sets every "Codex."-prefixed console variable of the JSON's "cvars" object; returns how many were set. */
	CODEXTACTICS_API int32 ApplyJson(const FString& Json);
	/** ApplyJson on the file (0 when it is missing, unreadable or -NoAITuning is given). */
	CODEXTACTICS_API int32 ApplyFile(const FString& Path);
}
