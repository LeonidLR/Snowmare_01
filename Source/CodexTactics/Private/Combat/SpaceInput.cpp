#include "Combat/SpaceInput.h"

void FSpaceInputTracker::Press()
{
	bPressed = true;
	bHoldFired = false;
	HeldTime = 0.f;
}

ESpaceInputAction FSpaceInputTracker::Release()
{
	const bool bWasTap = bPressed && !bHoldFired;
	bPressed = false;
	bHoldFired = false;
	HeldTime = 0.f;
	return bWasTap ? ESpaceInputAction::Tap : ESpaceInputAction::None;
}

ESpaceInputAction FSpaceInputTracker::Tick(float RealDeltaSeconds, float HoldDuration)
{
	if (!bPressed || bHoldFired)
	{
		return ESpaceInputAction::None;
	}
	HeldTime += RealDeltaSeconds;
	if (HeldTime >= HoldDuration)
	{
		bHoldFired = true;
		return ESpaceInputAction::Hold;
	}
	return ESpaceInputAction::None;
}
