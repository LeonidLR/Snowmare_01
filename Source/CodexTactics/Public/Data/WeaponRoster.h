#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/**
 * The game designer's weapon roster (decision 2026-11-08: "The only weapons in the game are the ones I name"). UE-only; the Godot
 * prototype's extra weapons (hammer, flamethrower, knife, cryo emitter, plasma carbine) are out of play.
 * Data: Content/Data/Weapons/weapons_tuning.json root block
 *   "roster": {"in_play": ["m16"], "next": ["shotgun", "pistol", "sniper_rifle", "rpg7"], "out": ["hammer", ...]}
 * "in_play" / "next" document the plan (docs/design/WEAPONS_FRAMEWORK.md "Roster"); the runtime gate is "out": an out-of-play weapon
 * never enters an arsenal (squad, PMC fighters), cannot be switched to, is no loot, its ammo is no item, and a save that holds one
 * loads without it (the M16 in hands). Their DA_Weapon_* assets stay on disk, unreferenced. Tested in CodexTactics.Weapons.Roster.*.
 */
struct CODEXTACTICS_API FWeaponRoster
{
	TArray<FString> InPlay;
	TArray<FString> Next;
	TArray<FString> Out;
};

namespace WeaponRoster
{
	/** Reads the "roster" block; a missing list stays empty, a non-string entry goes to OutErrors. */
	CODEXTACTICS_API bool Parse(const FJsonObject& RosterBlock, FWeaponRoster& OutRoster, TArray<FString>* OutErrors = nullptr);

	/** The roster in force (WeaponTuning::ApplyFile sets it; read lazily from the project file before that). */
	CODEXTACTICS_API void Set(const FWeaponRoster& Roster);
	CODEXTACTICS_API const FWeaponRoster& Get();

	/** The weapon is out of play (the designer removed it). Empty ids are not. */
	CODEXTACTICS_API bool IsOutOfPlay(const FString& WeaponId);

	/** The weapon a holder of an out-of-play weapon gets in hands instead: the first in-play weapon ("m16"). */
	CODEXTACTICS_API FString FallbackWeaponId();
}
