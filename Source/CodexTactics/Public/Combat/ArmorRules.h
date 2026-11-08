#pragma once

#include "CoreMinimal.h"
#include "Data/CombatTypes.h"

class FJsonObject;

/**
 * Armour of bodies and the penetration class rule (weapons framework step 2; game designer decision 1, 2026-11-08;
 * docs/design/WEAPONS_FRAMEWORK.md sections 4 / 11 / 12). UE-only on top of Godot enemy_base.gd take_damage (the multiplicative
 * armour formula and the armour shred stay Godot's).
 *
 * Every body has one armour class and one armour reduction fraction. Data: Content/Data/Weapons/weapons_tuning.json root block
 * "armor": {"bodies": {"<key>": {"class": "light", "reduction": 0.10}, ...}} (keys: BodyKey); a body without an entry keeps the C++
 * fallback (UHealthComponent default, EnemyAIRules::GetBaseArmor, the archetype's EArmorTier). Tested in CodexTactics.Weapons.Armour.*.
 */
struct CODEXTACTICS_API FBodyArmor
{
	EArmorClass Class = EArmorClass::Light;
	/** Share of the damage the armour stops, [0, 1). */
	float Reduction = 0.10f;
};

namespace ArmorRules
{
	/** The weapon's armour penetration applies when its class is at least the target's (decision 1). */
	CODEXTACTICS_API bool PenetrationApplies(EArmorClass WeaponClass, EArmorClass TargetClass);

	/**
	 * The armour reduction a hit meets: BaseReduction (x 0.3 while shredded, Godot armor_shred), then x (1 - Penetration) only when
	 * PenetrationApplies. Same operations as the pre-class UHealthComponent::TakeDamage (bit-identical when the class rule passes).
	 */
	CODEXTACTICS_API float EffectiveReduction(float BaseReduction, bool bShredded, float Penetration, EArmorClass WeaponClass, EArmorClass TargetClass);

	/** Godot ArmorTier -> class (Light / Medium / Heavy). */
	CODEXTACTICS_API EArmorClass ClassFromTier(EArmorTier Tier);

	/** The data key of an enemy archetype ("frost_hound", "brute", ...); operatives and PMC fighters use "operative". */
	CODEXTACTICS_API FString BodyKey(EEnemyArchetype Archetype);
	inline constexpr const TCHAR* OperativeBodyKey = TEXT("operative");

	/**
	 * Parses the "armor" block (its "bodies" object) into OutBodies. Invalid entries (unknown class name, reduction outside [0, 1) or not
	 * a number) are skipped and reported in OutErrors. Returns the number of bodies read.
	 */
	CODEXTACTICS_API int32 ParseBodies(const FJsonObject& ArmorBlock, TMap<FString, FBodyArmor>& OutBodies, TArray<FString>* OutErrors = nullptr);

	/** The table in force (set by WeaponTuning::ApplyFile at StartPlay). */
	CODEXTACTICS_API void SetBodies(const TMap<FString, FBodyArmor>& Bodies);
	CODEXTACTICS_API const TMap<FString, FBodyArmor>& GetBodies();

	/** InOut <- the table's entry for Key when there is one (returns true); else InOut stays (the fallback). */
	CODEXTACTICS_API bool ApplyBody(const FString& Key, FBodyArmor& InOut);
}
