#include "Environment/WeatherRainFxRules.h"

#include "Math/RandomStream.h"

namespace WeatherRainFxRulesLocal
{
	/** A fraction 0..1 from (Seed, Index, Salt): a fresh stream per query, so no shared RNG is ever advanced. */
	float Fraction(int32 Seed, int32 Index, uint32 Salt)
	{
		const uint32 Hash = HashCombine(HashCombine(GetTypeHash(Seed), GetTypeHash(Index)), Salt * 2654435761u);
		return FRandomStream(static_cast<int32>(Hash)).GetFraction();
	}
}

float WeatherRainFxRules::GetLookIntensity(float RainAmount, float StormAmount, const FWeatherRainFxTuning& Tuning)
{
	const float Rain = FMath::Clamp(RainAmount, 0.f, 1.f);
	const float Storm = FMath::Clamp(StormAmount, 0.f, 1.f);
	return FMath::Clamp(Rain * Tuning.RainLookIntensity + Storm * Tuning.StormLookIntensity, 0.f, 1.f);
}

float WeatherRainFxRules::StepIntensity(float Current, float Target, float DeltaSeconds, const FWeatherRainFxTuning& Tuning)
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

FWeatherRainFxParams WeatherRainFxRules::ComputeParams(float Intensity, float Wind, const FVector& WindDirection, const FWeatherRainFxTuning& Tuning)
{
	FWeatherRainFxParams Params;
	const float I = FMath::Clamp(Intensity, 0.f, 1.f);
	Params.Intensity = I;
	const FVector Flat(WindDirection.X, WindDirection.Y, 0.);
	Params.WindDirection = Flat.IsNearlyZero() ? FVector::ForwardVector : Flat.GetSafeNormal();
	Params.WindStrength = FMath::Max(FMath::Clamp(Wind, 0.f, 1.f), Tuning.WindStrengthFloor);

	// The look itself does not depend on the intensity (a min above its max is taken as one value).
	Params.DropLengthMin = FMath::Max(Tuning.DropLengthMin, 1.f);
	Params.DropLengthMax = FMath::Max(Tuning.DropLengthMax, Params.DropLengthMin);
	Params.DropWidthMin = FMath::Max(Tuning.DropWidthMin, 0.1f);
	Params.DropWidthMax = FMath::Max(Tuning.DropWidthMax, Params.DropWidthMin);
	Params.OpacityRandomMin = FMath::Clamp(Tuning.DropOpacityRandomMin, 0.f, 1.f);
	Params.Gravity = FVector(0., 0., -FMath::Max(Tuning.DropAcceleration, 0.f));
	Params.FallSpeed = FMath::Max(Tuning.FallSpeed, 10.f);
	Params.Lifetime = FMath::Max(Tuning.DropLifetime, 0.1f);
	// Wind^2 like the snow: a breeze barely tilts the rain, a storm slants it hard.
	Params.DropWindSpeed = FMath::Max(Tuning.DropWindSpeed, 0.f) * Params.WindStrength * Params.WindStrength;
	Params.SpawnRadius = FMath::Max(Tuning.SpawnRadius, 100.f);
	Params.SpawnHeight = FMath::Max(Tuning.SpawnHeight, 100.f);
	Params.SplashSizeMin = FMath::Max(Tuning.SplashSizeMin, 1.f);
	Params.SplashSizeMax = FMath::Max(Tuning.SplashSizeMax, Params.SplashSizeMin);
	Params.SplashRadius = FMath::Max(Tuning.SplashRadius, 100.f);
	if (I < Tuning.OffThreshold)
	{
		return Params;
	}
	Params.SpawnRate = Tuning.SpawnRateAtFull * FMath::Pow(I, FMath::Max(Tuning.SpawnRateExponent, 0.1f));
	Params.Opacity = FMath::Lerp(Tuning.OpacityMin, Tuning.OpacityMax, I);
	Params.SplashRate = Tuning.bSplashes ? Tuning.SplashRateAtFull * I : 0.f;
	Params.SplashOpacity = Tuning.bSplashes ? FMath::Max(Tuning.SplashOpacity, 0.f) : 0.f;
	return Params;
}

FVector WeatherRainFxRules::ComputeSpawnOffset(const FVector& WindDirection, float Wind, const FWeatherRainFxTuning& Tuning)
{
	const FVector Flat(WindDirection.X, WindDirection.Y, 0.);
	const float W = FMath::Clamp(Wind, 0.f, 1.f);
	return Flat.IsNearlyZero() ? FVector::ZeroVector : -Flat.GetSafeNormal() * (Tuning.UpwindOffsetAtFullWind * W * W);
}

