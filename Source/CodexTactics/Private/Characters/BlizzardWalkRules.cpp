#include "Characters/BlizzardWalkRules.h"

namespace BlizzardWalkRules
{
	float GetBlizzardAmount(EWeatherState Weather, EWeatherState NextWeather, float TransitionBlend)
	{
		const float Blend = FMath::Clamp(TransitionBlend, 0.f, 1.f);
		const float Amount = (Weather == EWeatherState::Blizzard ? 1.f - Blend : 0.f) + (NextWeather == EWeatherState::Blizzard ? Blend : 0.f);
		return FMath::Clamp(Amount, 0.f, 1.f);
	}

	EBlizzardWalkBlock GetBlock(const FBlizzardWalkInput& Input, const FBlizzardWalkTuning& Tuning, bool bActive)
	{
		if (Input.BlizzardAmount < (bActive ? Tuning.ExitAmount : Tuning.EnterAmount) || Input.BlizzardAmount <= 0.f)
		{
			return EBlizzardWalkBlock::NoBlizzard;
		}
		if (Input.bSheltered)
		{
			return EBlizzardWalkBlock::Sheltered;
		}
		if (Input.bDead || Input.bKnockedDown)
		{
			return EBlizzardWalkBlock::Down;
		}
		if (Input.Stance != EOperativeStance::Standing)
		{
			return EBlizzardWalkBlock::Stance;
		}
		if (Input.Speed <= (bActive ? Tuning.StopSpeed : Tuning.StartSpeed))
		{
			return EBlizzardWalkBlock::NotWalking;
		}
		if (Input.bSprinting || Input.Speed > Tuning.MaxSpeed)
		{
			return EBlizzardWalkBlock::TooFast;
		}
		if (Input.bAiming || Input.bHasAimTarget || Input.bReloading)
		{
			return EBlizzardWalkBlock::Aiming;
		}
		if (Input.bInCover || Input.bVaulting || Input.bCarrying)
		{
			return EBlizzardWalkBlock::Busy;
		}
		if (Input.bOtherUpperBodyClip || Input.bFullBodyClip)
		{
			return EBlizzardWalkBlock::OtherClip;
		}
		return EBlizzardWalkBlock::None;
	}

	float GetTargetWeight(const FBlizzardWalkInput& Input, const FBlizzardWalkTuning& Tuning, bool bActive)
	{
		return GetBlock(Input, Tuning, bActive) == EBlizzardWalkBlock::None ? FMath::Clamp(Input.BlizzardAmount, 0.f, 1.f) : 0.f;
	}

	const TCHAR* BlockName(EBlizzardWalkBlock Block)
	{
		switch (Block)
		{
		case EBlizzardWalkBlock::None: return TEXT("none");
		case EBlizzardWalkBlock::NoBlizzard: return TEXT("no blizzard");
		case EBlizzardWalkBlock::Sheltered: return TEXT("sheltered");
		case EBlizzardWalkBlock::Down: return TEXT("down");
		case EBlizzardWalkBlock::Stance: return TEXT("stance");
		case EBlizzardWalkBlock::NotWalking: return TEXT("not walking");
		case EBlizzardWalkBlock::TooFast: return TEXT("too fast");
		case EBlizzardWalkBlock::Aiming: return TEXT("aiming");
		case EBlizzardWalkBlock::Busy: return TEXT("busy");
		case EBlizzardWalkBlock::OtherClip: return TEXT("other clip");
		default: return TEXT("?");
		}
	}
}
