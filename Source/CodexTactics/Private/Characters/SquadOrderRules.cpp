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
}
