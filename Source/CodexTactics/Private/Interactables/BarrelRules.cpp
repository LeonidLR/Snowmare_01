#include "Interactables/BarrelRules.h"

EBarrelIgniteResult FBarrelBurnState::TryIgnite(int32& Matches, float BurnDuration)
{
	if (bBurning)
	{
		return EBarrelIgniteResult::AlreadyBurning;
	}
	if (bBurnt)
	{
		return EBarrelIgniteResult::BurntOut;
	}
	if (Matches <= 0)
	{
		return EBarrelIgniteResult::NoMatches;
	}
	--Matches;
	bBurnt = true;
	bBurning = true;
	TimeLeft = BurnDuration;
	return EBarrelIgniteResult::Ignited;
}

bool FBarrelBurnState::Tick(float DeltaSeconds)
{
	if (!bBurning)
	{
		return false;
	}
	TimeLeft -= DeltaSeconds;
	if (TimeLeft <= 0.f)
	{
		bBurning = false;
		TimeLeft = 0.f;
		return true;
	}
	return false;
}

float FBarrelBurnState::GetFireStrength(float FadeSeconds) const
{
	if (!bBurning)
	{
		return 0.f;
	}
	return TimeLeft <= FadeSeconds && FadeSeconds > 0.f ? TimeLeft / FadeSeconds : 1.f;
}
