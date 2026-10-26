#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Environment/WeatherRainFxRules.h"
#include "RainFxSettings.generated.h"

/**
 * The look of the rain on temperate (narrative) levels, edited in the editor without code (like DA_SnowFX):
 * /Game/Environment/Weather/Rain/DA_RainFX. UWeatherRainFxComponent reads it (soft reference, this default path) and pushes the values to
 * NS_Weather_Rain / NS_Weather_PuddleSplashes User parameters, the drop material, the screen-drops post process and the lightning flash on
 * every refresh, so a change made while PIE runs shows within a fraction of a second. Without the asset the component uses the C++ defaults
 * of FWeatherRainFxTuning. Docs: docs/systems/environment-weather.md section 18. Godot reference: none (UE-only).
 */
UCLASS(BlueprintType)
class CODEXTACTICS_API URainFxSettings : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Default asset the component loads. */
	static const TCHAR* DefaultAssetPath;

	/** Drops, splashes, puddle splashes, screen drops, lightning, wind, indoors and smoothing (see the tooltips; sizes in cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rain FX", meta = (ShowOnlyInnerProperties))
	FWeatherRainFxTuning Tuning;
};
