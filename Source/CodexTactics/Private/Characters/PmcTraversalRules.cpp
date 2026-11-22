#include "Characters/PmcTraversalRules.h"

namespace PmcTraversalRules
{
	float LegTimeoutSeconds(const FPmcTraversalTimeout& Timeout, float WalkCm, float LadderHeightCm, bool bTransition)
	{
		const float Walk = Timeout.WalkSpeedCms > KINDA_SMALL_NUMBER ? FMath::Max(0.f, WalkCm) / Timeout.WalkSpeedCms : 0.f;
		const float Climb = Timeout.LadderClimbCms > KINDA_SMALL_NUMBER ? FMath::Max(0.f, LadderHeightCm) / Timeout.LadderClimbCms : 0.f;
		return FMath::Max(0.f, Timeout.BaseSeconds) + Walk + Climb + (bTransition ? FMath::Max(0.f, Timeout.TransitionSeconds) : 0.f);
	}

	float DistToPolyline2D(const FVector2D& P, TConstArrayView<FVector2D> Points)
	{
		if (Points.IsEmpty())
		{
			return TNumericLimits<float>::Max();
		}
		if (Points.Num() == 1)
		{
			return static_cast<float>(FVector2D::Distance(P, Points[0]));
		}
		double Best = TNumericLimits<double>::Max();
		for (int32 i = 0; i + 1 < Points.Num(); ++i)
		{
			const FVector2D A = Points[i];
			const FVector2D AB = Points[i + 1] - A;
			const double LenSq = AB.SizeSquared();
			const double T = LenSq > KINDA_SMALL_NUMBER ? FMath::Clamp(FVector2D::DotProduct(P - A, AB) / LenSq, 0.0, 1.0) : 0.0;
			Best = FMath::Min(Best, FVector2D::Distance(P, A + AB * T));
		}
		return static_cast<float>(Best);
	}

	bool IsOnStairFlight(const FVector& Feet, TConstArrayView<FVector2D> Flight, float LowZ, float HighZ, float RadiusCm, float BandFraction)
	{
		const float Low = FMath::Min(LowZ, HighZ);
		const float High = FMath::Max(LowZ, HighZ);
		const float Height = High - Low;
		if (Height < 60.f)
		{
			return false;
		}
		const float Band = FMath::Clamp(BandFraction, 0.f, 0.49f) * Height;
		const float Z = static_cast<float>(Feet.Z);
		if (Z < Low + Band || Z > High - Band)
		{
			return false;
		}
		return DistToPolyline2D(FVector2D(Feet.X, Feet.Y), Flight) <= RadiusCm;
	}

	bool HasArrived(const FPmcTraversalArrival& Arrival, float RadiusCm, float ToleranceZCm, float MaxVelocityZ)
	{
		return Arrival.Dist2DCm <= RadiusCm && Arrival.FeetDistZCm <= ToleranceZCm && Arrival.bMovingOnGround &&
			FMath::Abs(Arrival.VelocityZ) < MaxVelocityZ && !Arrival.bBusyTraversing;
	}

	bool MeetsBar(int32 Crossed, int32 Sampled, double BarPercent)
	{
		return Sampled > 0 && RatePercent(Crossed, Sampled) >= BarPercent;
	}

	double RatePercent(int32 Crossed, int32 Sampled)
	{
		return Sampled > 0 ? 100.0 * static_cast<double>(Crossed) / static_cast<double>(Sampled) : 0.0;
	}

	TArray<TPair<FString, int32>> GroupCounts(TConstArrayView<FString> Keys)
	{
		TMap<FString, int32> Counts;
		for (const FString& Key : Keys)
		{
			++Counts.FindOrAdd(Key);
		}
		TArray<TPair<FString, int32>> Out;
		Out.Reserve(Counts.Num());
		for (const TPair<FString, int32>& Pair : Counts)
		{
			Out.Emplace(Pair.Key, Pair.Value);
		}
		Out.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B)
		{
			return A.Value != B.Value ? A.Value > B.Value : A.Key < B.Key;
		});
		return Out;
	}
}
