#include "Characters/FireArcRules.h"

namespace FireArcRules
{
	float StepAimYaw(float CurrentYaw, float TargetYaw, float DeltaSeconds, bool bProne, float AimTurnRateDegPerSec, float ProneTurnRateDegPerSec)
	{
		const FRotator Current(0.f, CurrentYaw, 0.f);
		const FRotator Target(0.f, TargetYaw, 0.f);
		if (bProne)
		{
			return FMath::RInterpConstantTo(Current, Target, DeltaSeconds, FMath::Max(1.f, ProneTurnRateDegPerSec)).Yaw;
		}
		const FRotator Eased = FMath::RInterpTo(Current, Target, DeltaSeconds, AimEaseSpeed);
		const FRotator Constant = FMath::RInterpConstantTo(Current, Target, DeltaSeconds, AimTurnRateDegPerSec);
		const float EasedLeft = FMath::Abs(FRotator::NormalizeAxis(TargetYaw - Eased.Yaw));
		const float ConstantLeft = FMath::Abs(FRotator::NormalizeAxis(TargetYaw - Constant.Yaw));
		return (EasedLeft <= ConstantLeft ? Eased : Constant).Yaw;
	}

	float ShotResidualDeg(float AimResidualDeg, float BodyErrorDeg, bool bProne)
	{
		return bProne ? FMath::Max(AimResidualDeg, FMath::Abs(BodyErrorDeg)) : AimResidualDeg;
	}
}
