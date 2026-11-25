#include "Combat/PeekSightRules.h"

#include "Interactables/TrapSpotRules.h"

namespace PeekSightRules
{
	EPeekSightPose DecidePose(ECoverHeight Height, EOperativeStance Stance, bool bCornerAimActive, bool bAtPeekCorner)
	{
		if (Height == ECoverHeight::None || Stance == EOperativeStance::Prone || !bCornerAimActive)
		{
			return EPeekSightPose::None; // out of cover, prone (yesterday's rule: prone behind a wall sees nothing) or hugging
		}
		if (bAtPeekCorner)
		{
			return EPeekSightPose::CornerLean;
		}
		return Height == ECoverHeight::LowCover ? EPeekSightPose::OverTop : EPeekSightPose::None;
	}

	float OverTopEyeHeight(const FPeekSightConfig& Config, float StanceEyeCm, float WallTopCm)
	{
		const float Wanted = FMath::Min(WallTopCm + Config.OverTopClearanceCm, Config.OverTopMaxEyeCm);
		return FMath::Max(StanceEyeCm, Wanted);
	}

	FVector OverTopEyePoint(const FPeekSightConfig& Config, const FVector& Feet, const FVector& WallNormal, float StanceEyeCm, float WallTopCm)
	{
		return Feet - WallNormal.GetSafeNormal2D() * Config.OverTopLeanCm + FVector(0.f, 0.f, OverTopEyeHeight(Config, StanceEyeCm, WallTopCm));
	}

	FVector CornerLeanEyePoint(const FPeekSightConfig& Config, const FVector& LeanPoint, const FVector& WallNormal)
	{
		return LeanPoint + WallNormal.GetSafeNormal2D() * Config.CornerLeanDepthCm;
	}

	FVector TwistedDirection(const FVector& BaseDirection, float YawDeg)
	{
		const FVector Base = BaseDirection.GetSafeNormal2D();
		return Base.IsNearlyZero() ? Base : FRotator(0.f, Base.Rotation().Yaw + YawDeg, 0.f).Vector().GetSafeNormal2D();
	}

	float LookYawDeg(const FVector& Eye, const FVector& BaseDirection, const FVector& LookPoint, float MaxAbsDeg)
	{
		const FVector Base = BaseDirection.GetSafeNormal2D();
		const FVector Delta = (LookPoint - Eye).GetSafeNormal2D();
		if (Base.IsNearlyZero() || Delta.IsNearlyZero())
		{
			return 0.f;
		}
		const float Degrees = FRotator::NormalizeAxis(static_cast<float>(Delta.Rotation().Yaw - Base.Rotation().Yaw));
		const float Limit = FMath::Max(0.f, MaxAbsDeg);
		return FMath::Clamp(Degrees, -Limit, Limit);
	}

	float PeekConeYawDeg(bool bHasLook, float LookYaw, float AppliedAimYawDeg)
	{
		return bHasLook ? LookYaw : AppliedAimYawDeg;
	}

	bool IsInCone(const FPeekSightConfig& Config, const FVector& Eye, const FVector& Direction, const FVector& Target)
	{
		return IsInCone(Config, Eye, Direction, Target, Config.RangeCm);
	}

	bool IsInCone(const FPeekSightConfig& Config, const FVector& Eye, const FVector& Direction, const FVector& Target, float RangeCm)
	{
		return Config.bEnabled && TrapSpotRules::IsInPeekSightCone(Eye, Direction, Target, FMath::Max(RangeCm, 0.f), Config.HalfAngleDeg);
	}

	float EffectiveRangeCm(const FPeekSightConfig& Config, float SightRangeCm)
	{
		if (Config.bRangeFromSight && SightRangeCm >= 0.f)
		{
			return SightRangeCm * FMath::Clamp(Config.SightRangeFraction, 0.f, 1.f);
		}
		return FMath::Max(Config.RangeCm, 0.f);
	}

	bool IsCornerInPeekReach(bool bCornerExposed, float ThreatDistanceCm, float PeekRangeCm)
	{
		return bCornerExposed && ThreatDistanceCm <= PeekRangeCm;
	}
}
