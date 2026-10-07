#include "Tactics/CoverDecisionRules.h"

namespace CoverDecisionRules
{
	float DangerScore(const FCoverDecisionConfig& Config, const FCoverFireSituation& Situation)
	{
		float Danger = 0.5f * FMath::Clamp(Situation.SuppressionPressure, 0.f, 1.f)
			+ 0.5f * FMath::Min(1.f, Situation.RecentIncomingDamage / FMath::Max(Config.DamageNormaliser, 1.f));
		if (Situation.HealthFraction < Config.PeekMinHealthFraction)
		{
			Danger += Config.LowHealthDangerBonus;
		}
		return Danger;
	}

	ECoverFireDecision DecideFire(const FCoverDecisionConfig& Config, const FCoverFireSituation& Situation)
	{
		if (Situation.bSniperLaserOnMe)
		{
			return ECoverFireDecision::Hold; // nothing shows until the sniper's shot has passed
		}
		if (Situation.bEnemyInFrontOfCover)
		{
			return ECoverFireDecision::OpenShot; // the wall does not stand between them: a normal shot off the wall
		}
		if (Situation.Height == ECoverHeight::HighCover && !Situation.bEdgeExposed)
		{
			return ECoverFireDecision::Hold; // nothing to shoot around or over
		}
		if (Situation.HealthFraction < Config.PeekMinHealthFraction)
		{
			return Situation.DistanceToEnemyCm < Config.CloseRangeCm ? ECoverFireDecision::BlindFire : ECoverFireDecision::Hold;
		}
		const float Danger = DangerScore(Config, Situation);
		if (Situation.DistanceToEnemyCm > Config.BlindFireMaxRangeCm)
		{
			return Danger < Config.HoldDangerFar ? ECoverFireDecision::CornerPeek : ECoverFireDecision::Hold;
		}
		if (Situation.DistanceToEnemyCm < Config.CloseRangeCm && Danger >= Config.CloseRangeDanger)
		{
			return ECoverFireDecision::BlindFire;
		}
		return Danger >= Config.BlindFireDangerThreshold ? ECoverFireDecision::BlindFire : ECoverFireDecision::CornerPeek;
	}

	ECoverStanceDecision DecideStance(const FCoverDecisionConfig& Config, const FCoverStanceSituation& Situation)
	{
		if (Situation.bSniperLaserOnMe)
		{
			return ECoverStanceDecision::Crouch;
		}
		if (Situation.Height == ECoverHeight::LowCover)
		{
			const bool bStandToFire = Situation.bWantsAimedFire && Situation.SuppressionPressure < Config.StandMaxSuppressionLow
				&& Situation.HealthFraction >= Config.PeekMinHealthFraction;
			return bStandToFire ? ECoverStanceDecision::Stand : ECoverStanceDecision::Crouch;
		}
		if (Situation.SuppressionPressure >= Config.CrouchSuppressionHigh || Situation.HealthFraction < Config.PeekMinHealthFraction
			|| Situation.bEnemyElevated)
		{
			return ECoverStanceDecision::Crouch;
		}
		return ECoverStanceDecision::Stand;
	}

	float SuppressionFromShooters(const FCoverDecisionConfig& Config, int32 ShootersTargetingHim)
	{
		return FMath::Clamp(ShootersTargetingHim * Config.PressurePerShooter, 0.f, 1.f);
	}

	float DecayRecentDamage(const FCoverDecisionConfig& Config, float RecentDamage, float DeltaSeconds)
	{
		const float Rate = FMath::Max(Config.DamageNormaliser, 1.f) / FMath::Max(Config.RecentDamageSeconds, 0.1f);
		return FMath::Max(0.f, RecentDamage - Rate * DeltaSeconds);
	}

	EOperativeStance ToStance(ECoverStanceDecision Decision)
	{
		return Decision == ECoverStanceDecision::Stand ? EOperativeStance::Standing : EOperativeStance::Crouching;
	}

	ECoverFireMode ToFireMode(ECoverFireDecision Decision)
	{
		switch (Decision)
		{
		case ECoverFireDecision::CornerPeek: return ECoverFireMode::CornerLean;
		case ECoverFireDecision::BlindFire: return ECoverFireMode::BlindFire;
		default: return ECoverFireMode::Normal;
		}
	}

	const TCHAR* FireDecisionName(ECoverFireDecision Decision)
	{
		switch (Decision)
		{
		case ECoverFireDecision::CornerPeek: return TEXT("CornerPeek");
		case ECoverFireDecision::BlindFire: return TEXT("BlindFire");
		case ECoverFireDecision::OpenShot: return TEXT("OpenShot");
		default: return TEXT("Hold");
		}
	}

	ECornerAimDecision DecideCornerAim(const FCoverDecisionConfig& Config, const FCornerAimSituation& Situation)
	{
		const bool bLowClip = Situation.bHasReserve && Situation.ClipFraction <= Config.AimEarlyReloadFraction;
		if (Situation.ClipFraction <= 0.f)
		{
			return Situation.bHasReserve ? ECornerAimDecision::DuckToReload : ECornerAimDecision::DuckForSafety;
		}
		if (Situation.bSniperLaserOnMe || Situation.bGrenadeNearby || Situation.bFlankEnemyNear)
		{
			return ECornerAimDecision::DuckForSafety;
		}
		const bool bHurt = Situation.HealthFraction < Config.PeekMinHealthFraction
			|| Situation.RecentIncomingDamage >= Config.AimBigHitDamage
			|| Situation.SuppressionPressure >= Config.AimBreakSuppression
			|| (Situation.HealthFraction < Config.AimWoundedHealthFraction && Situation.RecentIncomingDamage > 0.f);
		if (bHurt)
		{
			return bLowClip ? ECornerAimDecision::DuckToReload : ECornerAimDecision::DuckForSafety; // the forced duck reloads a low magazine
		}
		if (bLowClip && Situation.SecondsWithoutTarget >= Config.AimEarlyReloadLullSeconds)
		{
			return ECornerAimDecision::DuckToReload; // a lull (or the fight over) with a low magazine: reload behind the corner first
		}
		if (!Situation.bHoldWithoutTargets && Situation.SecondsWithoutTarget >= Config.AimNoTargetGraceSeconds)
		{
			return ECornerAimDecision::ReturnNoTargets;
		}
		return ECornerAimDecision::StayAndFire;
	}

	const TCHAR* CornerAimDecisionName(ECornerAimDecision Decision)
	{
		switch (Decision)
		{
		case ECornerAimDecision::DuckToReload: return TEXT("DuckToReload");
		case ECornerAimDecision::DuckForSafety: return TEXT("DuckForSafety");
		case ECornerAimDecision::ReturnNoTargets: return TEXT("ReturnNoTargets");
		default: return TEXT("StayAndFire");
		}
	}
}