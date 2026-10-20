#include "Combat/SightRules.h"

bool SightRules::IsSquadSightPhase(ECodexGamePhase Phase, ECodexCombatMode CombatMode, bool bExplorationSight)
{
	if (Phase == ECodexGamePhase::WaveCombat)
	{
		return CombatMode != ECodexCombatMode::TurnBased;
	}
	return bExplorationSight && Phase == ECodexGamePhase::Exploration;
}

bool SightRules::ShouldDropLastKnownGhost(bool bInFight, bool bHeard, float SecondsSinceLastKnown)
{
	return !bInFight && !bHeard && SecondsSinceLastKnown >= LastKnownFadeSeconds;
}

float SightRules::FieldOfViewDeg(const FSightFovConfig& Config, EOperativeStance Stance)
{
	switch (Stance)
	{
	case EOperativeStance::Prone:
		return Config.ProneDeg;
	case EOperativeStance::Crouching:
		return Config.CrouchingDeg;
	default:
		return Config.StandingDeg;
	}
}

bool SightRules::IsInFieldOfView(const FVector& Eye, const FVector& Facing, const FVector& Target, float FovDeg)
{
	if (FovDeg >= 360.f)
	{
		return true;
	}
	const FVector Forward = Facing.GetSafeNormal2D();
	const FVector ToTarget = (Target - Eye).GetSafeNormal2D();
	if (Forward.IsNearlyZero() || ToTarget.IsNearlyZero())
	{
		return true;
	}
	const float CosHalf = FMath::Cos(FMath::DegreesToRadians(FMath::Max(FovDeg, 0.f) * 0.5f));
	return FVector::DotProduct(Forward, ToTarget) >= CosHalf;
}

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

bool SightRules::IsWithinTouchReveal(float DistanceCm)
{
	return DistanceCm <= SquadTouchRevealCm;
}

float SightRules::BlindFireHitChance(float BaseHitChance, float EnemyDistanceFromGhostCm)
{
	return EnemyDistanceFromGhostCm <= BlindFireHitRadiusCm ? FMath::Clamp(BaseHitChance * BlindFireAccuracyMultiplier, 0.f, 1.f) : 0.f;
}

bool SightRules::ShouldForget(float SecondsSinceSeen, float SecondsSearchingThere)
{
	return SecondsSinceSeen >= ForgetSeconds || SecondsSearchingThere >= SearchSeconds;
}
