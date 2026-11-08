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

int32 SightRules::HeightLevel(float EyeAboveGroundCm, float LevelStepCm)
{
	if (LevelStepCm <= 0.f || !FMath::IsFinite(EyeAboveGroundCm))
	{
		return 0;
	}
	return FMath::Max(0, FMath::FloorToInt(EyeAboveGroundCm / LevelStepCm));
}

float SightRules::HeightRangeMultiplier(const FHeightSightConfig& Config, int32 Level)
{
	if (Level <= 0)
	{
		return 1.f;
	}
	const float Multiplier = 1.f + FMath::Max(Config.BonusPerLevel, 0.f) * static_cast<float>(Level);
	return FMath::Clamp(Multiplier, 1.f, FMath::Max(Config.MaxMultiplier, 1.f));
}

float SightRules::WeatherSightMultiplier(const FSightConditionsConfig& Config, EWeatherState Weather)
{
	const int32 Index = static_cast<int32>(Weather);
	return Index >= 0 && Index < UE_ARRAY_COUNT(Config.WeatherMultipliers) ? FMath::Max(Config.WeatherMultipliers[Index], 0.f) : 1.f;
}

float SightRules::BlendedWeatherSightMultiplier(const FSightConditionsConfig& Config, EWeatherState From, EWeatherState To, float Blend)
{
	return FMath::Lerp(WeatherSightMultiplier(Config, From), WeatherSightMultiplier(Config, To), FMath::Clamp(Blend, 0.f, 1.f));
}

float SightRules::DaylightSightMultiplier(const FSightConditionsConfig& Config, float SunElevationDeg)
{
	if (!FMath::IsFinite(SunElevationDeg))
	{
		return FMath::Max(Config.DayMultiplier, 0.f);
	}
	const float DayDeg = FMath::Max(Config.DayElevationDeg, 0.01f);
	const float NightDeg = FMath::Min(Config.NightElevationDeg, -0.01f);
	float Multiplier;
	if (SunElevationDeg >= 0.f)
	{
		Multiplier = FMath::Lerp(Config.TwilightMultiplier, Config.DayMultiplier, FMath::Clamp(SunElevationDeg / DayDeg, 0.f, 1.f));
	}
	else
	{
		Multiplier = FMath::Lerp(Config.TwilightMultiplier, Config.NightMultiplier, FMath::Clamp(SunElevationDeg / NightDeg, 0.f, 1.f));
	}
	return FMath::Max(Multiplier, 0.f);
}

float SightRules::ConditionsSightMultiplier(const FSightConditionsConfig& Config, EWeatherState From, EWeatherState To, float Blend, float SunElevationDeg)
{
	return BlendedWeatherSightMultiplier(Config, From, To, Blend) * DaylightSightMultiplier(Config, SunElevationDeg);
}

float SightRules::HeightRangeMultiplierAt(const FHeightSightConfig& Config, float EyeAboveGroundCm)
{
	return HeightRangeMultiplier(Config, HeightLevel(EyeAboveGroundCm, Config.LevelStepCm));
}

float SightRules::GroundBelowZ(float FeetZ, TArrayView<const float> SurfaceZs)
{
	float Ground = FeetZ;
	bool bAny = false;
	for (const float Z : SurfaceZs)
	{
		Ground = bAny ? FMath::Min(Ground, Z) : Z;
		bAny = true;
	}
	return Ground;
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

TArray<float> SightRules::BearingsNearestFirst(float PreferredYaw, float StepDeg)
{
	TArray<float> Out;
	Out.Add(FRotator::NormalizeAxis(PreferredYaw));
	if (StepDeg <= 0.f)
	{
		return Out;
	}
	for (float Off = StepDeg; Off <= 180.f + KINDA_SMALL_NUMBER; Off += StepDeg)
	{
		Out.Add(FRotator::NormalizeAxis(PreferredYaw + Off));
		if (Off < 180.f - KINDA_SMALL_NUMBER)
		{
			Out.Add(FRotator::NormalizeAxis(PreferredYaw - Off));
		}
	}
	return Out;
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

SightRules::EContact SightRules::ResolveContact(bool bEyeSees, bool bHeard, bool bLineToSource)
{
	if (bEyeSees || (bHeard && bLineToSource))
	{
		return EContact::Seen;
	}
	return bHeard ? EContact::Heard : EContact::None;
}

float SightRules::BlindFireHitChance(float BaseHitChance, float EnemyDistanceFromGhostCm)
{
	return EnemyDistanceFromGhostCm <= BlindFireHitRadiusCm ? FMath::Clamp(BaseHitChance * BlindFireAccuracyMultiplier, 0.f, 1.f) : 0.f;
}

bool SightRules::ShouldForget(float SecondsSinceSeen, float SecondsSearchingThere)
{
	return SecondsSinceSeen >= ForgetSeconds || SecondsSearchingThere >= SearchSeconds;
}
