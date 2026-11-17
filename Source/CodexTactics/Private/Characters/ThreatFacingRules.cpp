#include "Characters/ThreatFacingRules.h"

namespace ThreatFacingRules
{
	bool ShouldTurnToThreat(const FFacts& Facts)
	{
		return Facts.bRealTimeFight && Facts.bStanding && !Facts.bInCover && !Facts.bFacingCombatTarget && !Facts.bExplicitFacing
			&& !Facts.bGridOwnsFacing && Facts.bFoughtThisFight && Facts.bHasThreat;
	}

	bool MayCombatTargetTurnHim(bool bHasOwnFacing, float TargetDistanceCm, float WeaponReachCm)
	{
		return !bHasOwnFacing || TargetDistanceCm <= WeaponReachCm;
	}

	bool ShouldReturnToOrderedFacing(bool bHasFacingOrder, bool bStanding, bool bInCover, bool bTurnedToFight, float QuietSeconds)
	{
		return bHasFacingOrder && bStanding && !bInCover && !bTurnedToFight && QuietSeconds >= ReturnToOrderedFacingDelaySeconds;
	}

	bool PickThreatPoint(bool bLastTargetAlive, const FVector& LastTargetLocation, bool bHasNearest, const FVector& NearestLocation, FVector& OutPoint)
	{
		if (bLastTargetAlive)
		{
			OutPoint = LastTargetLocation;
			return true;
		}
		if (bHasNearest)
		{
			OutPoint = NearestLocation;
			return true;
		}
		return false;
	}
}
