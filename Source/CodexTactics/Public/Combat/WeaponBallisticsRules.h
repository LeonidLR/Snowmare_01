#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

struct FWeaponStats;
struct FWeaponAmmoStats;

/**
 * Ballistics driven by the unified weapon schema (weapons framework step 2; game designer decisions 2-5, 2026-11-08;
 * docs/design/WEAPONS_FRAMEWORK.md section 12). UE-only, no Godot reference. Pure logic, tested in CodexTactics.Weapons.Ballistics.*.
 * - Damage falloff (decision 4): x1 up to the weapon's effective range, linear down to Damage.FalloffDamageFloor at its max reach.
 * - Pellets (decision 2): Pellets.Count independent rolls against the shot's hit chance; the hit does BaseDamage x hits / Count.
 * - Per-shell reload (decision 3): a reload step loads one round, the operative may fire between steps.
 * - Recoil bloom (decision 5): + RecoilPerRoundDeg of spread per full-auto round, settling at RecoilPerRoundDeg / RecoilRecoverySeconds
 *   degrees per second.
 */
namespace WeaponBallisticsRules
{
	/** Damage multiplier at DistanceCm (EffectiveCm already resolved into (0, MaxCm]); 1 when Floor >= 1. */
	CODEXTACTICS_API float DamageFalloffMultiplier(float DistanceCm, float EffectiveCm, float MaxCm, float Floor);

	/** Same, from a weapon's stats (its own resolved effective range; the stance / elevation stretch is for the hit chance only). */
	CODEXTACTICS_API float DamageFalloffMultiplier(const FWeaponStats& Stats, float DistanceCm);

	/**
	 * Pellets of one shot that hit: Count rolls of Rand() <= HitChance. Count 1 makes exactly ONE Rand() call (the pre-pellet single
	 * roll, same random stream). bForceHit = every pellet hits (tests).
	 */
	CODEXTACTICS_API int32 RollPelletHits(int32 Count, float HitChance, TFunctionRef<float()> Rand, bool bForceHit = false);

	/** The share of the shot's damage Hits pellets of Count deliver. */
	CODEXTACTICS_API float PelletDamageShare(int32 Hits, int32 Count);

	/** True when the weapon reloads round by round. */
	CODEXTACTICS_API bool ReloadsPerRound(const FWeaponAmmoStats& Ammo);

	/** Seconds of one reload step: the whole reload, or one round (PerRoundReloadSeconds; 0 = ReloadSeconds / MagazineSize). */
	CODEXTACTICS_API float ReloadStepSeconds(const FWeaponAmmoStats& Ammo);

	/** Rounds one finished step loads: the whole gap (magazine) or one (per round), limited by the reserve. */
	CODEXTACTICS_API int32 ReloadStepRounds(const FWeaponAmmoStats& Ammo, int32 Clip, int32 Reserve);

	/** A per-round reload carries on after a step while the magazine is not full and there is reserve. */
	CODEXTACTICS_API bool ContinueReload(const FWeaponAmmoStats& Ammo, int32 Clip, int32 Reserve);

	/** A per-round reload stops between steps when the shooter has something to fire at and at least one round in. */
	CODEXTACTICS_API bool InterruptReload(const FWeaponAmmoStats& Ammo, int32 Clip, bool bWantsToFire);

	/** The recoil bloom after SecondsSinceLastRound without firing (linear settle); 0 with no recoil. */
	CODEXTACTICS_API float SettleRecoil(float BloomDeg, float SecondsSinceLastRound, float PerRoundDeg, float RecoverySeconds);
}
