// The squad on a narrow ramp / stair: single file (bug report 2026-11-02 item 7). Godot reference: none (UE-only).

#include "Formation/RampFileRules.h"

#include "Formation/FormationRuntimeRules.h"

namespace RampFileRules
{
	bool IsRampSurface(const FVector& FloorNormal, bool bTerrain, float MinSlopeDeg)
	{
		if (bTerrain || FloorNormal.Z <= 0.0)
		{
			return false;
		}
		const double Up = FloorNormal.GetSafeNormal().Z;
		return Up < FMath::Cos(FMath::DegreesToRadians(static_cast<double>(MinSlopeDeg)));
	}

	FVector2D UphillOf(const FVector& FloorNormal)
	{
		// The surface normal leans away from the uphill side: uphill is against its horizontal part.
		return FVector2D(-FloorNormal.X, -FloorNormal.Y).GetSafeNormal();
	}

	int32 TravelDirection(float FeetZ, float DestinationZ, bool bHasWalk, float LevelChangeCm, bool bOnRamp, float UphillSpeed, float ProgressSpeed)
	{
		if (!bHasWalk)
		{
			return 0;
		}
		const float Rise = DestinationZ - FeetZ;
		if (Rise > LevelChangeCm)
		{
			return 1;
		}
		if (Rise < -LevelChangeCm)
		{
			return -1;
		}
		if (bOnRamp && FMath::Abs(UphillSpeed) >= ProgressSpeed)
		{
			return UphillSpeed > 0.f ? 1 : -1;
		}
		return 0;
	}

	int32 FindWaitBehind(int32 Me, TArrayView<const FRampWalker> Walkers, const FRampFileConfig& Config, bool bMeWaiting)
	{
		if (!Walkers.IsValidIndex(Me) || Walkers[Me].Direction == 0)
		{
			return INDEX_NONE;
		}
		const FRampWalker& Self = Walkers[Me];
		const float Gap = Config.FileGapCm + (bMeWaiting ? Config.ReleaseMarginCm : 0.f);
		int32 Best = INDEX_NONE;
		float BestAhead = TNumericLimits<float>::Max();
		for (int32 Index = 0; Index < Walkers.Num(); ++Index)
		{
			const FRampWalker& Other = Walkers[Index];
			if (Index == Me || Other.Direction != Self.Direction || !Other.bProgressing)
			{
				continue;
			}
			// How far the other one is ahead of him in their walking direction; level with him the earlier priority counts as ahead.
			const float Ahead = (Other.Along - Self.Along) * static_cast<float>(Self.Direction);
			const bool bOtherAhead = Ahead > Config.SideBySideCm || (FMath::Abs(Ahead) <= Config.SideBySideCm && Other.Priority < Self.Priority);
			if (bOtherAhead && Ahead < Gap && Ahead < BestAhead)
			{
				Best = Index;
				BestAhead = Ahead;
			}
		}
		return Best;
	}

	bool IsInMouth(const FVector2D& OffsetFromOnRamp, float HeightDifference, const FVector2D& Uphill, int32 Direction, const FRampFileConfig& Config)
	{
		if (Direction == 0 || Uphill.IsNearlyZero() || OffsetFromOnRamp.Size() > Config.MouthCm || FMath::Abs(HeightDifference) > Config.MouthHeightCm)
		{
			return false;
		}
		const FVector2D Side(-Uphill.Y, Uphill.X);
		const float Lateral = static_cast<float>(FMath::Abs(FVector2D::DotProduct(OffsetFromOnRamp, Side)));
		const float Along = static_cast<float>(FVector2D::DotProduct(OffsetFromOnRamp, Uphill)) * static_cast<float>(Direction);
		return Lateral <= Config.MouthLateralCm && Along < 0.f;
	}

	namespace
	{
		/** +1 / -1: the side of the path P lies on (by its nearest leg), 0 without a leg. */
		int32 SideOfPath(TArrayView<const FVector2D> Path, const FVector2D& P)
		{
			double Best = TNumericLimits<double>::Max();
			int32 Side = 0;
			for (int32 Index = 0; Index + 1 < Path.Num(); ++Index)
			{
				const FVector2D A = Path[Index];
				const FVector2D Leg = Path[Index + 1] - A;
				const double LengthSq = Leg.SizeSquared();
				if (LengthSq < 1.0)
				{
					continue;
				}
				const double T = FMath::Clamp(FVector2D::DotProduct(P - A, Leg) / LengthSq, 0.0, 1.0);
				const double DistSq = FVector2D::DistSquared(P, A + Leg * T);
				if (DistSq < Best)
				{
					Best = DistSq;
					Side = FVector2D::CrossProduct(Leg, P - A) >= 0.0 ? 1 : -1;
				}
			}
			return Side;
		}
	}

	TArray<FVector2D> YieldSpots(TArrayView<const FVector2D> Path, const FVector2D& Here, float CorridorCm, float AheadCm)
	{
		struct FSpot
		{
			FVector2D Point;
			bool bOtherSide = false;
			double Distance = 0.0;
		};
		TArray<FSpot> Spots;
		const int32 HereSide = SideOfPath(Path, Here);
		for (const float Radius : { 130.f, 190.f, 250.f, 310.f })
		{
			for (int32 Step = 0; Step < 16; ++Step)
			{
				const double Angle = FMath::DegreesToRadians(22.5 * Step);
				const FVector2D Point = Here + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;
				if (!FormationRuntime::YieldFromLeaderPath(Path, Point, CorridorCm, FormationRuntime::YieldClearCm, AheadCm).IsEmpty())
				{
					continue; // still in his way
				}
				Spots.Add({ Point, HereSide != 0 && SideOfPath(Path, Point) != HereSide, static_cast<double>(Radius) });
			}
		}
		Spots.StableSort([](const FSpot& A, const FSpot& B) { return A.bOtherSide != B.bOtherSide ? !A.bOtherSide : A.Distance < B.Distance; });
		TArray<FVector2D> Out;
		for (const FSpot& Spot : Spots)
		{
			Out.Add(Spot.Point);
		}
		return Out;
	}

	bool ShouldDropRampSlot(bool bLeaderOnRamp, bool bSlotOnRamp)
	{
		return bSlotOnRamp && !bLeaderOnRamp;
	}

	bool HoldExpired(float HeldSeconds, const FRampFileConfig& Config)
	{
		return HeldSeconds >= Config.MaxHoldSeconds;
	}
}
