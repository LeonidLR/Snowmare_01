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

	FVector TwistedDirection(const FVector& BaseDirection, float YawDeg)
	{
		const FVector Base = BaseDirection.GetSafeNormal2D();
		return Base.IsNearlyZero() ? Base : FRotator(0.f, Base.Rotation().Yaw + YawDeg, 0.f).Vector().GetSafeNormal2D();
	}

	bool IsInCone(const FPeekSightConfig& Config, const FVector& Eye, const FVector& Direction, const FVector& Target)
	{
		return Config.bEnabled && TrapSpotRules::IsInPeekSightCone(Eye, Direction, Target, Config.RangeCm, Config.HalfAngleDeg);
	}
}
