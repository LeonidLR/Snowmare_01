#include "Combat/WeaponRangeRules.h"

namespace
{
	FWeaponRangeFalloff GFalloff;
}

float WeaponRangeRules::ResolveEffectiveCm(float EffectiveCm, float MaxCm)
{
	if (MaxCm <= 0.f)
	{
		return FMath::Max(EffectiveCm, 0.f);
	}
	return EffectiveCm > 0.f ? FMath::Min(EffectiveCm, MaxCm) : MaxCm;
}

float WeaponRangeRules::ScaledEffectiveCm(float EffectiveCm, float MaxCm, float Scale)
{
	const float Effective = ResolveEffectiveCm(EffectiveCm, MaxCm);
	const float Scaled = Effective * FMath::Max(Scale, 0.f);
	return MaxCm > 0.f ? FMath::Min(Scaled, MaxCm) : Scaled;
}

bool WeaponRangeRules::IsWithinReach(float DistanceCm, float MaxCm)
{
	return MaxCm <= 0.f || DistanceCm <= MaxCm;
}

float WeaponRangeRules::HitMultiplier(float DistanceCm, float EffectiveCm, float MaxCm, const FWeaponRangeFalloff& Falloff)
{
	if (!IsWithinReach(DistanceCm, MaxCm))
	{
		return 0.f;
	}
	const float Effective = ResolveEffectiveCm(EffectiveCm, MaxCm);
	if (DistanceCm <= Effective || MaxCm <= Effective)
	{
		return 1.f;
	}
	const float T = FMath::Clamp((DistanceCm - Effective) / (MaxCm - Effective), 0.f, 1.f);
	const float Floor = FMath::Clamp(Falloff.FloorMultiplier, 0.f, 1.f);
	return 1.f - (1.f - Floor) * FMath::Pow(T, FMath::Max(Falloff.Exponent, 0.1f));
}

const FWeaponRangeFalloff& WeaponRangeRules::GetFalloff()
{
	return GFalloff;
}

void WeaponRangeRules::SetFalloff(const FWeaponRangeFalloff& Falloff)
{
	GFalloff = Falloff;
}
