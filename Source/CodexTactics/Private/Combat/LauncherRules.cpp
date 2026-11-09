#include "Combat/LauncherRules.h"

#include "Combat/ExplosionRules.h"

namespace LauncherRules
{
	float MinimumRangeCm(const FWeaponStats& Stats)
	{
		return FMath::Max3(Stats.Range.MinRangeCm, Stats.Explosion.RadiusCm, Stats.Projectile.ArmingDistanceCm);
	}

	ELauncherRefusal Refusal(bool bLoaded, float TargetDistanceCm, const FWeaponStats& Stats)
	{
		if (!bLoaded)
		{
			return ELauncherRefusal::Empty;
		}
		return TargetDistanceCm < MinimumRangeCm(Stats) ? ELauncherRefusal::TooClose : ELauncherRefusal::None;
	}

	float DamageOn(const FWeaponStats& Stats, bool bStruck, float DistanceCm)
	{
		if (bStruck && Stats.Explosion.DirectHitDamage > 0.f)
		{
			return Stats.Explosion.DirectHitDamage;
		}
		return ExplosionRules::VictimDamage(Stats.Damage.BaseDamage, ExplosionRules::FromWeapon(Stats.Explosion), DistanceCm, Stats.Explosion.RadiusCm);
	}

	FVector MissPoint(const FVector& AimPoint, const FVector& AimDir, float Roll01, float SideRoll)
	{
		const FVector Flat = FVector(AimDir.X, AimDir.Y, 0.f).GetSafeNormal();
		const FVector Forward = Flat.IsNearlyZero() ? FVector::ForwardVector : Flat;
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
		const float Scatter = FMath::Lerp(MinScatterCm, MaxScatterCm, FMath::Clamp(Roll01, 0.f, 1.f));
		const float Side = SideRoll < 0.5f ? -1.f : 1.f;
		// Mostly sideways, a little long or short (the same roll: deterministic for a seed).
		return AimPoint + Right * (Side * Scatter * 0.8f) + Forward * (Scatter * 0.6f * (Roll01 < 0.5f ? -1.f : 1.f));
	}

	FString RefusalText(ELauncherRefusal Refusal)
	{
		switch (Refusal)
		{
		case ELauncherRefusal::TooClose: return TEXT("RPG: too close - you would be inside your own blast");
		case ELauncherRefusal::Empty: return TEXT("RPG: no rocket loaded");
		default: return FString();
		}
	}
}
