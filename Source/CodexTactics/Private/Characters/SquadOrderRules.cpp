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

	bool ShouldStandToSprintWithLeader(bool bLeaderSprinting, bool bFollowerCrouching, bool bStanceFromSquad, bool bCanSprint)
	{
		return bLeaderSprinting && bFollowerCrouching && bStanceFromSquad && bCanSprint;
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

	bool ShouldJoinGroupCover(const FGroupCoverJoinFacts& Facts)
	{
		if (Facts.bDirectOrder || !Facts.bAlive || Facts.bUnavailable)
		{
			return false;
		}
		if (Facts.bMultiSelection)
		{
			return Facts.bSelected; // the box selection is the group, Solo or not
		}
		return !Facts.bSoloMode && Facts.bFormationActive && Facts.bFollower && !Facts.bOnOwnOrder;
	}

	bool IsGroupCoverOrder(int32 JoiningCount)
	{
		return JoiningCount > 0;
	}

	bool ArmsPostFightHold(ECodexGamePhase Previous, ECodexGamePhase Phase)
	{
		const bool bFromFight = Previous == ECodexGamePhase::WaveCombat || Previous == ECodexGamePhase::WaveCleared
			|| Previous == ECodexGamePhase::PostCombat;
		return Phase == ECodexGamePhase::PostCombat || (Phase == ECodexGamePhase::Exploration && bFromFight);
	}

	bool KeepsPostFightHold(bool bHold, bool bLeaderMoving, bool bRegroup)
	{
		return bHold && !bLeaderMoving && !bRegroup;
	}

	bool IsFormationPhase(ECodexGamePhase Phase, bool bPostCombatFormation)
	{
		return Phase == ECodexGamePhase::Exploration || (bPostCombatFormation && Phase == ECodexGamePhase::PostCombat);
	}
}
