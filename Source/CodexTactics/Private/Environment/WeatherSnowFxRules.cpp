#include "Environment/WeatherSnowFxRules.h"

float WeatherSnowFxRules::StepIntensity(float Current, float Target, float DeltaSeconds, const FWeatherSnowFxTuning& Tuning)
{
	Current = FMath::Clamp(Current, 0.f, 1.f);
	Target = FMath::Clamp(Target, 0.f, 1.f);
	const float Dt = FMath::Max(DeltaSeconds, 0.f);
	if (Target > Current)
	{
		return FMath::Min(Target, Current + Tuning.IntensityRisePerSecond * Dt);
	}
	return FMath::Max(Target, Current - Tuning.IntensityFallPerSecond * Dt);
}

FWeatherSnowFxParams WeatherSnowFxRules::ComputeParams(float Intensity, float Wind, const FVector& WindDirection, const FWeatherSnowFxTuning& Tuning)
{
	FWeatherSnowFxParams Params;
	const float I = FMath::Clamp(Intensity, 0.f, 1.f);
	Params.WeatherIntensity = I;
	const FVector Flat(WindDirection.X, WindDirection.Y, 0.);
	Params.WindDirection = Flat.IsNearlyZero() ? FVector::ForwardVector : Flat.GetSafeNormal();
	Params.WindStrength = FMath::Max(FMath::Clamp(Wind, 0.f, 1.f), Tuning.WindStrengthFloor);
	if (I < Tuning.OffThreshold)
	{
		return Params;
	}
	Params.SpawnRate = Tuning.SpawnRateAtFull * FMath::Pow(I, FMath::Max(Tuning.SpawnRateExponent, 0.1f));
	Params.Opacity = FMath::Lerp(Tuning.OpacityMin, Tuning.OpacityMax, I);
	const float DriftSpan = FMath::Max(1.f - Tuning.DriftStartIntensity, KINDA_SMALL_NUMBER);
	const float Drift = FMath::Clamp((I - Tuning.DriftStartIntensity) / DriftSpan, 0.f, 1.f);
	Params.DriftOpacity = Tuning.DriftOpacityMax * Drift;
	Params.DriftSpriteSize = FMath::Lerp(Tuning.DriftSpriteSizeMin, Tuning.DriftSpriteSizeMax, Drift);
	return Params;
}

FVector WeatherSnowFxRules::ComputeSpawnOffset(const FVector& WindDirection, float Wind, const FWeatherSnowFxTuning& Tuning)
{
	const FVector Flat(WindDirection.X, WindDirection.Y, 0.);
	const float W = FMath::Clamp(Wind, 0.f, 1.f);
	return Flat.IsNearlyZero() ? FVector::ZeroVector : -Flat.GetSafeNormal() * (Tuning.UpwindOffsetAtFullWind * W * W);
}

FVector WeatherSnowFxRules::ComputeViewFocus(const FVector& CameraLocation, const FVector& CameraForward, float GroundZ, float FallbackDistance)
{
	const FVector Forward = CameraForward.GetSafeNormal();
	const double Drop = CameraLocation.Z - GroundZ;
	if (Forward.Z < -KINDA_SMALL_NUMBER && Drop > 0.)
	{
		return CameraLocation + Forward * (Drop / -Forward.Z);
	}
	FVector Ahead = CameraLocation + Forward * FallbackDistance;
	Ahead.Z = GroundZ;
	return Ahead;
}
