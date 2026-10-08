#include "Characters/AimOffsetRules.h"

namespace AimOffsetRules
{
	float PitchToTarget(const FVector& From, const FVector& To, float MaxAbsDegrees)
	{
		const FVector Delta = To - From;
		const double Horizontal = FMath::Max(Delta.Size2D(), 50.0);
		const double Degrees = FMath::RadiansToDegrees(FMath::Atan2(Delta.Z, Horizontal));
		return static_cast<float>(FMath::Clamp(Degrees, -static_cast<double>(MaxAbsDegrees), static_cast<double>(MaxAbsDegrees)));
	}

	bool WantsAimOffset(const FAimOffsetState& State)
	{
		return State.bEnabled && State.bRangedWeapon && State.bWeaponVisible && !State.bReloading && !State.bUpperBodyAction
			&& !State.bVaulting && !State.bSprinting && !State.bProne && !State.bDead;
	}

	float StepAlpha(float Current, bool bWanted, float DeltaSeconds, float BlendSeconds)
	{
		const float Target = bWanted ? 1.f : 0.f;
		if (BlendSeconds <= KINDA_SMALL_NUMBER)
		{
			return Target;
		}
		return FMath::FInterpConstantTo(Current, Target, DeltaSeconds, 1.f / BlendSeconds);
	}

	float YawToTarget(const FVector& From, const FVector& BaseDirection, const FVector& To, float MaxAbsDegrees)
	{
		const FVector Base = BaseDirection.GetSafeNormal2D();
		const FVector Delta = (To - From).GetSafeNormal2D();
		if (Base.IsNearlyZero() || Delta.IsNearlyZero())
		{
			return 0.f;
		}
		const float Degrees = FRotator::NormalizeAxis(static_cast<float>(Delta.Rotation().Yaw - Base.Rotation().Yaw));
		return FMath::Clamp(Degrees, -MaxAbsDegrees, MaxAbsDegrees);
	}

	float YawTargetWithinReach(float RawDegrees, float ClampDeg, float ReachDeg, float HysteresisDeg, bool& bInOutOfReach)
	{
		const float Abs = FMath::Abs(FRotator::NormalizeAxis(RawDegrees));
		bInOutOfReach = bInOutOfReach ? Abs > ReachDeg - FMath::Max(HysteresisDeg, 0.f) : Abs > ReachDeg;
		return bInOutOfReach ? 0.f : FMath::Clamp(FRotator::NormalizeAxis(RawDegrees), -ClampDeg, ClampDeg);
	}

	float ResidualAimError(float ErrorDegrees, float AppliedYawDegrees)
	{
		return FMath::Abs(FRotator::NormalizeAxis(ErrorDegrees - AppliedYawDegrees));
	}
}
