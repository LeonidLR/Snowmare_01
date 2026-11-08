#include "Combat/FirefightRules.h"

bool FFirefightStreak::AddFirefight(int32 Threshold)
{
	++Count;
	++Total;
	if (Count < FMath::Max(Threshold, 1))
	{
		return false;
	}
	Count = 0;
	++Hordes;
	return true;
}

bool FFirefightStreak::SlipPast()
{
	if (Count <= 0)
	{
		return false;
	}
	Count = 0;
	++SlipResets;
	return true;
}

int32 FFirefightStreak::Remaining(int32 Threshold) const
{
	return FMath::Max(0, FMath::Max(Threshold, 1) - Count);
}

namespace FirefightRules
{
	bool ApplyEvent(FFirefightEncounter& Encounter, EFirefightEvent Event, double Now)
	{
		if (!Encounter.bActive)
		{
			Encounter = FFirefightEncounter();
			Encounter.bActive = true;
			Encounter.StartTime = Now;
		}
		Encounter.LastContactTime = Now;
		switch (Event)
		{
		case EFirefightEvent::EnemyAware:
			Encounter.bEnemyAware = true;
			break;
		case EFirefightEvent::EnemyAttack:
			Encounter.bEnemyAware = true;
			Encounter.bShots = true;
			break;
		case EFirefightEvent::SquadShot:
			Encounter.bShots = true;
			break;
		case EFirefightEvent::AmbushSprung:
			Encounter.bEnemyAware = true;
			Encounter.bShots = true;
			Encounter.bAmbushSprung = true;
			break;
		}
		if (Encounter.bCounted || !Qualifies(Encounter))
		{
			return false;
		}
		Encounter.bCounted = true;
		return true;
	}

	bool Qualifies(const FFirefightEncounter& Encounter)
	{
		return Encounter.bAmbushSprung || (Encounter.bEnemyAware && Encounter.bShots);
	}

	void KeepContact(FFirefightEncounter& Encounter, double Now)
	{
		if (Encounter.bActive)
		{
			Encounter.LastContactTime = FMath::Max(Encounter.LastContactTime, Now);
		}
	}

	EFirefightEnd CheckEnd(const FFirefightEncounter& Encounter, int32 EngagedAlive, double Now, const FFirefightConfig& Config)
	{
		if (!Encounter.bActive)
		{
			return EFirefightEnd::None;
		}
		if (EngagedAlive <= 0)
		{
			return EFirefightEnd::AllDown;
		}
		return Now - Encounter.LastContactTime >= FMath::Max(Config.LostContactSeconds, 0.f) ? EFirefightEnd::LostContact : EFirefightEnd::None;
	}

	float GroupReach(float LargestVisionRangeCm, const FFirefightConfig& Config)
	{
		const float Min = FMath::Max(Config.SlipMinReachCm, 0.f);
		const float Max = FMath::Max(Config.SlipMaxReachCm, Min);
		return FMath::Clamp(LargestVisionRangeCm, Min, Max);
	}

	ESlipVerdict UpdateGroup(FSlipGroupState& State, float DistanceCm, float ReachCm, bool bEngagedNow, bool bGroupGone, const FFirefightConfig& Config)
	{
		if (bEngagedNow)
		{
			State.bEngaged = true;
		}
		if (State.bEngaged)
		{
			State.bInReach = false;
			return ESlipVerdict::None;
		}
		if (bGroupGone)
		{
			const bool bWasInReach = State.bInReach;
			State.bInReach = false;
			return bWasInReach ? ESlipVerdict::SlippedPast : ESlipVerdict::None;
		}
		if (!State.bInReach && DistanceCm <= ReachCm)
		{
			State.bInReach = true;
			return ESlipVerdict::EnteredReach;
		}
		if (State.bInReach && DistanceCm > ReachCm * FMath::Max(Config.SlipExitFactor, 1.f))
		{
			State.bInReach = false;
			return ESlipVerdict::SlippedPast;
		}
		return ESlipVerdict::None;
	}

	bool ShouldHint(int32 Remaining, const FFirefightConfig& Config)
	{
		return Config.HintFirefightsBefore > 0 && Remaining > 0 && Remaining == Config.HintFirefightsBefore;
	}

	int32 ResolveThreshold(int32 LevelValue, int32 ConsoleValue)
	{
		return ConsoleValue >= 1 ? ConsoleValue : FMath::Max(LevelValue, 1);
	}
}
