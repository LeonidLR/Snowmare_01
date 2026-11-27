#pragma once

#include "Tactics/CoverTypes.h"

/** Shared planar wall/line decision. Pose reach is separate from protection. */
namespace CoverThreatRules
{
	constexpr double TransitionSeconds = 3.0;

	inline bool MayTransition(double Now, double LastTransition)
	{
		return Now >= LastTransition && Now - LastTransition >= TransitionSeconds;
	}

	inline bool MayOpenShot(bool bPlayerCoverOrder, double Now, double LastTransition)
	{
		return !bPlayerCoverOrder && MayTransition(Now, LastTransition);
	}

	inline bool BlocksLine(const FCoverSlot& Slot, const FVector& Threat)
	{
		const FVector N = Slot.WallNormal.GetSafeNormal2D();
		const double Start = FVector::DotProduct(Slot.WorldLocation - Slot.WallPoint, N);
		const double End = FVector::DotProduct(Threat - Slot.WallPoint, N);
		if (!Slot.IsValid() || N.IsNearlyZero() || Start <= 0.0 || End >= 0.0)
		{
			return false;
		}
		const FVector Crossing = FMath::Lerp(Slot.WorldLocation, Threat, Start / (Start - End));
		const double Along = FVector::DotProduct(Crossing - Slot.WallPoint, Slot.RightTangent());
		return (!Slot.bLeftEdgeExposed || Along >= -Slot.LeftEdgeDistanceCm)
			&& (!Slot.bRightEdgeExposed || Along <= Slot.RightEdgeDistanceCm);
	}

	inline float Quality(const FCoverSlot& Slot, const FVector& Threat)
	{
		return BlocksLine(Slot, Threat)
			? FMath::Clamp(static_cast<float>(FVector::DotProduct(-Slot.WallNormal.GetSafeNormal2D(),
				(Threat - Slot.WorldLocation).GetSafeNormal2D())), 0.f, 1.f) : 0.f;
	}
}
