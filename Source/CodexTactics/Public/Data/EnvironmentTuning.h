#pragma once

#include "CoreMinimal.h"
#include "Environment/EnvironmentTypes.h"

class FJsonObject;

/**
 * Day cycle + weather tuning (UE-only, no Godot reference; design: docs/systems/environment-weather.md): the file
 * Content/Data/Environment/environment.json, read over the FEnvironmentConfig defaults. Keys are the snake_case names of
 * FEnvironmentConfig (early_morning_start_hour, midday_real_seconds, weather_dwell_phase_fraction, start_weight_clear, ...;
 * hours = game clock, seconds = REAL seconds; phase 2: cold_mult_*, cold_on_locked_levels, and the nested "roster" object
 * with pairs / bonus_types / suitability tables keyed by level-JSON enemy type names: "HOUND", "SPITTER", ...; phase 4:
 * snow_cover_rate_snowfall / snow_cover_rate_blizzard (cover gained per real second while it snows), snow_cover_start; section 17:
 * the nested "temperate" object - the Temperate climate profile: cold_mult_sunny / _overcast / _rain / _storm and the phase multipliers,
 * roster_enabled, falling_snow, screen_drops, rain_intensity_*, wetness_rate_*, drying_rate_*, puddle_*, start_wetness_* / start_puddles_*).
 * A missing key keeps the default (the design value); a roster list that is present replaces the default list, a suitability
 * row without all four weathers (4 scores each) or with an unknown type is skipped with a warning. Unreal is the master copy.
 * Console: CodexTactics.DumpEnvironment.
 */
namespace EnvironmentTuning
{
	/** Content/Data/Environment/environment.json. */
	CODEXTACTICS_API FString GetDefaultPath();
	/** The values in force (defaults until ApplyFile / Set). */
	CODEXTACTICS_API const FEnvironmentConfig& Get();
	CODEXTACTICS_API void Set(const FEnvironmentConfig& Config);
	/** Values of Json over Base. */
	CODEXTACTICS_API FEnvironmentConfig FromJson(const FJsonObject& Json, const FEnvironmentConfig& Base = FEnvironmentConfig());
	CODEXTACTICS_API TSharedRef<FJsonObject> ToJson(const FEnvironmentConfig& Config);
	/** Loads the file into Get(); false (values kept) when it is missing or unreadable. */
	CODEXTACTICS_API bool ApplyFile(const FString& Path);
}
