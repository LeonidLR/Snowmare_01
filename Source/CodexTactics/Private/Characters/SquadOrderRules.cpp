#include "Characters/SquadOrderRules.h"

namespace SquadOrderRules
{
	bool IsOnOwnOrder(const FOwnOrderFacts& Facts)
	{
		return Facts.bOwnMoveOrder || Facts.bUseOrder || Facts.bObjectTask;
	}

	bool ShouldStopOnBecomingLeader(bool bOnOwnOrder)
	{
		return !bOnOwnOrder;
	}

	bool ShouldHoldNow(bool bOnOwnOrder)
	{
		return !bOnOwnOrder;
	}

	bool ShouldLeaveCoverToFollow(const FCoverFollowFacts& Facts, float LeaveDistanceCm)
	{
		if (!Facts.bInCover || Facts.bOnOwnOrder || Facts.bFollowersHolding || Facts.bGuarding || !Facts.bFormationActive)
		{
			return false;
		}
		if (!Facts.bLeaderMoving || Facts.bLeaderTakingCover)
		{
			return false;
		}
		return Facts.DistanceToSlotCm > LeaveDistanceCm;
	}
}
