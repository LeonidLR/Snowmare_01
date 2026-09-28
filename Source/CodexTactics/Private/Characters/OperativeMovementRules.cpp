#include "Characters/OperativeMovementRules.h"

namespace OperativeMovementRules
{
	float GetStanceSpeedMultiplier(const FOperativeMovementConfig& Config, EOperativeStance Stance)
	{
		switch (Stance)
		{
		case EOperativeStance::Crouching:
			return Config.WalkSpeed > 0.f ? Config.CrouchSpeed / Config.WalkSpeed : 0.f;
		case EOperativeStance::Prone:
			return Config.ProneSpeedMultiplier;
		default:
			return 1.f;
		}
	}

	bool CanSprint(const FOperativeMovementConfig& Config, EOperativeStance Stance, float ColdPercent, bool bWounded)
	{
		return Stance != EOperativeStance::Prone && ColdPercent < Config.MaxColdToSprint && !bWounded;
	}

	float ComputeMaxSpeed(const FOperativeMovementConfig& Config, EOperativeStance Stance, bool bSprinting, bool bWounded, bool bCarrying)
	{
		float Speed = Config.WalkSpeed * GetStanceSpeedMultiplier(Config, Stance);
		if (bSprinting && Stance == EOperativeStance::Standing && Config.WalkSpeed > 0.f)
		{
			Speed *= Config.RunSpeed / Config.WalkSpeed;
		}
		if (bWounded)
		{
			Speed *= Config.WoundedSpeedMultiplier;
		}
		if (bCarrying)
		{
			Speed *= Config.CarryingSpeedMultiplier;
		}
		return Speed;
	}

	float GetTurnRate(const FOperativeMovementConfig& Config, EOperativeStance Stance)
	{
		switch (Stance)
		{
		case EOperativeStance::Crouching:
			return Config.TurnRateCrouching;
		case EOperativeStance::Prone:
			return Config.TurnRateProne;
		default:
			return Config.TurnRateStanding;
		}
	}
}
