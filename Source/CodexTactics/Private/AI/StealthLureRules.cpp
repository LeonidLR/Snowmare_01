#include "AI/StealthLureRules.h"

#include "AI/PerceptionRules.h"
#include "Combat/GrenadeRules.h"

#define LOCTEXT_NAMESPACE "StealthLureRules"

float StealthLureRules::GetRadius(const FStealthLureParams& Params, EStealthLureKind Kind)
{
	return FMath::Max(Kind == EStealthLureKind::Whistle ? Params.WhistleRadiusCm : Params.ThrowRadiusCm, 0.f);
}

float StealthLureRules::GetCooldown(const FStealthLureParams& Params, EStealthLureKind Kind)
{
	return FMath::Max(Kind == EStealthLureKind::Whistle ? Params.WhistleCooldownSeconds : Params.ThrowCooldownSeconds, 0.f);
}

float StealthLureRules::GetThrowRange(const FStealthLureParams& Params, EOperativeStance Stance)
{
	return GrenadeRules::EffectiveRange(FMath::Max(Params.ThrowRangeCm, 0.f), Stance);
}

float StealthLureRules::CooldownLeft(double ReadyAtSeconds, double NowSeconds)
{
	return static_cast<float>(FMath::Max(ReadyAtSeconds - NowSeconds, 0.0));
}

bool StealthLureRules::IsExploring(ECodexGamePhase Phase, ECodexCombatMode CombatMode)
{
	return Phase == ECodexGamePhase::Exploration && CombatMode == ECodexCombatMode::None;
}

EStealthLureRefusal StealthLureRules::CheckAvailability(const FStealthLureGate& Gate)
{
	if (!Gate.bOperativeAble)
	{
		return EStealthLureRefusal::Unable;
	}
	if (!IsExploring(Gate.Phase, Gate.CombatMode))
	{
		return EStealthLureRefusal::Combat;
	}
	if (Gate.bSquadDetected)
	{
		return EStealthLureRefusal::Detected;
	}
	if (Gate.CooldownLeftSeconds > 0.f)
	{
		return EStealthLureRefusal::Cooldown;
	}
	return EStealthLureRefusal::None;
}

bool StealthLureRules::IsHuntingSquad(bool bAlive, bool bOnPatrol, bool bHasTarget, bool bHasAlarmPoint, bool bKnowsSquadPosition)
{
	return bAlive && !bOnPatrol && (bHasTarget || bHasAlarmPoint || bKnowsSquadPosition);
}

float StealthLureRules::EffectiveRadius(const FStealthLureListener& Listener, float RadiusCm, float SearchPerceptionMultiplier)
{
	return PerceptionRules::OccludedRadius(OpenRadius(Listener, RadiusCm, SearchPerceptionMultiplier), Listener.Walls, Listener.OcclusionPerWall);
}

float StealthLureRules::OpenRadius(const FStealthLureListener& Listener, float RadiusCm, float SearchPerceptionMultiplier)
{
	return FMath::Max(RadiusCm, 0.f) * (Listener.bSearching ? FMath::Max(SearchPerceptionMultiplier, 0.f) : 1.f);
}

bool StealthLureRules::HearsAlongPath(const FStealthLureListener& Listener, float RadiusCm, float SearchPerceptionMultiplier)
{
	if (!Listener.bAlive || !Listener.bOnPatrol || Listener.bTurnBasedHeld || Listener.PathCm < 0.f)
	{
		return false;
	}
	const float Open = OpenRadius(Listener, RadiusCm, SearchPerceptionMultiplier);
	return Open > 0.f && Listener.PathCm <= Open;
}

bool StealthLureRules::Hears(const FStealthLureListener& Listener, float RadiusCm, float SearchPerceptionMultiplier)
{
	if (!Listener.bAlive || !Listener.bOnPatrol || Listener.bTurnBasedHeld || Listener.DistanceCm < 0.f)
	{
		return false;
	}
	const float Radius = EffectiveRadius(Listener, RadiusCm, SearchPerceptionMultiplier);
	return (Radius > 0.f && Listener.DistanceCm <= Radius) || HearsAlongPath(Listener, RadiusCm, SearchPerceptionMultiplier);
}

