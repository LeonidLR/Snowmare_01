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

	float FacingYaw(const FCoverSlot& Slot, ECoverFacing /*Side*/)
	{
		// The actor always faces away from the wall (back to it); the M4_Cover_Pack clips already carry the
		// "looking left/right along the wall" pose, the side only picks the _L / _R clip (ClipIndex).
		return Slot.WallNormal.Rotation().Yaw;
	}

	bool IsFacingAligned(float ActorYaw, const FCoverSlot& Slot, ECoverFacing Side, float ToleranceDeg)
	{
		return FMath::Abs(FRotator::NormalizeAxis(ActorYaw - FacingYaw(Slot, Side))) <= ToleranceDeg;
	}

	bool IsShimmyForward(ECoverFacing Facing, float ShimmyDirection, bool bThreatKnown)
	{
		return !bThreatKnown || (Facing == ECoverFacing::Right) == (ShimmyDirection > 0.f);
	}

	ECoverFacing FacingForShimmy(ECoverFacing Current, float ShimmyDirection, bool bThreatKnown)
	{
		if (bThreatKnown || FMath::IsNearlyZero(ShimmyDirection))
		{
			return Current;
		}
		return ShimmyDirection > 0.f ? ECoverFacing::Right : ECoverFacing::Left;
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

	float CornerStandOff(ECoverFacing Facing, bool bCrouched, const FCoverFacingConfig& Config)
	{
		const bool bPackL = ClipIndex(Facing) == 0;
		if (bCrouched)
		{
			return bPackL ? Config.CrouchStandOffPackLCm : Config.CrouchStandOffPackRCm;
		}
		return bPackL ? Config.StandStandOffPackLCm : Config.StandStandOffPackRCm;
	}

	bool ShouldSnapToCorner(const FCoverSlot& Slot, ECoverFacing Facing, float& OutShiftCm, const FCoverFacingConfig& Config, bool bCrouched)
	{
		OutShiftCm = 0.f;
		const float Distance = EdgeDistance(Slot, Facing);
		if (Distance < 0.f || Distance > Config.AutoCornerSnapCm)
		{
			return false;
		}
		const float Shift = Distance - FMath::Max(CornerStandOff(Facing, bCrouched, Config), 0.f);
		if (Shift <= FMath::Max(Config.CornerSnapToleranceCm, 1.f))
		{
			return false; // close enough (or already nearer than the stand-off: never walks away from the edge)
		}
		OutShiftCm = Shift;
		return true;
	}

	float DepthInFrontOfWall(const FCoverSlot& Slot, const FVector& Target)
	{
		const FVector Normal = Slot.WallNormal.GetSafeNormal2D();
		const FVector Delta = Target - Slot.WallPoint;
		return static_cast<float>(FVector::DotProduct(FVector(Delta.X, Delta.Y, 0.f), Normal));
	}

	bool ShouldCornerShot(const FCoverSlot& Slot, const FVector& Target, const FCoverFacingConfig& Config)
	{
		const float Depth = DepthInFrontOfWall(Slot, Target);
		if (Depth <= 0.f)
		{
			return true; // behind the wall face: only round the corner / over the top
		}
		if (Slot.Height != ECoverHeight::HighCover || Depth > Config.CornerShotFrontBandCm)
		{
			return false; // out on the open side
		}
		// Close to the wall's line: around the corner when it lies past the exposed edge on its side.
		const float Along = ThreatAlongWall(Slot, Target);
		const ECoverFacing Side = Along >= 0.f ? ECoverFacing::Right : ECoverFacing::Left;
		const float Edge = EdgeDistance(Slot, Side);
		return Edge >= 0.f && FMath::Abs(Along) >= Edge;
	}

	bool IsFireReady(bool bAtCorner, bool bShimmying, bool bHasThreat, float SecondsSinceThreat, float HoldSeconds)
	{
		return bAtCorner && !bShimmying && bHasThreat && SecondsSinceThreat <= HoldSeconds;
	}

	int32 ShimmyClipIndex(ECoverFacing Facing, bool bForward)
	{
		const int32 Side = ClipIndex(Facing);
		return bForward ? Side : 1 - Side;
	}

	bool IsFiringSpot(ECoverHeight Height, bool bAtCorner)
	{
		return Height == ECoverHeight::LowCover || (Height == ECoverHeight::HighCover && bAtCorner);
	}

	ECoverFacing KeepEngagedFacing(const FCoverSlot& Slot, ECoverFacing Current, ECoverFacing Wanted, bool bEngaged)
	{
		if (!bEngaged || Wanted == Current)
		{
			return Wanted;
		}
		if (Slot.IsEdgeExposed(Current))
		{
			return Current; // engaged at a usable side: hold it (both exposed: the flip waits for the calm)
		}
		return Slot.IsEdgeExposed(Wanted) ? Wanted : Current; // from a closed side to a usable one is fine
	}

	bool IsBeyondFacingEdge(const FCoverSlot& Slot, ECoverFacing Facing, const FVector& Target)
	{
		const float Edge = EdgeDistance(Slot, Facing);
		if (Edge < 0.f)
		{
			return false;
		}
		const float Along = ThreatAlongWall(Slot, Target) * (Facing == ECoverFacing::Right ? 1.f : -1.f);
		return Along >= Edge;
	}

	FVector CornerAimDirection(const FCoverSlot& Slot, ECoverFacing Facing, float OutwardDeg)
	{
		const FVector Along = AlongWallDirection(Slot, Facing).GetSafeNormal2D();
		const FVector Behind = -Slot.WallNormal.GetSafeNormal2D();
		const float Radians = FMath::DegreesToRadians(OutwardDeg);
		return (Along * FMath::Cos(Radians) + Behind * FMath::Sin(Radians)).GetSafeNormal2D();
	}

	bool IsCornerHoldTarget(const FCoverSlot& Slot, ECoverFacing Facing, const FVector& FireOrigin, const FVector& Target,
		float OutwardDeg, float ReachDeg)
	{
		if (!Slot.IsEdgeExposed(Facing))
		{
			return false;
		}
		if (DepthInFrontOfWall(Slot, Target) > 0.f && !IsBeyondFacingEdge(Slot, Facing, Target))
		{
			return false; // in front of the wall, within its span: the open side
		}
		const FVector Aim = CornerAimDirection(Slot, Facing, OutwardDeg);
		const FVector ToTarget = (Target - FireOrigin).GetSafeNormal2D();
		if (ToTarget.IsNearlyZero())
		{
			return false;
		}
		const float Degrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(Aim, ToTarget)), -1.f, 1.f)));
		return Degrees <= ReachDeg;
	}

	int32 ClipIndex(ECoverFacing Facing)
	{
		// Pack _L = his own right as he stands back to the wall (the pack names its sides facing the wall).
		return Facing == ECoverFacing::Right ? 0 : 1;
	}
}
