#include "Characters/SquadOrderRules.h"

namespace SquadOrderRules
{
	bool IsOnOwnOrder(const FOwnOrderFacts& Facts)
	{
		return Facts.bOwnMoveOrder || Facts.bUseOrder || Facts.bObjectTask || Facts.bFireOrder;
	}

	bool IsBoxSelectionAllowed(bool bSoloMode, bool bTurnBasedCombat)
	{
		return bSoloMode && !bTurnBasedCombat;
	}

	bool ShouldStopOnBecomingLeader(bool bOnOwnOrder)
	{
		return !bOnOwnOrder;
	}

	bool ShouldHoldNow(bool bOnOwnOrder)
	{
		return !bOnOwnOrder;
	}

	bool ShouldDeferLeaderSwitch(const FLeaderSwitchFacts& Facts)
	{
		return Facts.bEnabled && Facts.bFormationFollows && Facts.bLeaderCanLead && Facts.bLeaderOrderRunning && !Facts.bSelectedOnOwnOrder;
	}

	EPendingLeaderStep StepPendingLeader(const FPendingLeaderFacts& Facts, float SettleSeconds)
	{
		if (!Facts.bPendingValid)
		{
			return EPendingLeaderStep::Drop;
		}
		if (!Facts.bEnabled || !Facts.bFormationFollows || !Facts.bLeaderCanLead)
		{
			return EPendingLeaderStep::HandOver;
		}
		if (Facts.bPendingOnOwnOrder)
		{
			return EPendingLeaderStep::HandOverForHisOrder;
		}
		if (Facts.bLeaderOrderRunning || Facts.bPendingOnLadder)
		{
			return EPendingLeaderStep::Keep;
		}
		return Facts.bPendingWalking && Facts.SecondsSinceOrderDone < SettleSeconds ? EPendingLeaderStep::Keep : EPendingLeaderStep::HandOver;
	}

	bool EndsOldLeaderOrder(bool bOnLadder, bool bUseOrObjectTask)
	{
		return !bOnLadder && !bUseOrObjectTask;
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

	bool DropsPendingCover(bool bHasPendingCover, bool bRegroup, bool bInCover, bool bPendingOnSameWall)
	{
		if (!bHasPendingCover)
		{
			return false;
		}
		return bRegroup || (bInCover && bPendingOnSameWall);
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

	bool IsFollowPhase(ECodexGamePhase Phase, ECodexCombatMode Mode, bool bPostCombatFormation, bool bCombatFormation)
	{
		if (IsFormationPhase(Phase, bPostCombatFormation))
		{
			return true;
		}
		if (!bCombatFormation)
		{
			return false;
		}
		switch (Phase)
		{
		case ECodexGamePhase::Cutscene:
		case ECodexGamePhase::Preparation:
		case ECodexGamePhase::WaveCleared:
			return true;
		case ECodexGamePhase::WaveCombat:
			return Mode != ECodexCombatMode::TurnBased;
		default:
			return false;
		}
	}

	bool IsGroupCoverPhase(ECodexGamePhase Phase, ECodexCombatMode Mode, bool bPostCombatFormation, bool bCombatFormation, bool bGroupCoverInCombat)
	{
		if (IsFormationPhase(Phase, bPostCombatFormation))
		{
			return true;
		}
		return bGroupCoverInCombat && IsFollowPhase(Phase, Mode, bPostCombatFormation, bCombatFormation);
	}
}
