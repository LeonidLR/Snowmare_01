#include "Survival/ColdRules.h"

namespace ColdRules
{
	float GetStanceMultiplier(const FColdConfig& Config, EOperativeStance Stance)
	{
		switch (Stance)
		{
		case EOperativeStance::Crouching:
			return Config.StanceMultiplierCrouching;
		case EOperativeStance::Prone:
			return Config.StanceMultiplierProne;
		default:
			return Config.StanceMultiplierStanding;
		}
	}

	float StepCold(const FColdConfig& Config, float Cold, float DeltaSeconds, const FColdEnvironment& Environment,
		EOperativeStance Stance, float Fortitude)
	{
		if (Environment.bWarm || Environment.bPreparation || Environment.ZoneMultiplier <= 0.001f)
		{
			const float Boost = 1.f + Fortitude * Config.FortitudeWarmupPerPoint;
			return FMath::Max(0.f, Cold - Config.WarmthRecoveryRate * Boost * DeltaSeconds);
		}
		if (Environment.bSprinting)
		{
			// Deviation (user decision 2026-10-01): the sprint warms the body up instead of letting it cool down.
			const float Boost = 1.f + Fortitude * Config.FortitudeWarmupPerPoint;
			return FMath::Max(0.f, Cold - Config.SprintWarmupRate * Boost * DeltaSeconds);
		}
		const float FortitudeCut = FMath::Clamp(Fortitude * Config.FortitudeCutPerPoint, 0.f, Config.MaxFortitudeCut);
		const float Wind = (Environment.bElevated ? Config.ElevatedWindMultiplier : 1.f)
			* (Environment.bInCover ? FMath::Clamp(Config.CoverWindChillMultiplier, 0.f, 1.f) : 1.f); // Sprint 12 shelter
		const float Rate = Environment.ZoneMultiplier * GetStanceMultiplier(Config, Stance) * Wind;
		return FMath::Min(100.f, Cold + Config.AccumulationRate * (1.f - FortitudeCut) * Rate * DeltaSeconds);
	}

	EColdTier GetTier(float Cold)
	{
		if (Cold < 40.f)
		{
			return EColdTier::Normal;
		}
		if (Cold < 70.f)
		{
			return EColdTier::Chills;
		}
		if (Cold < 90.f)
		{
			return EColdTier::Freezing;
		}
		return Cold >= 100.f ? EColdTier::Frostbite : EColdTier::Hypothermia;
	}

	float GetSpeedMultiplier(EColdTier Tier)
	{
		switch (Tier)
		{
		case EColdTier::Normal:
			return 1.f;
		case EColdTier::Chills:
			return 0.7f;
		case EColdTier::Freezing:
			return 0.45f;
		default:
			return 0.25f;
		}
	}

	int32 GetMaxActionPoints(EColdTier Tier)
	{
		switch (Tier)
		{
		case EColdTier::Normal:
			return 5;
		case EColdTier::Chills:
			return 4;
		case EColdTier::Freezing:
			return 3;
		default:
			return 2;
		}
	}

	float GetMisfireChance(const FColdConfig& Config, float Cold, bool bNearHeat)
	{
		if (bNearHeat || Cold < Config.MisfireThreshold)
		{
			return 0.f;
		}
		const float Denominator = FMath::Max(1.f, 100.f - Config.MisfireThreshold);
		return Config.MisfireMaxChance * FMath::Clamp((Cold - Config.MisfireThreshold) / Denominator, 0.f, 1.f);
	}

	float GetAimPenalty(const FColdConfig& Config, float Cold)
	{
		return Cold > 50.f ? Config.AimPenaltyMax * ((Cold - 50.f) / 50.f) : 0.f;
	}

	bool UpdateWeaponFrozen(const FColdConfig& Config, bool bFrozen, float Cold, bool bNearHeat)
	{
		if (bFrozen)
		{
			return !(bNearHeat || Cold < Config.WeaponFreezeThreshold - Config.WeaponThawHysteresis);
		}
		return Cold >= Config.WeaponFreezeThreshold && !bNearHeat;
	}
}
