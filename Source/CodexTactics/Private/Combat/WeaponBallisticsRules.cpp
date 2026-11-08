#include "Combat/WeaponBallisticsRules.h"

#include "Data/WeaponStats.h"

namespace WeaponBallisticsRules
{
	float DamageFalloffMultiplier(float DistanceCm, float EffectiveCm, float MaxCm, float Floor)
	{
		if (Floor >= 1.f || MaxCm <= EffectiveCm || DistanceCm <= EffectiveCm)
		{
			return 1.f;
		}
		const float T = FMath::Clamp((DistanceCm - EffectiveCm) / (MaxCm - EffectiveCm), 0.f, 1.f);
		return FMath::Lerp(1.f, FMath::Clamp(Floor, 0.f, 1.f), T);
	}

	float DamageFalloffMultiplier(const FWeaponStats& Stats, float DistanceCm)
	{
		return DamageFalloffMultiplier(DistanceCm, Stats.Range.GetEffectiveRangeCm(), Stats.Range.MaxRangeCm, Stats.Damage.FalloffDamageFloor);
	}

	int32 RollPelletHits(int32 Count, float HitChance, TFunctionRef<float()> Rand, bool bForceHit)
	{
		const int32 Pellets = FMath::Max(1, Count);
		if (bForceHit)
		{
			return Pellets;
		}
		int32 Hits = 0;
		for (int32 Index = 0; Index < Pellets; ++Index)
		{
			Hits += Rand() <= HitChance ? 1 : 0;
		}
		return Hits;
	}

	float PelletDamageShare(int32 Hits, int32 Count)
	{
		return Count <= 1 ? (Hits > 0 ? 1.f : 0.f) : static_cast<float>(FMath::Clamp(Hits, 0, Count)) / static_cast<float>(Count);
	}

	bool ReloadsPerRound(const FWeaponAmmoStats& Ammo)
	{
		return Ammo.ReloadMode == EWeaponReloadMode::PerRound && Ammo.MagazineSize > 0;
	}

	float ReloadStepSeconds(const FWeaponAmmoStats& Ammo)
	{
		if (!ReloadsPerRound(Ammo))
		{
			return Ammo.ReloadSeconds;
		}
		return Ammo.PerRoundReloadSeconds > 0.f ? Ammo.PerRoundReloadSeconds : Ammo.ReloadSeconds / static_cast<float>(Ammo.MagazineSize);
	}

	int32 ReloadStepRounds(const FWeaponAmmoStats& Ammo, int32 Clip, int32 Reserve)
	{
		const int32 Needed = FMath::Max(0, Ammo.MagazineSize - Clip);
		return FMath::Max(0, FMath::Min(ReloadsPerRound(Ammo) ? FMath::Min(1, Needed) : Needed, Reserve));
	}

	bool ContinueReload(const FWeaponAmmoStats& Ammo, int32 Clip, int32 Reserve)
	{
		return ReloadsPerRound(Ammo) && Clip < Ammo.MagazineSize && Reserve > 0;
	}

	bool InterruptReload(const FWeaponAmmoStats& Ammo, int32 Clip, bool bWantsToFire)
	{
		return ReloadsPerRound(Ammo) && Clip > 0 && bWantsToFire;
	}

	float SettleRecoil(float BloomDeg, float SecondsSinceLastRound, float PerRoundDeg, float RecoverySeconds)
	{
		if (PerRoundDeg <= 0.f || BloomDeg <= 0.f)
		{
			return 0.f;
		}
		if (RecoverySeconds <= 0.f)
		{
			return 0.f;
		}
		return FMath::Max(0.f, BloomDeg - FMath::Max(0.f, SecondsSinceLastRound) * PerRoundDeg / RecoverySeconds);
	}
}