float WeatherRainFxRules::ComputePuddleSplashRate(float Intensity, float PuddleAmount, const FWeatherRainFxTuning& Tuning)
{
	const float I = FMath::Clamp(Intensity, 0.f, 1.f);
	const float Puddles = FMath::Clamp(PuddleAmount, 0.f, 1.f);
	if (!Tuning.bPuddleSplashes || I < Tuning.OffThreshold || Puddles < Tuning.PuddleSplashMinPuddles)
	{
		return 0.f;
	}
	return FMath::Max(Tuning.PuddleSplashRateAtFull, 0.f) * I * Puddles;
}

float WeatherRainFxRules::ComputeScreenDropsStrength(float ScreenDrops, float RainIntensity, const FWeatherRainFxTuning& Tuning)
{
	if (!Tuning.bScreenDrops)
	{
		return 0.f;
	}
	return FMath::Max(Tuning.ScreenDropsStrength, 0.f) * FMath::Clamp(ScreenDrops, 0.f, 1.f) * FMath::Clamp(RainIntensity, 0.f, 1.f);
}

FWeatherSnowFxTuning WeatherRainFxRules::ToIndoorTuning(const FWeatherRainFxTuning& Tuning)
{
	FWeatherSnowFxTuning Indoor;
	Indoor.bNoSnowIndoors = Tuning.bNoRainIndoors;
	Indoor.IndoorSearchRadius = Tuning.IndoorSearchRadius;
	Indoor.IndoorMarginCm = Tuning.IndoorMarginCm;
	Indoor.IndoorBelowCm = Tuning.IndoorBelowCm;
	Indoor.bKillUnderRoofs = Tuning.bKillUnderRoofs;
	Indoor.RoofTraceHeight = Tuning.RoofTraceHeight;
	Indoor.RoofMaxSizeCm = Tuning.RoofMaxSizeCm;
	return Indoor;
}

bool WeatherRainFxRules::IsLightningSuppressed(int32 ConsoleValue, bool bAutomatedRun)
{
	if (ConsoleValue == 0)
	{
		return true;
	}
	if (ConsoleValue > 0)
	{
		return false;
	}
	return bAutomatedRun;
}

bool WeatherRainFxRules::IsLightningActive(float StormAmount, bool bSuppressed, const FWeatherRainFxTuning& Tuning)
{
	return Tuning.bLightning && !bSuppressed && StormAmount >= FMath::Max(Tuning.LightningMinStorm, 0.001f);
}

bool WeatherRainFxRules::IsLightningAllowed(bool bTemperateClimate, float StormAmount, bool bSuppressed, const FWeatherRainFxTuning& Tuning)
{
	return bTemperateClimate && IsLightningActive(StormAmount, bSuppressed, Tuning);
}

float WeatherRainFxRules::ComputeStrikeDelay(int32 Seed, int32 Index, const FWeatherRainFxTuning& Tuning)
{
	const float Min = FMath::Max(Tuning.LightningIntervalMin, 0.5f);
	const float Max = FMath::Max(Tuning.LightningIntervalMax, Min);
	return FMath::Lerp(Min, Max, WeatherRainFxRulesLocal::Fraction(Seed, Index, 1u));
}

int32 WeatherRainFxRules::GetPulseCount(int32 Seed, int32 Index, const FWeatherRainFxTuning& Tuning)
{
	const int32 MaxPulses = FMath::Clamp(Tuning.LightningMaxPulses, 1, 6);
	if (MaxPulses <= 1)
	{
		return 1;
	}
	const int32 Span = MaxPulses - 2 + 1; // 2..MaxPulses
	return 2 + FMath::Min(Span - 1, FMath::FloorToInt(WeatherRainFxRulesLocal::Fraction(Seed, Index, 2u) * Span));
}

float WeatherRainFxRules::ComputeFlashScale(float SecondsSinceStrike, int32 Seed, int32 Index, const FWeatherRainFxTuning& Tuning)
{
	const float Duration = FMath::Max(Tuning.LightningFlashSeconds, 0.05f);
	if (SecondsSinceStrike < 0.f || SecondsSinceStrike >= Duration)
	{
		return 0.f;
	}
	const int32 Pulses = GetPulseCount(Seed, Index, Tuning);
	// Equal slots; each pulse rises at once and decays within its slot, weaker than the one before (x0.65 .. x0.85).
	const float Slot = Duration / Pulses;
	const int32 Pulse = FMath::Min(Pulses - 1, FMath::FloorToInt(SecondsSinceStrike / Slot));
	const float InSlot = (SecondsSinceStrike - Pulse * Slot) / Slot;
	float Peak = 1.f;
	for (int32 Each = 1; Each <= Pulse; ++Each)
	{
		Peak *= FMath::Lerp(0.65f, 0.85f, WeatherRainFxRulesLocal::Fraction(Seed, Index * 8 + Each, 3u));
	}
	// A short bright head then a fast decay (the gap before the next pulse stays dark).
	const float Decay = FMath::Clamp(1.f - InSlot / 0.7f, 0.f, 1.f);
	return Peak * Decay * Decay;
}
