#include "Tactics/CoverLookAoRules.h"
#include "Tactics/CoverFacingRules.h"

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

	bool IsWithinCornerAoReach(const FCoverSlot& Slot, ECoverFacing LookFacing, const FVector& LookDirection,
		float OutwardDeg, float MaxAoOffDeg, float* OutOffDeg)
	{
		const FVector Look2D = LookDirection.GetSafeNormal2D();
		const FVector Base = CoverFacingRules::CornerAimDirection(Slot, LookFacing, OutwardDeg);
		if (Look2D.IsNearlyZero() || Base.IsNearlyZero())
		{
			if (OutOffDeg) { *OutOffDeg = 0.f; }
			return true;
		}
		const float OffDeg = FMath::Abs(FRotator::NormalizeAxis(Look2D.Rotation().Yaw - Base.Rotation().Yaw));
		if (OutOffDeg) { *OutOffDeg = OffDeg; }
		return OffDeg <= FMath::Max(MaxAoOffDeg, 0.f);
	}
}
