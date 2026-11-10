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

	EInjuredGait ChooseTierGait(const FInjuredTriggers& Triggers, CriticalWoundRules::EWoundTier Tier, float ColdShare)
	{
		using CriticalWoundRules::EWoundTier;
		const EInjuredGait Cold = ChooseGait(Triggers, false, ColdShare);
		if (!Triggers.bWounded || Tier == EWoundTier::None)
		{
			return Cold;
		}
		if (Tier == EWoundTier::Heavy || Tier == EWoundTier::Critical)
		{
			return EInjuredGait::WoundedHeavy;
		}
		return Cold == EInjuredGait::ColdSlow ? EInjuredGait::WoundedHeavy : EInjuredGait::Wounded;
	}

	bool IsWoundGait(EInjuredGait Gait)
	{
		return Gait == EInjuredGait::Wounded || Gait == EInjuredGait::WoundedHeavy;
	}

	float RunShare(EInjuredGait Gait, float Speed, float StartSpeed, float FullSpeed, float ConeShare)
	{
		if (Gait != EInjuredGait::Wounded || FullSpeed <= 0.f)
		{
			return 0.f;
		}
		const float Start = FMath::Min(StartSpeed, FullSpeed);
		const float Band = FullSpeed - Start > KINDA_SMALL_NUMBER ? FMath::Clamp((Speed - Start) / (FullSpeed - Start), 0.f, 1.f) : (Speed >= FullSpeed ? 1.f : 0.f);
		return Band * FMath::Clamp(ConeShare, 0.f, 1.f);
	}

	EInjuredIdleSet ChooseIdleSet(EInjuredGait Gait, float SecondsSinceRun, float RunIdleHoldSeconds)
	{
		switch (Gait)
		{
		case EInjuredGait::Wounded:
			return SecondsSinceRun >= 0.f && SecondsSinceRun < RunIdleHoldSeconds ? EInjuredIdleSet::Run : EInjuredIdleSet::Walk;
		case EInjuredGait::WoundedHeavy:
			return EInjuredIdleSet::Slow;
		default:
			return EInjuredIdleSet::None;
		}
	}

	int32 NextIdleVariant(int32 Previous, int32 Count, bool bRandom, float Random01)
	{
		if (Count <= 1 || Previous < 0)
		{
			return 0;
		}
		const int32 Last = FMath::Clamp(Previous, 0, Count - 1);
		if (!bRandom)
		{
			return (Last + 1) % Count;
		}
		const int32 Pick = FMath::Clamp(static_cast<int32>(FMath::Clamp(Random01, 0.f, 0.9999f) * static_cast<float>(Count - 1)), 0, Count - 2);
		return Pick >= Last ? Pick + 1 : Pick;
	}

	FInjuredRaiseConfig SanitizeRaise(const FInjuredRaiseConfig& Config)
	{
		auto Seconds = [](float Value, float Fallback) { return FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.f, 5.f) : Fallback; };
		FInjuredRaiseConfig Out = Config;
		Out.RaiseSeconds = Seconds(Config.RaiseSeconds, 0.25f);
		Out.LowerAfterSeconds = Seconds(Config.LowerAfterSeconds, 1.f);
		Out.LowerSeconds = Seconds(Config.LowerSeconds, 0.4f);
		return Out;
	}

	float StepRaiseShare(float Share, float SecondsSinceDemand, float DeltaSeconds, const FInjuredRaiseConfig& Config)
	{
		const bool bDemand = SecondsSinceDemand >= 0.f && SecondsSinceDemand <= Config.LowerAfterSeconds;
		const float Seconds = bDemand ? Config.RaiseSeconds : Config.LowerSeconds;
		const float Target = bDemand ? 1.f : 0.f;
		if (Seconds <= KINDA_SMALL_NUMBER)
		{
			return Target;
		}
		return FMath::FInterpConstantTo(FMath::Clamp(Share, 0.f, 1.f), Target, FMath::Max(0.f, DeltaSeconds), 1.f / Seconds);
	}

	float UpperBodyShare(const FInjuredRaiseConfig& Config, float LayerWeight, float RaiseShare, float UpperSlotWeight)
	{
		if (!Config.bFullBody)
		{
			return 0.f;
		}
		return FMath::Clamp(LayerWeight, 0.f, 1.f) * (1.f - FMath::Clamp(RaiseShare, 0.f, 1.f)) * (1.f - FMath::Clamp(UpperSlotWeight, 0.f, 1.f));
	}

	bool HoldsShotForRaise(float UpperShare)
	{
		return UpperShare > ShotRaiseTolerance;
	}

	float FirstShotRaiseDelay(const FInjuredRaiseConfig& Config, float RaiseShare)
	{
		if (!Config.bFullBody)
		{
			return 0.f;
		}
		return Config.RaiseSeconds * FMath::Max(0.f, 1.f - ShotRaiseTolerance - FMath::Clamp(RaiseShare, 0.f, 1.f));
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
		case EInjuredGait::WoundedHeavy:
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
		return Gait == EInjuredGait::ColdSlow || Gait == EInjuredGait::WoundedHeavy ? FMath::Clamp(ConeShare, 0.f, 1.f) : 0.f;
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
		return IsWoundGait(Gait) && bProne && bHasInjuredCrawl;
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
		Out.CrouchScale = FMath::IsFinite(Cue.CrouchScale) ? FMath::Clamp(Cue.CrouchScale, 0.f, 1.f) : 0.f;
		Out.CoverScale = FMath::IsFinite(Cue.CoverScale) ? FMath::Clamp(Cue.CoverScale, 0.f, 1.f) : 0.f;
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
