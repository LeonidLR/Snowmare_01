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

	FLauncherAmmoCap CapAmmo(int32 Clip, int32 Reserve, int32 MaxRockets)
	{
		const int32 Cap = FMath::Max(0, MaxRockets);
		const int32 ClampedClip = FMath::Clamp(Clip, 0, Cap);
		const int32 ClampedReserve = FMath::Clamp(Reserve, 0, FMath::Max(0, Cap - ClampedClip));
		return { ClampedClip, ClampedReserve };
	}

	int32 CapReserveToAdd(int32 CurrentClip, int32 CurrentReserve, int32 AddCount, int32 MaxRockets)
	{
		if (AddCount <= 0)
		{
			return 0;
		}
		const int32 Cap = FMath::Max(0, MaxRockets);
		const int32 Have = FMath::Max(0, CurrentClip) + FMath::Max(0, CurrentReserve);
		return FMath::Clamp(AddCount, 0, FMath::Max(0, Cap - Have));
	}
}
