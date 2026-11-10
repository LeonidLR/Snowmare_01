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

	float LayerTarget(EInjuredGait Gait, bool bEligible, float MoveShare, float ConeShare)
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
			return FMath::Clamp(MoveShare, 0.f, 1.f) * FMath::Clamp(ConeShare, 0.f, 1.f);
		default:
			return 0.f;
		}
	}

	bool UsesInjuredCrawl(EInjuredGait Gait, bool bProne, bool bHasInjuredCrawl)
	{
		return Gait == EInjuredGait::Wounded && bProne && bHasInjuredCrawl;
	}

	float TorsoPelvisShare(float Share, bool bAiming)
	{
		return bAiming ? 0.f : FMath::Clamp(Share, 0.f, 1.f);
	}
}
