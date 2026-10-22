#pragma once

#include "CoreMinimal.h"
#include "WeatherSnowFxRules.generated.h"

/**
 * Tuning of the falling snow for the tactical camera (environment phase 4b, docs/systems/environment-weather.md §12). Visual only.
 * Values in UE units (cm, s). Defaults: Snowfall (intensity ~0.5) = calm, readable flakes; Blizzard (1) = dense, fast, sideways sheets.
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FWeatherSnowFxTuning
{
	GENERATED_BODY()

	/** Flakes per second at intensity 1 over the whole spawn volume of NS_Weather_Snow; scaled by intensity^SpawnRateExponent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow", meta = (ClampMin = "0"))
	float SpawnRateAtFull = 24000.f;

	/** >1 keeps calm snowfall sparse and the blizzard dense (Snowfall 0.6 -> 0.36 of the blizzard rate at 2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow", meta = (ClampMin = "0.1"))
	float SpawnRateExponent = 2.f;

	/** Flake opacity at a trace of snow and at intensity 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow", meta = (ClampMin = "0", ClampMax = "4"))
	float OpacityMin = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow", meta = (ClampMin = "0", ClampMax = "4"))
	float OpacityMax = 1.f;

	/** Ground drift ("pozyomka") starts above this intensity and reaches DriftOpacityMax at 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drift", meta = (ClampMin = "0", ClampMax = "1"))
	float DriftStartIntensity = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drift", meta = (ClampMin = "0", ClampMax = "4"))
	float DriftOpacityMax = 0.65f;

	/** Drift puff base size (cm; NS_Weather_Snow multiplies it by a random 1.2..2.5) at the start intensity and at 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drift", meta = (ClampMin = "0"))
	float DriftSpriteSizeMin = 250.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drift", meta = (ClampMin = "0"))
	float DriftSpriteSizeMax = 500.f;

	/**
	 * The system stands this far UPWIND of the view focus at wind 1 (scaled by wind^2, like the asset's wind speed): with a strong wind
	 * the flakes cross the view instead of being born in it, so the blizzard's sideways sheets fill the whole view. Keep it in step with
	 * NS_Weather_Snow's spawn radius / flake wind speed (create_weather_snow_fx.py SPAWN_RADIUS, FLAKE_WIND).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind", meta = (ClampMin = "0"))
	float UpwindOffsetAtFullWind = 4000.f;

	/** The wind the system receives never drops below this (snow always drifts a little). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind", meta = (ClampMin = "0", ClampMax = "1"))
	float WindStrengthFloor = 0.1f;

	/** Intensity follows the weather at most this fast (per second) up / down (an instant ForceWeather fades in). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoothing", meta = (ClampMin = "0.01"))
	float IntensityRisePerSecond = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoothing", meta = (ClampMin = "0.01"))
	float IntensityFallPerSecond = 0.25f;

	/** Below this smoothed intensity nothing spawns and the system deactivates softly (the flakes in the air still land). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoothing", meta = (ClampMin = "0"))
	float OffThreshold = 0.02f;

};

/** The contract values written to the Niagara User parameters (docs §5). */
struct CODEXTACTICS_API FWeatherSnowFxParams
{
	float WeatherIntensity = 0.f;
	float SpawnRate = 0.f;
	float Opacity = 0.f;
	float WindStrength = 0.f;
	FVector WindDirection = FVector::ForwardVector;
	float DriftOpacity = 0.f;
	float DriftSpriteSize = 0.f;
};

/** Pure logic of UWeatherSnowFxComponent (tests: CodexTactics.Environment.SnowFx.*). */
namespace WeatherSnowFxRules
{
	/** Moves Current toward Target (both clamped 0..1) by the rise / fall rate of Tuning over DeltaSeconds. */
	CODEXTACTICS_API float StepIntensity(float Current, float Target, float DeltaSeconds, const FWeatherSnowFxTuning& Tuning);

	/**
	 * The User parameter values for a smoothed Intensity (0..1) and Wind (0..1, horizontal direction; a zero direction falls back to +X).
	 * Intensity under OffThreshold -> nothing spawns.
	 */
	CODEXTACTICS_API FWeatherSnowFxParams ComputeParams(float Intensity, float Wind, const FVector& WindDirection, const FWeatherSnowFxTuning& Tuning);

	/** Offset of the system from the view focus: upwind (against the horizontal WindDirection) by UpwindOffsetAtFullWind x Wind^2. */
	CODEXTACTICS_API FVector ComputeSpawnOffset(const FVector& WindDirection, float Wind, const FWeatherSnowFxTuning& Tuning);

	/**
	 * Where the view looks at the ground: the camera ray hits the horizontal plane at GroundZ (cm); a ray that never meets it (looking up or
	 * flat) -> the point FallbackDistance ahead projected to GroundZ.
	 */
	CODEXTACTICS_API FVector ComputeViewFocus(const FVector& CameraLocation, const FVector& CameraForward, float GroundZ, float FallbackDistance);
}
