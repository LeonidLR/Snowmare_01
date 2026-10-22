#pragma once

#include "CoreMinimal.h"
#include "Environment/EnvironmentTypes.h"

class FJsonObject;

/**
 * Day cycle + weather tuning (UE-only, no Godot reference; design: docs/systems/environment-weather.md): the file
 * Content/Data/Environment/environment.json, read over the FEnvironmentConfig defaults. Keys are the snake_case names of
 * FEnvironmentConfig (early_morning_start_hour, midday_real_seconds, weather_dwell_phase_fraction, start_weight_clear, ...;
 * hours = game clock, seconds = REAL seconds; phase 2: cold_mult_*, cold_on_locked_levels, and the nested "roster" object
 * with pairs / bonus_types / suitability tables keyed by level-JSON enemy type names: "HOUND", "SPITTER", ...).
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
