#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Environment/WeatherSnowFxRules.h"
#include "SnowFxSettings.generated.h"

/**
 * The look of the falling snow, edited in the editor without code (user request 2026-10-23): /Game/Environment/Weather/Snow/DA_SnowFX.
 * UWeatherSnowFxComponent reads it (soft reference, this default path) and pushes the values to NS_Weather_Snow's User parameters on every
 * refresh, so a change made while PIE runs shows within a fraction of a second. Without the asset the component uses the C++ defaults of
 * FWeatherSnowFxTuning. How much snow falls per weather stays in the presets (DA_Weather_<State>.SnowIntensity / WindStrength).
 * Docs: docs/systems/environment-weather.md §12.b. Godot reference: none (UE-only).
 */
UCLASS(BlueprintType)
class CODEXTACTICS_API USnowFxSettings : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Default asset the component loads. */
	static const TCHAR* DefaultAssetPath;

	/** Flakes, ground drift, wind, indoors and smoothing (see the tooltips; sizes in cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Snow FX", meta = (ShowOnlyInnerProperties))
	FWeatherSnowFxTuning Tuning;
};
