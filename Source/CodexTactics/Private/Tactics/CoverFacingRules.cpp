#include "Tactics/CoverFacingRules.h"

namespace CoverFacingRules
{
	float ThreatAlongWall(const FCoverSlot& Slot, const FVector& Threat)
	{
		return static_cast<float>(FVector::DotProduct(Threat - Slot.WorldLocation, Slot.RightTangent()));
	}

	ECoverFacing ResolveThreatSide(const FCoverSlot& Slot, const FVector& Threat, ECoverFacing Current, float HysteresisCm)
	{
		const float Along = ThreatAlongWall(Slot, Threat);
		const float Margin = FMath::Max(HysteresisCm, 0.f);
		if (Current == ECoverFacing::Right)
		{
			return Along < -Margin ? ECoverFacing::Left : ECoverFacing::Right;
		}
		return Along > Margin ? ECoverFacing::Right : ECoverFacing::Left;
	}

	ECoverFacing DefaultSide(const FCoverSlot& Slot, ECoverFacing Current)
	{
		if (Slot.bLeftEdgeExposed && Slot.bRightEdgeExposed)
		{
			if (FMath::IsNearlyEqual(Slot.LeftEdgeDistanceCm, Slot.RightEdgeDistanceCm))
			{
				return Current;
			}
			return Slot.LeftEdgeDistanceCm < Slot.RightEdgeDistanceCm ? ECoverFacing::Left : ECoverFacing::Right;
		}
		if (Slot.bLeftEdgeExposed)
		{
			return ECoverFacing::Left;
		}
		if (Slot.bRightEdgeExposed)
		{
			return ECoverFacing::Right;
		}
		return Current;
	}

	int32 PickThreat(const TArray<FCoverThreatCandidate>& Candidates)
	{
		int32 Best = INDEX_NONE;
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			if (Best == INDEX_NONE)
			{
				Best = Index;
				continue;
			}
			const FCoverThreatCandidate& A = Candidates[Index];
			const FCoverThreatCandidate& B = Candidates[Best];
			if (static_cast<uint8>(A.Source) < static_cast<uint8>(B.Source)
				|| (A.Source == B.Source && A.DistanceCm < B.DistanceCm))
			{
				Best = Index;
			}
		}
		return Best;
	}

	FVector AlongWallDirection(const FCoverSlot& Slot, ECoverFacing Side)
	{
		return Side == ECoverFacing::Right ? Slot.RightTangent() : -Slot.RightTangent();
	}

	float FacingYaw(const FCoverSlot& Slot, ECoverFacing Side)
	{
		return AlongWallDirection(Slot, Side).Rotation().Yaw;
	}

	bool IsFacingAligned(float ActorYaw, const FCoverSlot& Slot, ECoverFacing Side, float ToleranceDeg)
	{
		return FMath::Abs(FRotator::NormalizeAxis(ActorYaw - FacingYaw(Slot, Side))) <= ToleranceDeg;
	}

	bool IsShimmyForward(ECoverFacing Facing, float ShimmyDirection)
	{
		return (Facing == ECoverFacing::Right) == (ShimmyDirection > 0.f);
	}

	float EdgeDistance(const FCoverSlot& Slot, ECoverFacing Side)
	{
		if (!Slot.IsEdgeExposed(Side))
		{
			return -1.f;
		}
		return Side == ECoverFacing::Left ? Slot.LeftEdgeDistanceCm : Slot.RightEdgeDistanceCm;
	}

	bool IsAtCorner(const FCoverSlot& Slot, ECoverFacing Facing, const FCoverFacingConfig& Config)
	{
		const float Distance = EdgeDistance(Slot, Facing);
		return Distance >= 0.f && Distance <= Config.CornerReachCm;
	}

	bool ShouldSnapToCorner(const FCoverSlot& Slot, ECoverFacing Facing, float& OutShiftCm, const FCoverFacingConfig& Config)
	{
		OutShiftCm = 0.f;
		const float Distance = EdgeDistance(Slot, Facing);
		if (Distance < 0.f || Distance <= Config.CornerReachCm || Distance > Config.AutoCornerSnapCm)
		{
			return false;
		}
		OutShiftCm = FMath::Max(Distance - Config.CornerStandOffCm, 0.f);
		return OutShiftCm > 1.f;
	}

	int32 ClipIndex(ECoverFacing Facing)
	{
		return Facing == ECoverFacing::Left ? 0 : 1;
	}
}
