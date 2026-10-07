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
}
