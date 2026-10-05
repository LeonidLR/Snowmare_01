#include "Combat/SightRules.h"

float SightRules::EyeHeight(EOperativeStance Stance)
{
	switch (Stance)
	{
	case EOperativeStance::Prone:
		return 25.f;
	case EOperativeStance::Crouching:
		return 95.f;
	default:
		return 160.f;
	}
}

float SightRules::ProfileHeight(EOperativeStance Stance)
{
	switch (Stance)
	{
	case EOperativeStance::Prone:
		return 25.f;
	case EOperativeStance::Crouching:
		return 90.f;
	default:
		return 150.f;
	}
}

float SightRules::LineHeightAt(float EyeHeightCm, float TargetHeightCm, float CoverDistanceCm, float TargetDistanceCm)
{
	if (TargetDistanceCm <= KINDA_SMALL_NUMBER)
	{
		return EyeHeightCm;
	}
	const float Alpha = FMath::Clamp(CoverDistanceCm / TargetDistanceCm, 0.f, 1.f);
	return FMath::Lerp(EyeHeightCm, TargetHeightCm, Alpha);
}

bool SightRules::ClearsCover(float EyeHeightCm, float TargetHeightCm, float CoverDistanceCm, float TargetDistanceCm, float CoverTopCm)
{
	return LineHeightAt(EyeHeightCm, TargetHeightCm, CoverDistanceCm, TargetDistanceCm) > CoverTopCm;
}

float SightRules::EnemyHearingRadius(EOperativeStance Stance)
{
	return Stance == EOperativeStance::Prone ? ProneHearingCm : EnemyHearingCm;
}

float SightRules::BlindFireHitChance(float BaseHitChance, float EnemyDistanceFromGhostCm)
{
	return EnemyDistanceFromGhostCm <= BlindFireHitRadiusCm ? FMath::Clamp(BaseHitChance * BlindFireAccuracyMultiplier, 0.f, 1.f) : 0.f;
}

bool SightRules::ShouldForget(float SecondsSinceSeen, float SecondsSearchingThere)
{
	return SecondsSinceSeen >= ForgetSeconds || SecondsSearchingThere >= SearchSeconds;
}
