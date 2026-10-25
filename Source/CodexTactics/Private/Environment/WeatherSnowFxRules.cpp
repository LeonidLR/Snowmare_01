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

	// The look does not depend on the intensity (a min above its max is taken as one value).
	Params.FlakeSizeMin = FMath::Max(Tuning.FlakeSizeMin, 0.f);
	Params.FlakeSizeMax = FMath::Max(Tuning.FlakeSizeMax, Params.FlakeSizeMin);
	Params.FlakeOpacityRandomMin = FMath::Max(Tuning.FlakeOpacityRandomMin, 0.f);
	Params.LifetimeMin = FMath::Max(Tuning.LifetimeMin, 0.1f);
	Params.LifetimeMax = FMath::Max(Tuning.LifetimeMax, Params.LifetimeMin);
	Params.Gravity = FVector(0., 0., -FMath::Max(Tuning.FallAcceleration, 0.f));
	Params.SpeedLimit = FMath::Max(Tuning.SpeedLimit, 10.f);
	Params.FlakeWindSpeed = FMath::Max(Tuning.FlakeWindSpeed, 0.f);
	Params.SpawnRadius = FMath::Max(Tuning.SpawnRadius, 100.f);
	Params.SpawnHeight = FMath::Max(Tuning.SpawnHeight, 100.f);
	Params.DriftOpacityRandomMin = FMath::Max(Tuning.DriftOpacityRandomMin, 0.f);
	Params.DriftWindSpeed = FMath::Max(Tuning.DriftWindSpeed, 0.f);
	Params.DriftRadius = FMath::Max(Tuning.DriftRadius, 100.f);
	Params.DriftHeight = FMath::Max(Tuning.DriftHeight, 10.f);
	if (I < Tuning.OffThreshold)
	{
		return Params;
	}
	Params.SpawnRate = Tuning.SpawnRateAtFull * FMath::Pow(I, FMath::Max(Tuning.SpawnRateExponent, 0.1f));
	Params.Opacity = FMath::Lerp(Tuning.OpacityMin, Tuning.OpacityMax, I);
	const float DriftSpan = FMath::Max(1.f - Tuning.DriftStartIntensity, KINDA_SMALL_NUMBER);
	const float Drift = Tuning.bDriftEnabled ? FMath::Clamp((I - Tuning.DriftStartIntensity) / DriftSpan, 0.f, 1.f) : 0.f;
	Params.DriftOpacity = Tuning.DriftOpacityMax * Drift;
	Params.DriftSpriteSize = FMath::Lerp(Tuning.DriftSpriteSizeMin, Tuning.DriftSpriteSizeMax, Drift);
	Params.DriftRate = Tuning.DriftRateAtFull * Drift;
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

FBox WeatherSnowFxRules::MakeZoneKillBox(const FTransform& ZoneTransform, const FVector& ZoneHalfExtent, float MarginCm, float BelowCm)
{
	const FVector Half = ZoneHalfExtent.GetAbs();
	if (Half.X <= 0. || Half.Y <= 0. || Half.Z <= 0.)
	{
		return FBox(ForceInit);
	}
	FBox Box(ForceInit);
	for (int32 Corner = 0; Corner < 8; ++Corner)
	{
		const FVector Local((Corner & 1) ? Half.X : -Half.X, (Corner & 2) ? Half.Y : -Half.Y, (Corner & 4) ? Half.Z : -Half.Z);
		Box += ZoneTransform.TransformPosition(Local);
	}
	const double Margin = FMath::Max(MarginCm, 0.f);
	Box.Min -= FVector(Margin, Margin, FMath::Max(BelowCm, 0.f));
	Box.Max += FVector(Margin, Margin, 0.);
	return Box;
}

FBox WeatherSnowFxRules::MakeRoofKillBox(const FBox& RoofBounds, double FloorZ, float MarginCm, float BelowCm)
{
	if (!RoofBounds.IsValid || RoofBounds.Max.Z <= FloorZ)
	{
		return FBox(ForceInit);
	}
	const double Margin = FMath::Max(MarginCm, 0.f);
	return FBox(FVector(RoofBounds.Min.X - Margin, RoofBounds.Min.Y - Margin, FloorZ - FMath::Max(BelowCm, 0.f)),
		FVector(RoofBounds.Max.X + Margin, RoofBounds.Max.Y + Margin, RoofBounds.Max.Z));
}

bool WeatherSnowFxRules::IsRoofCandidate(const FBox& Bounds, double HitZ, double FromZ, const FWeatherSnowFxTuning& Tuning)
{
	if (!Bounds.IsValid || HitZ <= FromZ || HitZ - FromZ > Tuning.RoofTraceHeight)
	{
		return false;
	}
	const FVector Size = Bounds.GetSize();
	return FMath::Max(Size.X, Size.Y) <= Tuning.RoofMaxSizeCm;
}

double WeatherSnowFxRules::DistanceSq2D(const FVector& Point, const FBox& Box)
{
	const double Dx = FMath::Max3(Box.Min.X - Point.X, 0., Point.X - Box.Max.X);
	const double Dy = FMath::Max3(Box.Min.Y - Point.Y, 0., Point.Y - Box.Max.Y);
	return Dx * Dx + Dy * Dy;
}

TArray<FBox> WeatherSnowFxRules::SelectKillBoxes(const TArray<FBox>& Candidates, const FVector& Focus, double Radius, int32 MaxCount)
{
	TArray<TPair<double, int32>> Order;
	const double RadiusSq = FMath::Square(FMath::Max(Radius, 0.));
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const FBox& Box = Candidates[Index];
		if (!Box.IsValid)
		{
			continue;
		}
		const double DistanceSq = DistanceSq2D(Focus, Box);
		if (DistanceSq <= RadiusSq)
		{
			Order.Emplace(DistanceSq, Index);
		}
	}
	// Nearest first; equal distances keep the candidates' order.
	Order.StableSort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
	TArray<FBox> Chosen;
	for (const TPair<double, int32>& Entry : Order)
	{
		if (Chosen.Num() >= MaxCount)
		{
			break;
		}
		const FBox& Box = Candidates[Entry.Value];
		if (!Chosen.ContainsByPredicate([&Box](const FBox& Other) { return Other.IsInsideOrOn(Box.Min) && Other.IsInsideOrOn(Box.Max); }))
		{
			Chosen.Add(Box);
		}
	}
	return Chosen;
}

bool WeatherSnowFxRules::IsInsideKillBox(const FVector& Point, const FBox& Box)
{
	return Box.IsValid && Box.IsInside(Point);
}

void WeatherSnowFxRules::PackKillBox(const FBox& Box, FVector& OutOrigin, FVector& OutSize)
{
	if (!Box.IsValid)
	{
		OutOrigin = FVector(0., 0., -1.0e7);
		OutSize = FVector::ZeroVector;
		return;
	}
	OutOrigin = Box.GetCenter();
	OutSize = Box.GetSize();
}
