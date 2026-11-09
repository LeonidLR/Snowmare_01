#pragma once

#include "CoreMinimal.h"
#include "Data/WeaponStats.h"

/**
 * The RPG-7's pure rules (the weapons framework, 2026-11-09; the game designer's numbers the same day; docs/systems/weapons.md "The RPG-7";
 * new - no Godot reference): the one launch refusal left (a target closer than the minimum range - at least past the rocket's own blast, so the
 * shooter never catches his own splash), the damage split (a direct hit = direct_hit_damage on the actor struck, the splash base_damage on
 * everyone else in the blast) and the miss scatter. The designer removed the back-blast damage and the wall-behind / closed-room / prone
 * refusals (2026-11-09). The world part is AOperativeCharacter::FireLauncherAt (OperativeCharacterLauncher.cpp) on the shared projectile layer.
 * UE-only. Tested in CodexTactics.Weapons.Rpg.*.
 */

/** Why a launcher shot did not go. */
enum class ELauncherRefusal : uint8
{
	None,
	/** The target is closer than the minimum range (the shooter would be inside his own blast). */
	TooClose,
	/** No rocket loaded. */
	Empty
};

namespace LauncherRules
{
	/** The nearest target worth a rocket, cm: min_range_m, never inside the rocket's own blast radius (nor its arming distance). */
	CODEXTACTICS_API float MinimumRangeCm(const FWeaponStats& Stats);

	/** The shot's refusal: empty, then too close. */
	CODEXTACTICS_API ELauncherRefusal Refusal(bool bLoaded, float TargetDistanceCm, const FWeaponStats& Stats);

	/**
	 * The damage a body takes from a rocket bursting DistanceCm from it before its armour: the actor struck takes direct_hit_damage as its whole
	 * share (bStruck; the blast leaves it out), everyone else the splash base_damage x the blast falloff (0 outside the radius).
	 */
	CODEXTACTICS_API float DamageOn(const FWeaponStats& Stats, bool bStruck, float DistanceCm);

	/**
	 * Where a missed rocket goes: the aim point pushed sideways (along Right) and along the line of fire by Roll01 in [0, 1) - between
	 * MinScatterCm and MaxScatterCm off the aim point, left or right by SideRoll < 0.5.
	 */
	CODEXTACTICS_API FVector MissPoint(const FVector& AimPoint, const FVector& AimDir, float Roll01, float SideRoll);

	constexpr float MinScatterCm = 150.f;
	constexpr float MaxScatterCm = 350.f;

	/** The feed / floating text for a refusal ("" for None). */
	CODEXTACTICS_API FString RefusalText(ELauncherRefusal Refusal);
}
