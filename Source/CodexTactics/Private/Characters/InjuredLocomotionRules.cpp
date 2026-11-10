#include "Characters/InjuredLocomotionRules.h"

namespace InjuredLocomotionRules
{
	float SanitizeColdFrom(double Value)
	{
		if (!FMath::IsFinite(Value) || Value <= 0.0)
		{
			return 0.f;
		}
		if (Value <= 1.0)
		{
			return static_cast<float>(Value);
		}
		return Value <= 100.0 ? static_cast<float>(Value / 100.0) : 0.f;
	}

	EInjuredGait ChooseGait(const FInjuredTriggers& Triggers, bool bWounded, float ColdShare)
	{
		if (Triggers.bWounded && bWounded)
		{
			return EInjuredGait::Wounded;
		}
		if (Triggers.ColdSlowFrom > 0.f && ColdShare >= Triggers.ColdSlowFrom)
		{
			return EInjuredGait::ColdSlow;
		}
		if (Triggers.ColdFrom > 0.f && ColdShare >= Triggers.ColdFrom)
		{
			return EInjuredGait::Cold;
		}
		return EInjuredGait::None;
	}

	float ForwardConeShare(float RelativeDeg, float FullDeg, float ZeroDeg)
	{
		const float Abs = FMath::Abs(FRotator::NormalizeAxis(RelativeDeg));
		const float Full = FMath::Clamp(FullDeg, 0.f, 180.f);
		const float Zero = FMath::Clamp(ZeroDeg, Full, 180.f);
		if (Abs <= Full)
		{
			return 1.f;
		}
		if (Abs >= Zero || Zero - Full <= KINDA_SMALL_NUMBER)
		{
			return 0.f;
		}
		return 1.f - (Abs - Full) / (Zero - Full);
	}

	float LayerTarget(EInjuredGait Gait, bool bEligible, float MoveShare)
	{
		if (!bEligible)
		{
			return 0.f;
		}
		switch (Gait)
		{
		case EInjuredGait::Wounded:
			return 1.f;
		case EInjuredGait::Cold:
		case EInjuredGait::ColdSlow:
			return FMath::Clamp(MoveShare, 0.f, 1.f);
		default:
			return 0.f;
		}
	}

	float SlowWalkShare(EInjuredGait Gait, float ConeShare)
	{
		return Gait == EInjuredGait::ColdSlow ? FMath::Clamp(ConeShare, 0.f, 1.f) : 0.f;
	}

	float BandedAlpha(float Alpha, float BandDeg, float GapDeg)
	{
		const float A = FMath::Clamp(Alpha, 0.f, 1.f);
		const float Band = GapDeg > KINDA_SMALL_NUMBER ? FMath::Clamp(BandDeg / GapDeg, 0.f, 0.49f) : 0.f;
		if (Band <= 0.f)
		{
			return A;
		}
		if (A <= Band)
		{
			return 0.f;
		}
		if (A >= 1.f - Band)
		{
			return 1.f;
		}
		return (A - Band) / (1.f - 2.f * Band);
	}

	bool UsesInjuredCrawl(EInjuredGait Gait, bool bProne, bool bHasInjuredCrawl)
	{
		return Gait == EInjuredGait::Wounded && bProne && bHasInjuredCrawl;
	}

	float TorsoPelvisShare(float Share, bool bAiming)
	{
		return bAiming ? 0.f : FMath::Clamp(Share, 0.f, 1.f);
	}

	FInjuredLimpCue SanitizeLimpCue(const FInjuredLimpCue& Cue)
	{
		FInjuredLimpCue Out = Cue;
		Out.HunchDeg = FMath::IsFinite(Cue.HunchDeg) ? FMath::Clamp(Cue.HunchDeg, 0.f, 30.f) : 0.f;
		Out.SwayDeg = FMath::IsFinite(Cue.SwayDeg) ? FMath::Clamp(Cue.SwayDeg, 0.f, 20.f) : 0.f;
		Out.SwayPhase = FMath::IsFinite(Cue.SwayPhase) ? Cue.SwayPhase - FMath::FloorToFloat(Cue.SwayPhase) : 0.f;
		Out.AimingScale = FMath::IsFinite(Cue.AimingScale) ? FMath::Clamp(Cue.AimingScale, 0.f, 1.f) : 0.f;
		return Out;
	}

	FVector2D LimpCueAngles(const FInjuredLimpCue& Cue, float LayerWeight, float WalkShare, float GaitPhase, bool bAiming)
	{
		if (!Cue.bEnabled)
		{
			return FVector2D::ZeroVector;
		}
		const float Scale = FMath::Clamp(LayerWeight, 0.f, 1.f) * (bAiming ? FMath::Clamp(Cue.AimingScale, 0.f, 1.f) : 1.f);
		const float Hunch = Cue.HunchDeg * Scale;
		const float Sway = Cue.SwayDeg * Scale * FMath::Clamp(WalkShare, 0.f, 1.f) * FMath::Sin(UE_TWO_PI * (GaitPhase + Cue.SwayPhase));
		return FVector2D(Hunch, Sway);
	}
}
