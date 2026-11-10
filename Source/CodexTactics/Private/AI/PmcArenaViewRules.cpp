#include "AI/PmcArenaViewRules.h"

FPmcArenaView PmcArenaViewRules::FrameBox(const TArray<FVector>& Points, const FPmcArenaFraming& Framing)
{
	FPmcArenaView View;
	View.DistanceCm = Framing.MinDistanceCm;
	if (Points.IsEmpty())
	{
		return View;
	}
	const float YawRad = FMath::DegreesToRadians(Framing.YawDeg);
	const FVector Forward(FMath::Cos(YawRad), FMath::Sin(YawRad), 0.f);
	const FVector Right(-Forward.Y, Forward.X, 0.f);
	float MinR = TNumericLimits<float>::Max();
	float MaxR = TNumericLimits<float>::Lowest();
	float MinF = TNumericLimits<float>::Max();
	float MaxF = TNumericLimits<float>::Lowest();
	double SumZ = 0.0;
	for (const FVector& Point : Points)
	{
		const float R = static_cast<float>(FVector::DotProduct(Point, Right));
		const float F = static_cast<float>(FVector::DotProduct(Point, Forward));
		MinR = FMath::Min(MinR, R);
		MaxR = FMath::Max(MaxR, R);
		MinF = FMath::Min(MinF, F);
		MaxF = FMath::Max(MaxF, F);
		SumZ += Point.Z;
	}
	const float MeanZ = static_cast<float>(SumZ / Points.Num());
	const float CentreR = 0.5f * (MinR + MaxR);
	const float CentreF = 0.5f * (MinF + MaxF);
	View.Look = Right * CentreR + Forward * CentreF + FVector(0.f, 0.f, MeanZ);
	// The screen extents round the look point: right = across, forward x sin(pitch) + height x cos(pitch) = up the screen.
	const float Pitch = FMath::DegreesToRadians(FMath::Clamp(Framing.PitchDeg, 10.f, 89.f));
	float HalfWidth = 0.f;
	float HalfHeight = 0.f;
	for (const FVector& Point : Points)
	{
		const FVector Offset = Point - View.Look;
		HalfWidth = FMath::Max(HalfWidth, FMath::Abs(static_cast<float>(FVector::DotProduct(Offset, Right))));
		HalfHeight = FMath::Max(HalfHeight, FMath::Abs(static_cast<float>(FVector::DotProduct(Offset, Forward)) * FMath::Sin(Pitch)
			+ static_cast<float>(Offset.Z) * FMath::Cos(Pitch)));
	}
	HalfWidth += Framing.MarginCm;
	HalfHeight += Framing.MarginCm;
	const float TanHalfH = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(Framing.HorizontalFovDeg, 5.f, 170.f) * 0.5f));
	const float TanHalfV = TanHalfH / FMath::Max(Framing.Aspect, 0.1f);
	const float Distance = FMath::Max(HalfWidth / TanHalfH, HalfHeight / TanHalfV) * FMath::Max(Framing.PerspectiveSlack, 1.f);
	const float MaxDistance = FMath::Max(Framing.MinDistanceCm, Framing.MaxDistanceCm);
	View.DistanceCm = FMath::Clamp(Distance, Framing.MinDistanceCm, MaxDistance);
	if (Distance > MaxDistance)
	{
		// Too wide to fit (a lone flanker far off): centre on the points' mean, so the bulk of the fight stays in the middle and only the
		// outlier leaves the frame (the box centre put both clusters on the frame's edges).
		FVector Mean = FVector::ZeroVector;
		for (const FVector& Point : Points)
		{
			Mean += Point;
		}
		View.Look = Mean / Points.Num();
	}
	return View;
}

TArray<FVector> PmcArenaViewRules::FramedFighters(const TArray<FVector>& Fighters, const TArray<int32>& Sides, bool bHasObjective, const FVector& Objective,
	float IsolationCm)
{
	auto SideOf = [&Sides](int32 Index) { return Sides.IsValidIndex(Index) ? Sides[Index] : 0; };
	TArray<bool> Near;
	Near.SetNumZeroed(Fighters.Num());
	TSet<int32> SidesFramed;
	for (int32 Index = 0; Index < Fighters.Num(); ++Index)
	{
		bool bNear = bHasObjective && FVector::Dist2D(Fighters[Index], Objective) <= IsolationCm;
		for (int32 Other = 0; Other < Fighters.Num() && !bNear; ++Other)
		{
			bNear = Other != Index && FVector::Dist2D(Fighters[Index], Fighters[Other]) <= IsolationCm;
		}
		Near[Index] = bNear;
		if (bNear)
		{
			SidesFramed.Add(SideOf(Index));
		}
	}
	TArray<FVector> Kept;
	for (int32 Index = 0; Index < Fighters.Num(); ++Index)
	{
		// A lone man is kept when nobody of his side is framed otherwise.
		if (Near[Index] || !SidesFramed.Contains(SideOf(Index)))
		{
			Kept.Add(Fighters[Index]);
		}
	}
	return Kept;
}

FVector PmcArenaViewRules::EyeOf(const FPmcArenaView& View, const FPmcArenaFraming& Framing)
{
	const FVector Direction = FRotator(-Framing.PitchDeg, Framing.YawDeg, 0.f).Vector();
	return View.Look - Direction * View.DistanceCm;
}

float PmcArenaViewRules::YawAcross(const FVector& SpawnA, const FVector& SpawnB)
{
	const FVector Axis = (SpawnB - SpawnA).GetSafeNormal2D();
	if (Axis.IsNearlyZero())
	{
		return 0.f;
	}
	// Looking along Axis turned -90 deg: the camera's right (UE: forward turned +90) = Axis, A on the left, B on the right of the frame.
	return FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(Axis.Y), static_cast<float>(Axis.X))) - 90.f;
}

float PmcArenaViewRules::SmoothAlpha(float DeltaSeconds, float HalfLifeSeconds)
{
	if (HalfLifeSeconds <= 0.f)
	{
		return 1.f;
	}
	return FMath::Clamp(1.f - FMath::Pow(2.f, -FMath::Max(DeltaSeconds, 0.f) / HalfLifeSeconds), 0.f, 1.f);
}

FPmcArenaView PmcArenaViewRules::Smooth(const FPmcArenaView& Current, const FPmcArenaView& Target, float DeltaSeconds, float LookHalfLife,
	float ZoomOutHalfLife, float ZoomInHalfLife)
{
	FPmcArenaView Out;
	Out.Look = FMath::Lerp(Current.Look, Target.Look, SmoothAlpha(DeltaSeconds, LookHalfLife));
	const float ZoomHalfLife = Target.DistanceCm > Current.DistanceCm ? ZoomOutHalfLife : ZoomInHalfLife;
	Out.DistanceCm = FMath::Lerp(Current.DistanceCm, Target.DistanceCm, SmoothAlpha(DeltaSeconds, ZoomHalfLife));
	return Out;
}

FLinearColor PmcArenaViewRules::TeamColor(int32 SideIndex)
{
	return SideIndex == 0 ? FLinearColor(1.f, 0.03f, 0.02f, 0.9f) : FLinearColor(0.03f, 1.f, 0.08f, 0.9f);
}

bool PmcArenaViewRules::IsSpawnSpotUsable(bool bReachesGoal, float PathFromSpawnCm, float StraightFromSpawnCm, float DetourFactor, float SlackCm)
{
	if (!bReachesGoal || PathFromSpawnCm < 0.f)
	{
		return false;
	}
	return PathFromSpawnCm <= FMath::Max(StraightFromSpawnCm, 0.f) * DetourFactor + SlackCm;
}
