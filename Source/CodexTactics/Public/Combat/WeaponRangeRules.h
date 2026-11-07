#pragma once

#include "CoreMinimal.h"

/**
 * Weapon reach and accuracy falloff by distance (bug #39, user 2026-11-04: "operatives with an M16 kill very distant enemies like a sniper"
 * after the 120 m sight range). UE-only, no Godot reference (Godot's ranges were 14 m and the sight never reached past them).
 *
 * Every real-time shooter (the squad, the hostile PMC operatives - AOperativeCharacter - and the Marksman) obeys two distances per weapon
 * (Content/Data/Weapons/weapons_tuning.json): the MAX reach (attack_range_m) - no shot beyond it, a target farther out is seen but not
 * engaged - and the EFFECTIVE range (effective_range_m) - up to it the weapon's own distance table applies unchanged, from it to the max
 * reach the hit chance falls steeply (x 1 at the effective range down to FloorMultiplier at the max reach, eased by Exponent).
 * The stance / elevation range bonuses (SquadFireRules) stretch the effective range (a steadier aim), never the max reach.
 * Tested in CodexTactics.Combat.WeaponRange.*.
 */
struct CODEXTACTICS_API FWeaponRangeFalloff
{
	/** Hit chance multiplier at the max reach (the far end of the falloff). */
	float FloorMultiplier = 0.1f;
	/** Shape of the falloff: 1 linear, 2 quadratic (steep right past the effective range). */
	float Exponent = 2.f;
};

namespace WeaponRangeRules
{
	/** The effective range clamped into (0, MaxCm]; 0 / negative / beyond the max = the max (no falloff). */
	CODEXTACTICS_API float ResolveEffectiveCm(float EffectiveCm, float MaxCm);

	/** The effective range stretched by a stance / elevation bonus (x Scale), never past the max reach. */
	CODEXTACTICS_API float ScaledEffectiveCm(float EffectiveCm, float MaxCm, float Scale);

	/** A shot at DistanceCm is allowed (within the max reach; MaxCm <= 0 = unlimited). */
	CODEXTACTICS_API bool IsWithinReach(float DistanceCm, float MaxCm);

	/**
	 * Hit chance multiplier at DistanceCm: 1 up to the effective range, then 1 - (1 - Floor) x t^Exponent with t = (d - E) / (M - E),
	 * Floor at the max reach, 0 beyond it.
	 */
	CODEXTACTICS_API float HitMultiplier(float DistanceCm, float EffectiveCm, float MaxCm, const FWeaponRangeFalloff& Falloff = FWeaponRangeFalloff());

	/** The falloff shared by every weapon (weapons_tuning.json "range_falloff"; defaults until it is read). */
	CODEXTACTICS_API const FWeaponRangeFalloff& GetFalloff();
	CODEXTACTICS_API void SetFalloff(const FWeaponRangeFalloff& Falloff);
}
