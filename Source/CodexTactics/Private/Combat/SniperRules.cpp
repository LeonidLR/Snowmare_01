#include "Combat/SniperRules.h"

namespace SniperRules
{
	bool CanFireInStance(EOperativeStance Stance)
	{
		return Stance == EOperativeStance::Crouching || Stance == EOperativeStance::Prone;
	}

	bool CanFireNow(EOperativeStance Stance, bool bMoving)
	{
		return CanFireInStance(Stance) && !bMoving;
	}

	EOperativeStance FiringStance(EOperativeStance Current)
	{
		return CanFireInStance(Current) ? Current : EOperativeStance::Crouching;
	}

	ESniperFireStep NextStep(const FSniperFireContext& Context)
	{
		if (Context.bMoving)
		{
			return Context.bDirectOrder ? ESniperFireStep::Stop : ESniperFireStep::WaitForMove;
		}
		if (!CanFireInStance(Context.Stance))
		{
			return ESniperFireStep::Kneel;
		}
		return Context.bStanceTransitionPlaying ? ESniperFireStep::Settle : ESniperFireStep::Ready;
	}

	int32 TurnBasedAttackCost(EOperativeStance Stance, int32 StanceApCost, int32 AttackApCost)
	{
		return FMath::Max(0, AttackApCost) + (CanFireInStance(Stance) ? 0 : FMath::Max(0, StanceApCost));
	}

	bool CanAffordTurnBasedAttack(int32 ApLeft, EOperativeStance Stance, int32 StanceApCost, int32 AttackApCost)
	{
		return ApLeft >= TurnBasedAttackCost(Stance, StanceApCost, AttackApCost);
	}

	EOperativeStance AutonomyStance(EOperativeStance Desired, bool bSniper)
	{
		return bSniper && Desired == EOperativeStance::Standing ? EOperativeStance::Crouching : Desired;
	}

	int32 ClipIndex(EOperativeStance Stance)
	{
		switch (Stance)
		{
		case EOperativeStance::Crouching: return 1;
		case EOperativeStance::Prone: return 2;
		default: return 0;
		}
	}

	bool ShouldCycleBolt(int32 RoundsLeftAfterShot)
	{
		return RoundsLeftAfterShot > 0;
	}

	float PlayRateToFit(float ClipSeconds, float Seconds)
	{
		return ClipSeconds > KINDA_SMALL_NUMBER && Seconds > KINDA_SMALL_NUMBER ? ClipSeconds / Seconds : 1.f;
	}
}
