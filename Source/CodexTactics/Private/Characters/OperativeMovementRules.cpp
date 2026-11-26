#include "Characters/OperativeMovementRules.h"

namespace OperativeMovementRules
{
	bool HasNoWayUp(float FeetZ, float GoalZ, float CapsuleHalfHeightCm, bool bHasPath, bool bPartial, float EndZ)
	{
		constexpr float LevelSlackCm = 80.f; // stairs / navmesh height noise are not a different roof
		// Some existing move APIs supply the capsule centre (LowCoverMoveSmoke), not feet.
		const float UpperGoalCm = FMath::Max(LevelSlackCm, CapsuleHalfHeightCm + 20.f);
		return GoalZ > FeetZ + UpperGoalCm && (!bHasPath || bPartial || EndZ < GoalZ - UpperGoalCm);
	}

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

	float ComputeMoveHold(float SettleSecondsLeft, float PlayingClipSecondsLeft)
	{
		return FMath::Max3(0.f, SettleSecondsLeft, PlayingClipSecondsLeft);
	}

	bool ShouldDeferMove(float MoveHoldSeconds)
	{
		return MoveHoldSeconds > MoveHoldThreshold;
	}

	bool IsMantlePathAllowed(bool bAutonomousOrder, bool bPlayerTeam, bool bPlayerSquadSolo, bool bSquadMantleEnabled, bool bIsFormationLeader)
	{
		if (bAutonomousOrder)
		{
			return false;
		}
		if (!bPlayerTeam)
		{
			return true;
		}
		return bPlayerSquadSolo || (bSquadMantleEnabled && bIsFormationLeader);
	}

	bool AllowsCrowdOvershootFinish(float DistanceToPathEndCm)
	{
		return DistanceToPathEndCm <= CrowdOvershootTestRangeCm;
	}

	EProneEntry DecideProneEntry(const FOperativeMovementConfig& Config, EOperativeStance Stance, float Speed2D, bool bSprinting,
		bool bHasMoveOrder, bool bBlocked)
	{
		if (!bHasMoveOrder && Speed2D < ProneStillSpeedCm)
		{
			return EProneEntry::InPlace;
		}
		if (bBlocked)
		{
			return EProneEntry::Refused;
		}
		if (Stance == EOperativeStance::Standing)
		{
			if (bSprinting && Speed2D >= Config.ProneDiveMinRunSpeed)
			{
				return EProneEntry::DiveFromRun;
			}
			if (!bSprinting && Speed2D >= Config.ProneDiveMinWalkSpeed)
			{
				return EProneEntry::DiveFromWalk;
			}
		}
		return EProneEntry::StopThenProne;
	}

	float ComputeProneDiveTranslationScale(float EntrySpeed, float ClipEntrySpeed, float MaxMomentumScale, float ClipTravel, float AllowedTravel)
	{
		const float Momentum = ClipEntrySpeed > KINDA_SMALL_NUMBER ? FMath::Clamp(EntrySpeed / ClipEntrySpeed, 1.f, FMath::Max(1.f, MaxMomentumScale)) : 1.f;
		if (ClipTravel <= KINDA_SMALL_NUMBER)
		{
			return Momentum;
		}
		return FMath::Clamp(FMath::Min(Momentum, AllowedTravel / ClipTravel), 0.f, Momentum);
	}

	bool ShouldCrawlOnAfterDive(const FVector& DiveStart, const FVector& DiveEnd, const FVector& Goal, float AcceptanceRadius)
	{
		const FVector ToGoal = FVector(Goal.X - DiveEnd.X, Goal.Y - DiveEnd.Y, 0.f);
		if (ToGoal.Size() <= AcceptanceRadius)
		{
			return false; // arrived
		}
		const FVector Direction = FVector(DiveEnd.X - DiveStart.X, DiveEnd.Y - DiveStart.Y, 0.f).GetSafeNormal();
		return Direction.IsNearlyZero() || FVector::DotProduct(ToGoal, Direction) >= 0.f; // behind him: the dive carried him past it
	}
}
