// FormationCornerHold: the two-corner hold at a wall (#57, user request 2026-11-07) - which squad peek order is the second corner, and the
// middle men's sweep between the two corner looks; the game designer's answers 2026-11-08 (the middle men's places between the corners, the
// leader chain). Godot reference: none (UE-only; docs/systems/squad-and-orders.md "Both corners held").

#include "Formation/FormationCornerHoldRules.h"

namespace FormationCornerHold
{
	EPeekOrder DecidePeekOrder(const FPeekOrderFacts& Facts, double SameEnd)
	{
		if (!Facts.bEnabled || !Facts.bPeekerAtCorner || !Facts.bSameWall)
		{
			return EPeekOrder::Ordinary;
		}
		if (Facts.ToPeekEdgeCm <= SameEnd || (Facts.bHeldCorner && Facts.ToHeldEdgeCm <= SameEnd))
		{
			return EPeekOrder::AlreadyHeld;
		}
		// Both corners held already: a third end of that wall is a new peek of its own.
		return Facts.bHeldCorner ? EPeekOrder::Ordinary : EPeekOrder::SecondCorner;
	}

	float SweepFraction(float Seconds, float SweepSeconds, int32 Index, int32 Count)
	{
		const int32 Num = FMath::Max(Count, 1);
		const int32 Slice = FMath::Clamp(Index, 0, Num - 1);
		float Eased = 0.5f;
		if (SweepSeconds > KINDA_SMALL_NUMBER)
		{
			// One cycle = there and back (2 x SweepSeconds); the triangle 0 -> 1 -> 0, eased at the ends.
			const float Cycle = FMath::Frac(FMath::Max(Seconds, 0.f) / (2.f * SweepSeconds));
			const float Triangle = Cycle < 0.5f ? 2.f * Cycle : 2.f - 2.f * Cycle;
			Eased = 0.5f - 0.5f * FMath::Cos(UE_PI * Triangle);
		}
		return (static_cast<float>(Slice) + Eased) / static_cast<float>(Num);
	}

	float SweepYaw(float FromYaw, float ToYaw, float OutwardYaw, float Fraction)
	{
		const float From = FMath::FindDeltaAngleDegrees(OutwardYaw, FromYaw);
		const float To = FMath::FindDeltaAngleDegrees(OutwardYaw, ToYaw);
		return FRotator::NormalizeAxis(OutwardYaw + FMath::Lerp(From, To, FMath::Clamp(Fraction, 0.f, 1.f)));
	}

	float ClampOffOutward(float Yaw, float OutwardYaw, float MaxOffDeg)
	{
		const float Off = FMath::Clamp(FMath::FindDeltaAngleDegrees(OutwardYaw, Yaw), -FMath::Abs(MaxOffDeg), FMath::Abs(MaxOffDeg));
		return FRotator::NormalizeAxis(OutwardYaw + Off);
	}

	bool StillHolds(bool bAlive, bool bKnockedDown, bool bInCoverAtThatWall, bool bPeeking)
	{
		return bAlive && !bKnockedDown && bInCoverAtThatWall && bPeeking;
	}

	TArray<FVector2D> MiddlePlaces(const FVector2D& CornerA, const FVector2D& CornerB, const FVector2D& LinePoint, float NormalYaw, int32 Count,
		double MinSpacingCm)
	{
		TArray<FVector2D> Places;
		if (Count < 1)
		{
			return Places;
		}
		// The wall line's direction: perpendicular to its outward normal, pointing from A towards B.
		const double Rad = FMath::DegreesToRadians(static_cast<double>(NormalYaw));
		FVector2D Along(-FMath::Sin(Rad), FMath::Cos(Rad));
		if (FVector2D::DotProduct(CornerB - CornerA, Along) < 0.0)
		{
			Along = -Along;
		}
		const double FromA = FVector2D::DotProduct(CornerA - LinePoint, Along);
		const double FromB = FVector2D::DotProduct(CornerB - LinePoint, Along);
		const double Gap = FromB - FromA;
		if (Gap <= UE_KINDA_SMALL_NUMBER || Gap < static_cast<double>(Count + 1) * FMath::Max(MinSpacingCm, 0.0))
		{
			return Places; // too short for everyone: the caller keeps today's places
		}
		Places.Reserve(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const double Fraction = static_cast<double>(Index + 1) / static_cast<double>(Count + 1);
			Places.Add(LinePoint + Along * (FromA + Gap * Fraction));
		}
		return Places;
	}

	TArray<int32> OrderAlong(const TArray<FVector2D>& Positions, const FVector2D& CornerA, const FVector2D& CornerB)
	{
		TArray<int32> Order;
		Order.Reserve(Positions.Num());
		for (int32 Index = 0; Index < Positions.Num(); ++Index)
		{
			Order.Add(Index);
		}
		const FVector2D Dir = (CornerB - CornerA).GetSafeNormal();
		Order.StableSort([&Positions, &CornerA, &Dir](const int32 X, const int32 Y)
		{
			return FVector2D::DotProduct(Positions[X] - CornerA, Dir) < FVector2D::DotProduct(Positions[Y] - CornerA, Dir);
		});
		return Order;
	}

	bool IsSweepAlert(const FSweepAlertFacts& Facts)
	{
		return Facts.bThreatKnown || Facts.bUnderFire || Facts.bSuppressed;
	}

	float SweepSecondsFor(const FSweepAlertFacts& Facts, float CalmSeconds, float AlertSeconds)
	{
		const float Calm = FMath::Max(CalmSeconds, 0.f);
		const float Alert = FMath::Max(AlertSeconds, 0.f);
		const float Want = IsSweepAlert(Facts) ? Alert : Calm;
		return Want > 0.f ? Want : FMath::Max(Calm, Alert);
	}

	FLeaderChain AfterSecondCorner(int32 Leader, int32 SecondCornerMan)
	{
		FLeaderChain Chain;
		Chain.Leader = SecondCornerMan;
		Chain.Previous = SecondCornerMan != Leader ? Leader : INDEX_NONE;
		return Chain;
	}

	FLeaderAfterFall DecideLeaderAfterFall(const FLeaderFallFacts& Facts)
	{
		FLeaderAfterFall Result;
		if (!Facts.bEnabled || !Facts.bFallenLeadsTheHold)
		{
			return Result; // NotTheHold: the existing rule, as before
		}
		if (!Facts.bPreviousKnown || Facts.bPreviousIsFallen || !Facts.bPreviousAble)
		{
			Result.Pick = ELeaderAfterFall::Fallback;
			return Result;
		}
		Result.Pick = ELeaderAfterFall::PreviousLeader;
		Result.bKeepOtherCorner = Facts.bPreviousHoldsOtherCorner;
		return Result;
	}
}
