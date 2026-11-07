// FormationCornerHold: the two-corner hold at a wall (#57, user request 2026-11-07) - which squad peek order is the second corner, and the
// middle men's sweep between the two corner looks. Godot reference: none (UE-only; docs/systems/squad-and-orders.md "Both corners held").

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
}
