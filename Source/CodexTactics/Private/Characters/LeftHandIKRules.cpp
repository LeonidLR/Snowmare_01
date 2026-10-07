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
			&& !State.bVaulting && !State.bDead && !State.bProne;
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
