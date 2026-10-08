#include "Combat/TargetedShotRules.h"

#define LOCTEXT_NAMESPACE "TargetedShotRules"

FMineShotChance TargetedShotRules::ComputeMineShotChance(float Accuracy, float ColdLevel, EOperativeStance Stance, float DistanceM)
{
	FMineShotChance Result;
	switch (Stance)
	{
	case EOperativeStance::Crouching:
		Result.StanceMultiplier = 1.f;
		Result.PenaltyPerMeter = 1.8f;
		Result.StanceName = LOCTEXT("Crouching", "Crouched");
		break;
	case EOperativeStance::Prone:
		Result.StanceMultiplier = 1.25f;
		Result.PenaltyPerMeter = 1.f;
		Result.StanceName = LOCTEXT("Prone", "Prone");
		break;
	default:
		Result.StanceMultiplier = 0.6f;
		Result.PenaltyPerMeter = 4.f;
		Result.StanceName = LOCTEXT("Standing", "Standing");
		break;
	}
	const float Effective = FMath::Max(MinEffectiveAccuracy, Accuracy - ColdLevel * ColdAccuracyPenalty);
	Result.Chance = FMath::Clamp(Effective * Result.StanceMultiplier - DistanceM * Result.PenaltyPerMeter, 0.f, MaxMineShotChance);
	return Result;
}

FText TargetedShotRules::GetMineMissReason(EOperativeStance Stance, float DistanceM, float ColdLevel)
{
	if (Stance == EOperativeStance::Standing && DistanceM > StandingMissReasonDistance)
	{
		return LOCTEXT("MissStanding", "can't hit standing at this range");
	}
	if (ColdLevel > ColdMissReasonLevel)
	{
		return LOCTEXT("MissCold", "hands shaking from the cold");
	}
	return LOCTEXT("MissSnow", "the bullet went into the frozen snow");
}

EPlannedShotRetry TargetedShotRules::GetPlannedShotRetry(ECodexGamePhase Phase, ECodexCombatMode Mode, float SecondsSinceRelease)
{
	if (Phase != ECodexGamePhase::WaveCombat || Mode == ECodexCombatMode::TurnBased || Mode == ECodexCombatMode::None)
	{
		return EPlannedShotRetry::GiveUp;
	}
	if (Mode == ECodexCombatMode::TacticalPause)
	{
		return EPlannedShotRetry::Wait;
	}
	return SecondsSinceRelease <= PlannedShotRetrySeconds ? EPlannedShotRetry::Retry : EPlannedShotRetry::GiveUp;
}

#undef LOCTEXT_NAMESPACE
