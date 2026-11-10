#include "Characters/ProneCrawlRules.h"

namespace ProneCrawlRules
{
	bool IsOrderedCrawl(const FCrawlFacts& Facts)
	{
		if (!Facts.bAlive || !Facts.bProne || Facts.bInCover)
		{
			return false;
		}
		// His own order counts while it runs (a pause on the path - a repath, a stance clip's hold - is still the order); a formation move only
		// while he really crawls (a follower parked in his slot keeps firing).
		return Facts.bOwnMoveOrder || (Facts.bFormationMove && Facts.Speed2D > FormationCrawlMinSpeedCm);
	}

	bool HoldsFire(const FCrawlFacts& Facts, bool bHoldEnabled)
	{
		return bHoldEnabled && IsOrderedCrawl(Facts);
	}

	ECrawlFacing ChooseFacing(bool bOrderedCrawl, bool bHasCombatTarget, bool bUnderFire, bool bHasRememberedThreat)
	{
		if (bHasCombatTarget)
		{
			return ECrawlFacing::CombatTarget;
		}
		if (bUnderFire)
		{
			return ECrawlFacing::FireFlash;
		}
		return bOrderedCrawl && bHasRememberedThreat ? ECrawlFacing::RememberedThreat : ECrawlFacing::Movement;
	}

	float RelativeMoveDeg(float BodyYawDeg, float MoveYawDeg)
	{
		return FRotator::NormalizeAxis(MoveYawDeg - BodyYawDeg);
	}

	float LayerTarget(bool bEligible, bool bOrderedCrawl, float LocomotionSpeed, float StartSpeed)
	{
		return bEligible && (bOrderedCrawl || LocomotionSpeed > StartSpeed) ? 1.f : 0.f;
	}

	float PhaseRate(float Speed, float CycleCm, float FromCycleCm, float FromShare, float MaxPlayRate, float LengthSeconds)
	{
		const float Share = FMath::Clamp(FromShare, 0.f, 1.f);
		const float Cycle = FMath::Lerp(FMath::Max(0.f, CycleCm), FMath::Max(0.f, FromCycleCm > 0.f ? FromCycleCm : CycleCm), Share);
		if (Cycle <= 1.f || Speed <= 0.f)
		{
			return 0.f;
		}
		return FMath::Min(Speed / Cycle, FMath::Max(0.f, MaxPlayRate) / FMath::Max(0.1f, LengthSeconds));
	}
}
