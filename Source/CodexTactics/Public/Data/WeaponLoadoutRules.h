#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/**
 * Per-role starting weapons of the player's squad (the game designer's decision 21+, 2026-11-03 / confirmed 2026-11-08: the ENGINEER
 * carries the D-1 shotgun as his primary weapon and no M16). Data: Content/Data/Weapons/weapons_tuning.json root block
 *   "loadouts": {"commander": ["m16", "pistol", "grenade"], "engineer": ["shotgun", "pistol", "grenade"], ...}
 * Keys are the EOperativeRole names in snake case (commander, engineer, medic_sapper, recruit). A role without an entry keeps the game
 * mode's StartingArsenal + its ExtraWeapons (the Blueprint data). The first weapon is the one in hands (the primary weapon).
 * PMC fighters do not use it (their weapon_pool, pmc_operators.json). UE-only. Tested in CodexTactics.Weapons.Loadout.*.
 */
namespace WeaponLoadoutRules
{
	/** Reads the "loadouts" block (role key -> weapon ids, order kept); non-string entries are skipped into OutErrors. */
	CODEXTACTICS_API TMap<FString, TArray<FString>> Parse(const FJsonObject& LoadoutsBlock, TArray<FString>* OutErrors = nullptr);

	/** The table in force (WeaponTuning::ApplyFile sets it; empty = every role keeps the Blueprint loadout). */
	CODEXTACTICS_API void Set(const TMap<FString, TArray<FString>>& Loadouts);
	CODEXTACTICS_API const TMap<FString, TArray<FString>>& Get();

	/** The role's weapon ids, or false (keep the Blueprint loadout). Out-of-play ids (WeaponRoster) are dropped. */
	CODEXTACTICS_API bool Find(const FString& RoleKey, TArray<FString>& OutWeaponIds);

	/** "MedicSapper" -> "medic_sapper" (an EOperativeRole name to its data key). */
	CODEXTACTICS_API FString RoleKey(const FString& RoleEnumName);

	/** /Game/Data/Weapons/DA_Weapon_<id>.DA_Weapon_<id> */
	CODEXTACTICS_API FString WeaponAssetPath(const FString& WeaponId);

	/**
	 * The weapon to hold after a save / the campaign carry when the saved one is not in the arsenal (e.g. an old save of the Engineer with
	 * the M16): Saved if owned, else the primary (the first non-grenade weapon), else the first weapon, else empty.
	 */
	CODEXTACTICS_API FString ResolveHeldWeapon(const FString& Saved, const TArray<FString>& Arsenal);
}
