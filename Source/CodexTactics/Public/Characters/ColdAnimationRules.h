#pragma once

#include "CoreMinimal.h"

/**
 * Cold presentation layer rules (never changes movement): which of the four cold levels shows and how its weight
 * fades. Godot reference: Scripts/components/cold_animation_controller.gd, resources/character_animation_config.gd.
 */
namespace ColdAnimationRules
{
	/**
	 * Cold level 0..4 from the cold (0..100): rises past each threshold, falls only below threshold - hysteresis.
	 * Needs exactly 4 thresholds, otherwise 0 (Godot select_tier).
	 */
	CODEXTACTICS_API int32 SelectTier(int32 CurrentTier, float Cold, const TArray<float>& Thresholds, float Hysteresis);

	/**
	 * Index of the clip shown at Level (1..4): the level's own entry or the nearest lower one that is set,
	 * INDEX_NONE when none is (Godot resolve_clip; its "idle" / "walk_fwd" fallback = no cold pose here).
	 */
	CODEXTACTICS_API int32 ResolveClipIndex(const TArray<bool>& ClipSet, int32 Level);

	/**
	 * Layer weight: 0 at once when not eligible (combat / actions take priority), else towards 1 (a cold level shows)
	 * or 0 over FadeSeconds (Godot update / suspend).
	 */
	CODEXTACTICS_API float StepWeight(float Weight, int32 Tier, bool bEligible, float DeltaSeconds, float FadeSeconds = 0.3f);
}
