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
};

namespace StealthModeRules
{
	/** Toggle a mixed selection on; toggle an entirely stealthed selection off. An empty selection never enables it. */
	CODEXTACTICS_API bool EnableForSelection(int32 SelectedCount, int32 StealthedCount);
	/** Sprint always breaks stealth; a shot breaks it only when stealth_break_on_fire is enabled. */
	CODEXTACTICS_API bool ShouldBreak(bool bActive, bool bFired, bool bSprinting, const FStealthModeConfig& Config);
	/** Multipliers relative to the existing stance/weather/perception rules; disabled/inactive = identity. */
	CODEXTACTICS_API float DetectionRangeScale(bool bActive, const FStealthModeConfig& Config);
	CODEXTACTICS_API float DetectionBuildupScale(bool bActive, const FStealthModeConfig& Config);
	CODEXTACTICS_API float FootstepNoiseScale(bool bActive, const FStealthModeConfig& Config);
	/** Optional JSON keys over Base; malformed values keep the base and multipliers/visuals are clamped to [0, 1]. */
	CODEXTACTICS_API FStealthModeConfig FromJson(const FJsonObject& Json, const FStealthModeConfig& Base = FStealthModeConfig());
	/** Loads the project data once. A missing/invalid file keeps the documented defaults. */
	CODEXTACTICS_API const FStealthModeConfig& GetConfig();
	CODEXTACTICS_API FString GetDefaultPath();
}
