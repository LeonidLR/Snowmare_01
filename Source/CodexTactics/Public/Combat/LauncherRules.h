#pragma once

#include "CoreMinimal.h"
#include "Data/WeaponStats.h"

enum class EOperativeStance : uint8;

/**
 * The RPG-7's pure rules (the weapons framework, 2026-11-09; docs/systems/weapons.md "The RPG-7"; new - no Godot reference): the stances it
 * fires from, the back-blast check (a wall right behind the launcher / a closed room), the back-blast's damage on a body in the cone behind,
 * the minimum range (the warhead arms at its arming distance) and the miss scatter. The world part is AOperativeCharacter::FireLauncherAt
 * (OperativeCharacterLauncher.cpp) on the shared projectile layer. UE-only. Tested in CodexTactics.Weapons.Rpg.*.
 */

/** What the back-blast check found behind the launcher. */
enum class ELauncherBackblast : uint8
{
	/** The cone behind is open enough. */
	Clear,
	/** A wall closer than explosion.back_blast_clearance_m behind: the blast would come straight back. */
	WallBehind,
	/** A roof above and a wall within the back-blast length behind: a closed room. */
	ClosedRoom
};

/** Why a launcher shot did not go. */
enum class ELauncherRefusal : uint8
{
	None,
	/** Prone: the back-blast would hit his own legs (stand or kneel). */
	Prone,
	/** The back-blast is blocked (explosion.back_blast_refuses). */
	Backblast,
	/** The target is closer than the arming distance (the warhead would not go off). */
	TooClose,
	/** No rocket loaded. */
	Empty
};

namespace LauncherRules
{
	/** The launcher fires standing or kneeling (crouched), never prone. */
	CODEXTACTICS_API bool StanceAllows(EOperativeStance Stance);

	/**
	 * The back-blast check from the measured surroundings: WallBehindCm = the distance to the first wall along the cone's axis behind the
	 * launcher (< 0 = none within the back-blast length), bRoofAbove = a ceiling over the shooter. A wall inside the clearance = WallBehind;
	 * a roof with any wall inside the back-blast length = ClosedRoom; else Clear. No back-blast (length 0) = always Clear.
	 */
	CODEXTACTICS_API ELauncherBackblast ClassifyBackblast(float WallBehindCm, bool bRoofAbove, const FWeaponExplosionStats& Stats);

	/** The back-blast's damage on a body at Point (BackBlastDamage x the cone multiplier; 0 outside the cone). */
	CODEXTACTICS_API float BackblastDamageAt(const FWeaponExplosionStats& Stats, const FVector& Launcher, const FVector& AimDir, const FVector& Point);

	/** The nearest target worth a rocket: past the arming distance (and the weapon's min_range_m), cm. */
	CODEXTACTICS_API float MinimumRangeCm(const FWeaponStats& Stats);

	/** The shot's refusal, the checks in the player's order: empty, prone, too close, then the back-blast (when it refuses). */
	CODEXTACTICS_API ELauncherRefusal Refusal(bool bLoaded, EOperativeStance Stance, float TargetDistanceCm, ELauncherBackblast Backblast,
		const FWeaponStats& Stats);

	/**
	 * Where a missed rocket goes: the aim point pushed sideways (along Right) and along the line of fire by Roll01 in [0, 1) - between
	 * MinScatterCm and MaxScatterCm off the aim point, left or right by SideRoll < 0.5.
	 */
	CODEXTACTICS_API FVector MissPoint(const FVector& AimPoint, const FVector& AimDir, float Roll01, float SideRoll);

	constexpr float MinScatterCm = 150.f;
	constexpr float MaxScatterCm = 350.f;

	/** The feed / floating text for a refusal ("" for None). */
	CODEXTACTICS_API FString RefusalText(ELauncherRefusal Refusal, ELauncherBackblast Backblast);
}
