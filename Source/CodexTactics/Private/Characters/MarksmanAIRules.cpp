#include "Characters/MarksmanAIRules.h"

bool MarksmanAIRules::ShouldRetreat(float DistanceCm, float ThresholdCm)
{
	return DistanceCm < ThresholdCm;
}

bool MarksmanAIRules::ShouldFlank(bool bTargetInCover, float CampSeconds, float FlankAfterSeconds)
{
	return bTargetInCover && CampSeconds >= FlankAfterSeconds;
}

EOperativeStance MarksmanAIRules::EvaluateBestStance(bool bLowCover, bool bElevated, bool bMoving)
{
	if (bMoving)
	{
		return EOperativeStance::Standing;
	}
	if (bLowCover && !bElevated)
	{
		return EOperativeStance::Crouching;
	}
	return EOperativeStance::Prone;
}

EMarksmanMove MarksmanAIRules::ChooseMove(const FMarksmanConfig& Config, float DistanceCm, bool bHasLineOfFire)
{
	// Without a line of fire the operatives are no threat (a wall between): he seeks a firing position instead of
	// retreating from it (Sprint 06-D: a marksman at a yard wall 12 m from the squad looped retreat / approach).
	if (!bHasLineOfFire)
	{
		return EMarksmanMove::Approach;
	}
	if (ShouldRetreat(DistanceCm, Config.RetreatDistance))
	{
		return EMarksmanMove::Retreat;
	}
	if (DistanceCm < Config.PreferredMinRange)
	{
		return EMarksmanMove::BackOff;
	}
	if (DistanceCm > Config.PreferredMaxRange)
	{
		return EMarksmanMove::Approach;
	}
	return EMarksmanMove::Hold;
}

FVector MarksmanAIRules::ComputeFlankDestination(const FVector& Pos, const FVector& TargetPos, const FVector& TargetFacing,
	float DesiredAngleDegrees, float DistanceCm)
{
	FVector Facing = FVector(TargetFacing.X, TargetFacing.Y, 0.f).GetSafeNormal();
	if (Facing.IsNearlyZero())
	{
		Facing = (Pos - TargetPos).GetSafeNormal2D();
	}
	const float Angle = FMath::Clamp(DesiredAngleDegrees, 45.f, 90.f);
	const FVector Left = TargetPos + Facing.RotateAngleAxis(-Angle, FVector::UpVector) * DistanceCm;
	const FVector Right = TargetPos + Facing.RotateAngleAxis(Angle, FVector::UpVector) * DistanceCm;
	return FVector::DistSquared2D(Pos, Left) <= FVector::DistSquared2D(Pos, Right) ? Left : Right;
}

float MarksmanAIRules::ComputeSniperHitChance(const FMarksmanConfig& Config, EOperativeStance ShooterStance,
	EOperativeStance TargetStance, float CoverMult, float DistanceCm)
{
	const float Shooter = ShooterStance == EOperativeStance::Prone ? Config.ProneAccuracyBonus
		: (ShooterStance == EOperativeStance::Crouching ? Config.CrouchAccuracyBonus : 1.f);
	const float Target = TargetStance == EOperativeStance::Prone ? 0.6f : (TargetStance == EOperativeStance::Crouching ? 0.8f : 1.f);
	const float Max = FMath::Max(Config.PreferredMaxRange, 1.f);
	const float Range = DistanceCm <= Max ? 1.f : FMath::Lerp(1.f, 0.5f, FMath::Clamp((DistanceCm - Max) / Max, 0.f, 1.f));
	return FMath::Clamp(Config.BaseAccuracy * Shooter * Target * FMath::Max(CoverMult, 0.f) * Range, 0.05f, 0.95f);
}

float MarksmanAIRules::GetHalfHeight(const FMarksmanConfig& Config, EOperativeStance Stance)
{
	switch (Stance)
	{
	case EOperativeStance::Prone: return Config.ProneHalfHeight;
	case EOperativeStance::Crouching: return Config.CrouchHalfHeight;
	default: return Config.StandHalfHeight;
	}
}