ENoiseReaction StealthLureRules::ReactToNoise(const FNoiseReactionInput& Input)
{
	if (Input.bFollowingScent && Input.ScentDistanceCm >= 0.f && Input.ScentDistanceCm <= ScentHoldsNoiseCm)
	{
		return ENoiseReaction::KeepScent;
	}
	if (Input.bSearching && Input.DistanceToCurrentNoiseCm <= SameNoiseCm && Input.SecondsSinceCurrentNoise >= 0.f
		&& Input.SecondsSinceCurrentNoise < SameNoiseSeconds)
	{
		return ENoiseReaction::SameNoise;
	}
	return Input.bSearching ? ENoiseReaction::Retarget : ENoiseReaction::Investigate;
}

bool StealthLureRules::GoesToNoise(ENoiseReaction Reaction)
{
	return Reaction == ENoiseReaction::Investigate || Reaction == ENoiseReaction::Retarget;
}

float StealthLureRules::GetApproachSpeed(float PatrolWalkSpeed, float NormalSpeed, float Fraction, float TrapSearchSpeed)
{
	if (Fraction <= 0.f)
	{
		return TrapSearchSpeed;
	}
	const float Floor = FMath::Min(FMath::Max(PatrolWalkSpeed, 0.f), FMath::Max(NormalSpeed, 0.f));
	return FMath::Max(FMath::Max(NormalSpeed, 0.f) * Fraction, Floor);
}

FVector StealthLureRules::ChooseInvestigatePoint(const FVector& Noise, bool bOnNavmesh, const FVector& OnNavmesh, ELureGoalPath Path, const FVector& PathEnd)
{
	if (!bOnNavmesh)
	{
		return Noise;
	}
	switch (Path)
	{
	case ELureGoalPath::Partial:
		return PathEnd;
	case ELureGoalPath::Full:
	case ELureGoalPath::None:
	default:
		return OnNavmesh;
	}
}

float StealthLureRules::GetArriveRadius(float SearchArriveCm, bool bLureFirstLeg)
{
	return bLureFirstLeg ? FMath::Max(SearchArriveCm, LureArriveCm) : SearchArriveCm;
}

bool StealthLureRules::HasArrived(float DistanceCm, float ArriveCm, float StuckSeconds)
{
	if (DistanceCm <= ArriveCm)
	{
		return true;
	}
	return StuckSeconds >= ArriveStuckSeconds && DistanceCm <= ArriveCm + ArriveStuckSlackCm;
}

FText StealthLureRules::GetButtonName(EStealthLureKind Kind)
{
	return Kind == EStealthLureKind::Whistle ? LOCTEXT("Whistle", "WHISTLE") : LOCTEXT("Throw", "THROW");
}

FText StealthLureRules::GetButtonLabel(EStealthLureKind Kind, EStealthLureRefusal Refusal, float CooldownLeftSeconds, const FString& KeyLabel)
{
	const FText Name = GetButtonName(Kind);
	switch (Refusal)
	{
	case EStealthLureRefusal::None:
		return FText::Format(LOCTEXT("LabelReady", "{0}\n[{1}]"), Name, FText::FromString(KeyLabel));
	case EStealthLureRefusal::Cooldown:
		return FText::Format(LOCTEXT("LabelCooldown", "{0}\n{1}s"), Name, FMath::CeilToInt(CooldownLeftSeconds));
	default:
		return FText::Format(LOCTEXT("LabelOff", "{0}\n--"), Name);
	}
}

FText StealthLureRules::GetRefusalText(EStealthLureKind Kind, EStealthLureRefusal Refusal, float CooldownLeftSeconds)
{
	switch (Refusal)
	{
	case EStealthLureRefusal::Unable:
		return LOCTEXT("RefuseUnable", "This operative can't do that right now.");
	case EStealthLureRefusal::Combat:
		return LOCTEXT("RefuseCombat", "Only while exploring - useless once the fight is on.");
	case EStealthLureRefusal::Detected:
		return LOCTEXT("RefuseDetected", "The squad has been spotted - nobody will fall for it now.");
	case EStealthLureRefusal::Cooldown:
		return FText::Format(Kind == EStealthLureKind::Whistle ? LOCTEXT("RefuseCooldownWhistle", "Whistle again in {0} s.")
			: LOCTEXT("RefuseCooldownThrow", "Another throw in {0} s."), FMath::CeilToInt(CooldownLeftSeconds));
	default:
		return FText::GetEmpty();
	}
}

FText StealthLureRules::GetFeedLine(EStealthLureKind Kind)
{
	return Kind == EStealthLureKind::Whistle ? LOCTEXT("FeedWhistle", "Whistle.") : LOCTEXT("FeedThrow", "Stone thrown.");
}

#undef LOCTEXT_NAMESPACE
