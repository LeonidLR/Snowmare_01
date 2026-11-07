#include "Combat/UnderFireRules.h"

namespace UnderFireRules
{
	const FUnderFireConfig& GetConfig()
	{
		static const FUnderFireConfig Config;
		return Config;
	}

	bool IsThreatActive(double Now, double LastShotAtTime, float MemorySeconds)
	{
		return Now - LastShotAtTime <= MemorySeconds && Now >= LastShotAtTime;
	}

	bool SharesThreat(bool bIsTarget, float DistanceToTargetCm, float ShareRadiusCm)
	{
		return bIsTarget || DistanceToTargetCm <= ShareRadiusCm;
	}

	bool ShouldFaceThreat(const FFaceFacts& Facts)
	{
		return Facts.bRealTimeFight && Facts.bAlive && Facts.bThreatActive && !Facts.bHasCombatTarget && !Facts.bInCover && !Facts.bSprinting
			&& !Facts.bCarrying && !Facts.bPanicking && !Facts.bFireOrder && !Facts.bExplicitFacing && !Facts.bGridOwnsFacing;
	}

	bool ShouldReturnFire(bool bThreatActive, bool bHasDirectTarget, bool bMayAutoFire, bool bLineToFlash, bool bInReach)
	{
		return bThreatActive && !bHasDirectTarget && bMayAutoFire && bLineToFlash && bInReach;
	}
}
