#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/** Stealth mode tuning (designer task 003, UE-only; no Godot reference). Data: Data/AI/stealth_mode.json. */
struct CODEXTACTICS_API FStealthModeConfig
{
	bool bEnabled = true;
	float DetectionRangeMultiplier = 0.4f;
	float DetectionBuildupMultiplier = 0.5f;
	float FootstepNoiseMultiplier = 0.5f;
	bool bBreakOnFire = true;
	float VisualOpacity = 0.4f;
	float VisualGray = 0.45f;
	// Stealth v2 (008): fast crouched gait, fast perception multipliers, and threat look-lock tuning.
	float FastSpeedCm = 300.f;
	float FastDetectionRangeMultiplier = 0.6f;
	float FastDetectionBuildupMultiplier = 0.75f;
	float LookLockKeepSeconds = 2.0f;
	float LookLockFovDeg = 100.0f;
	float LookLockSwitchRatio = 0.7f;
};

namespace StealthModeRules
{
	/** Toggle a mixed selection on; toggle an entirely stealthed selection off. An empty selection never enables it. */
	CODEXTACTICS_API bool EnableForSelection(int32 SelectedCount, int32 StealthedCount);
	/** Formation blocks stealth: allowed only in SOLO mode or while followers hold position (`bSoloOrHolding`), when guarding outside the formation, or when no active formation runs. */
	CODEXTACTICS_API bool IsBlockedByFormation(bool bSoloOrHolding, bool bFormationActive, bool bGuarding);
	/** Localized tooltip/refusal shown when formation blocks stealth entry. */
	CODEXTACTICS_API FText GetFormationBlockedTooltip();
	/** Sprint always breaks stealth; a shot breaks it only when stealth_break_on_fire is enabled. */
	CODEXTACTICS_API bool ShouldBreak(bool bActive, bool bFired, bool bSprinting, const FStealthModeConfig& Config);
	/** Multipliers relative to the existing stance/weather/perception rules; disabled/inactive = identity. */
	CODEXTACTICS_API float DetectionRangeScale(bool bActive, const FStealthModeConfig& Config);
	CODEXTACTICS_API float DetectionRangeScale(bool bActive, bool bFast, const FStealthModeConfig& Config);
	CODEXTACTICS_API float DetectionBuildupScale(bool bActive, const FStealthModeConfig& Config);
	CODEXTACTICS_API float DetectionBuildupScale(bool bActive, bool bFast, const FStealthModeConfig& Config);
	CODEXTACTICS_API float FootstepNoiseScale(bool bActive, const FStealthModeConfig& Config);
	CODEXTACTICS_API float FootstepNoiseScale(bool bActive, bool bFast, const FStealthModeConfig& Config);
	/** Optional JSON keys over Base; malformed values keep Base; multipliers/visuals clamp to [0, 1], speeds to [0, 2000], and look-lock tuning to non-negative bounds. */
	CODEXTACTICS_API FStealthModeConfig FromJson(const FJsonObject& Json, const FStealthModeConfig& Base = FStealthModeConfig());
	/** Loads the project data once. A missing/invalid file keeps the documented defaults. */
	CODEXTACTICS_API const FStealthModeConfig& GetConfig();
	CODEXTACTICS_API FString GetDefaultPath();
}
