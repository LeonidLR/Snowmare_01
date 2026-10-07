#include "Characters/LeftHandIKRules.h"

namespace LeftHandIKRules
{
	FTransform GripInHandSpace(const FTransform& WeaponRelativeToHand, const FTransform& SocketInWeapon)
	{
		// Socket (weapon space) -> weapon component -> hand_r space.
		return SocketInWeapon * WeaponRelativeToHand;
	}

	bool WantsIK(const FLeftHandIKState& State)
	{
		return State.bEnabled && State.bHasGrip && State.bWeaponVisible && !State.bReloading && !State.bUpperBodyAction
			&& !State.bVaulting && !State.bDead && !State.bProne && State.bTwoHandedPose;
	}

	FVector SlideIntoReach(const FVector& Target, const FVector& Axis, const FVector& Shoulder, float MaxDistance, float MaxSlide)
	{
		const FVector Dir = Axis.GetSafeNormal();
		if (Dir.IsNearlyZero() || FVector::Dist(Target, Shoulder) <= MaxDistance)
		{
			return Target;
		}
		// |Target - Dir * d - Shoulder| = MaxDistance: d^2 - 2 d (V . Dir) + |V|^2 - R^2 = 0, V = Target - Shoulder; the
		// smallest d >= 0 (the reachable point nearest the original target).
		const FVector V = Target - Shoulder;
		const double B = FVector::DotProduct(V, Dir);
		const double C = V.SizeSquared() - FMath::Square(static_cast<double>(MaxDistance));
		const double Disc = B * B - C;
		double Slide = Disc >= 0.0 ? B - FMath::Sqrt(Disc) : B; // no solution: the point nearest the shoulder
		Slide = FMath::Clamp(Slide, 0.0, static_cast<double>(FMath::Max(MaxSlide, 0.f)));
		return Target - Dir * Slide;
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
