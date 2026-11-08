#pragma once

#include "CoreMinimal.h"
#include "Data/WeaponStats.h"

class FJsonObject;

/**
 * Pure logic of the unified weapon schema (docs/design/WEAPONS_FRAMEWORK.md): one weapon's JSON object
 * (Content/Data/Weapons/weapons_tuning.json "weapons.<id>") <-> FWeaponStats, and the validation of a weapon's data.
 * Godot reference: resources/weapon_data.gd (the common core keys keep its names). Tested in CodexTactics.Weapons.Schema.*.
 *
 * JSON units are metres / seconds; FWeaponStats holds centimetres / seconds. A key missing from the object keeps the value
 * already in the stats (the asset's own value), so a file may list only what it tunes.
 */
namespace WeaponStatsRules
{
	/** The file's schema version this code writes (root key "schema_version"; a file without it is version 1). */
	inline constexpr int32 SchemaVersion = 2;

	/**
	 * Applies a weapon's JSON object onto InOut. Unknown keys go to OutUnknown (a typo guard; not an error); a value of the wrong type
	 * or an unknown enum name goes to OutErrors and leaves that field unchanged. Returns true when OutErrors got nothing new.
	 * Does NOT validate the result: call Validate.
	 */
	CODEXTACTICS_API bool ApplyJson(const FJsonObject& Json, FWeaponStats& InOut, TArray<FString>* OutUnknown = nullptr, TArray<FString>* OutErrors = nullptr);

	/** Every key the schema knows at the top level of a weapon object (ApplyJson's typo guard; the dump writes all of them). */
	CODEXTACTICS_API const TArray<FString>& KnownKeys();

	/**
	 * Stats as a weapon's JSON object (every key; 4 decimals, metres). bReserved also writes the reserved keys / per-kind blocks
	 * (the dump does; a round trip through ApplyJson gives the same stats).
	 */
	CODEXTACTICS_API TSharedRef<FJsonObject> ToJson(const FWeaponStats& Stats, bool bReserved = true);

	/**
	 * Checks a weapon's data; appends one readable line per problem to OutErrors (prefixed with WeaponId when given). Rules: no
	 * negative damage / range / time; effective range <= max range; min range below the effective range; shot interval > 0;
	 * armour penetration in [0, 1]; a weapon that uses ammo has a magazine >= 1 and a reload > 0; reserve >= -1; a melee weapon uses
	 * no ammo; hit chances in [0, 1]; grid reach >= 1 cell; a status effect has a duration; pellets >= 1, cone < 45 deg; a Projectile
	 * has a speed; the full-damage blast radius within the blast radius. Returns true when nothing was found.
	 */
	CODEXTACTICS_API bool Validate(const FWeaponStats& Stats, TArray<FString>& OutErrors, const FString& WeaponId = FString());

	/** Enum <-> JSON name (lower snake case: "hitscan", "sniper_rifle", "armor_shred", "melee_adj", ...). */
	CODEXTACTICS_API FString KindName(EWeaponKind Kind);
	CODEXTACTICS_API bool ParseKind(const FString& Name, EWeaponKind& Out);
	CODEXTACTICS_API FString ArmorClassName(EArmorClass Class);
	CODEXTACTICS_API bool ParseArmorClass(const FString& Name, EArmorClass& Out);
}
