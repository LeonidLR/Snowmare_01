#include "Tactics/CoverLookAoRules.h"

namespace CoverLookAoRules
{
	float SignedYawFromNormal(const FVector& WallNormal, const FVector& LookDirection)
	{
		const FVector Normal = WallNormal.GetSafeNormal2D();
		const FVector Look = LookDirection.GetSafeNormal2D();
		if (Normal.IsNearlyZero() || Look.IsNearlyZero())
		{
			return 0.f;
		}
		return FMath::FindDeltaAngleDegrees(Normal.Rotation().Yaw, Look.Rotation().Yaw);
	}

	ECoverFacing SideFromLook(float SignedYawDeg, ECoverFacing Current, float HysteresisDeg)
	{
		const float Band = FMath::Max(HysteresisDeg, 0.f);
		if (Current == ECoverFacing::Right)
		{
			return SignedYawDeg < -Band ? ECoverFacing::Left : ECoverFacing::Right;
		}
		return SignedYawDeg > Band ? ECoverFacing::Right : ECoverFacing::Left;
	}

	void ClampAxes(float& YawDeg, float& PitchDeg, float YawAbsDeg, float PitchAbsDeg)
	{
		const float YawAbs = FMath::Max(0.f, FMath::Abs(YawAbsDeg));
		const float PitchAbs = FMath::Max(0.f, FMath::Abs(PitchAbsDeg));
		YawDeg = FMath::Clamp(YawDeg, -YawAbs, YawAbs);
		PitchDeg = FMath::Clamp(PitchDeg, -PitchAbs, PitchAbs);
	}

	float AxisAbs(float Min, float Max, float Fallback)
	{
		const float Abs = FMath::Max(FMath::Abs(Min), FMath::Abs(Max));
		return Abs > KINDA_SMALL_NUMBER ? Abs : FMath::Max(0.f, Fallback);
	}

	bool WantsPose(bool bInCornerPeek, bool bHasLook)
	{
		return bInCornerPeek && bHasLook;
	}
}
