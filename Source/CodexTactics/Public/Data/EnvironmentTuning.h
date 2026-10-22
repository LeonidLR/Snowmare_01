#pragma once

#include "CoreMinimal.h"
#include "Environment/EnvironmentTypes.h"

class FJsonObject;

/**
 * Day cycle + weather tuning (UE-only, no Godot reference; design: docs/systems/environment-weather.md): the file
 * Content/Data/Environment/environment.json, read over the FEnvironmentConfig defaults. Keys are the snake_case names of
 * FEnvironmentConfig (early_morning_start_hour, midday_real_seconds, weather_dwell_phase_fraction, start_weight_clear, ...;
 * hours = game clock, seconds = REAL seconds). A missing key keeps the default (the design value). Unreal is the master copy.
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
