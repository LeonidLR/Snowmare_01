// FormationPeekCarry: the squad's corner peek carried over to the next wall (the game designer's play-test 2026-11-01, bug_04 A).
// Godot reference: none (UE-only).

#include "Formation/FormationPeekCarryRules.h"

namespace FormationPeekCarry
{
	bool ShouldCarry(const FCarryOrderFacts& Facts)
	{
		return Facts.bPeekInForce && Facts.bSlotAtCorner && Facts.bWallLayoutOrder && !Facts.bPlanned;
	}

	bool PickCorner(bool bLeftExposed, float LeftCm, bool bRightExposed, float RightCm, ECoverFacing& OutSide, float& OutAlongCm)
	{
		if (!bLeftExposed && !bRightExposed)
		{
			return false;
		}
		const bool bRight = bRightExposed && (!bLeftExposed || FMath::Abs(RightCm) <= FMath::Abs(LeftCm));
		OutSide = bRight ? ECoverFacing::Right : ECoverFacing::Left;
		OutAlongCm = bRight ? FMath::Abs(RightCm) : -FMath::Abs(LeftCm);
		return true;
	}

	float AimAlong(float EdgeAlongCm)
	{
		// In from the corner towards the slot, never past the slot itself (a slot closer to the corner than the inset: the slot).
		const float Inset = FMath::Min(AimInsetCm, FMath::Abs(EdgeAlongCm));
		return EdgeAlongCm - FMath::Sign(EdgeAlongCm) * Inset;
	}

	EPeekCarryStep Decide(const FCarryProgressFacts& Facts)
	{
		if (!Facts.bSameLeader || !Facts.bLeaderOrderUnchanged || !Facts.bWallOrder || Facts.Seconds > MaxSeconds)
		{
			return EPeekCarryStep::Drop;
		}
		if (!Facts.bLeaderMoved && Facts.Seconds < StartGraceSeconds)
		{
			return EPeekCarryStep::Wait; // he has not set off yet (the stand-up clip): standing still is not arriving
		}
		if (Facts.LeaderStillSeconds >= StragglerSeconds || (Facts.bApproachDone && Facts.LeaderStillSeconds >= LeaderStillSeconds))
		{
			return EPeekCarryStep::Fire;
		}
		return EPeekCarryStep::Wait;
	}

	const TCHAR* StepName(EPeekCarryStep Step)
	{
		switch (Step)
		{
		case EPeekCarryStep::Fire: return TEXT("fire");
		case EPeekCarryStep::Drop: return TEXT("drop");
		default: return TEXT("wait");
		}
	}
}
