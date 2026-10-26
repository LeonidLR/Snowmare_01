#pragma once

#include "CoreMinimal.h"
#include "Characters/TransferRules.h"
#include "Interactables/LootRules.h"

/**
 * Pure rules of a fallen operative's remains (play-test bug 2026-10-26: "the fight is over, one of my fighters is down, but I can't walk
 * up and take his equipment"). Godot reference: Scenes/movements/player.gd _die (the body stays a loot container, its own collision is
 * the click target), get_items_list / loot_single_item (ammo of the weapon in hand, medkits, food, grenades, the weapon), main.gd
 * _handle_expendable_member_death. UE: AOperativeCharacter::SpawnRemainsLoot, ALootCrateActor::BindRemainsBody.
 */
namespace CorpseLootRules
{
	/** The click box of the remains: an oriented box round the lying body (world centre, half extents along its yaw). */
	struct FRemainsBox
	{
		bool bValid = false;
		FVector Center = FVector::ZeroVector;
		FVector Extent = FVector::ZeroVector;
		float Yaw = 0.f;
	};

	/** Margin round the bones (the flesh round a bone, a hand-sized slack for the cursor), cm. */
	inline constexpr float BodyPaddingCm = 20.f;
	/** Smallest half height of the box (a body lies ~25 cm high; the cursor must still meet it from a steep camera), cm. */
	inline constexpr float MinHalfHeightCm = 25.f;
	/** Smallest half length / width (a curled-up body), cm. */
	inline constexpr float MinHalfLengthCm = 40.f;
	inline constexpr float MinHalfWidthCm = 30.f;

	/**
	 * The box round the body's bone points: its long axis is the points' main direction on the ground (2D principal axis), its sides the
	 * points' spread along / across it plus PaddingCm, its bottom PaddingCm / 2 below the lowest point (the snow surface), at least
	 * the minimum sizes. Invalid without points.
	 */
	CODEXTACTICS_API FRemainsBox FitBoxToBody(TConstArrayView<FVector> Points, float PaddingCm = BodyPaddingCm);

	/**
	 * A bone of the body itself: not the root, the IK / virtual / attach helpers (ik_*, VB *, attach*, interaction, center_of_mass) - the
	 * death clips leave those at the reference pose (an ik_hand bone hangs ~1 m above the snow next to a lying body).
	 */
	CODEXTACTICS_API bool IsBodyBone(const FString& BoneName);

	/** What a fallen operative carried (counts of every hand-over item: ammo = the clip + the reserve of that weapon). */
	struct FCarriedGear
	{
		TMap<ETransferItem, int32> Items;
		int32 Grenades = 0;
		/** The firearm in his hands (Godot current_weapon_data); empty / grenade / knife = none. */
		FString WeaponId;
	};

	/** A weapon id the remains offer as an item: a firearm (not the grenade, the knife or nothing). */
	CODEXTACTICS_API bool IsLootableWeapon(const FString& WeaponId);

	/**
	 * A carried item the remains hold - user decision 2026-10-26 ("trim the remains to the Godot list", player.gd get_items_list): ammo
	 * (a weapon's rounds / fuel: clip + reserve), medkits, food (canned food, bread, chocolate). Not the matches, heat packs, firewood,
	 * flares or the set-up items (mines, barricades, turrets) - those stay with the body. The grenades and the weapon come separately.
	 */
	CODEXTACTICS_API bool IsRemainsItem(ETransferItem Item);

	/**
	 * The remains' contents: what he carried of the Godot list (IsRemainsItem; no crate defaults), the grenades and his firearm (Godot
	 * get_items_list).
	 */
	CODEXTACTICS_API FLootContents BuildRemainsContents(const FCarriedGear& Gear);

	/** "Remains: <name>" (the crate name, glossary «Останки»). */
	CODEXTACTICS_API FText GetRemainsName(const FText& OperativeName);

	/** The label over the body (Godot CorpseLootPrompt): "[Click] Search the body: <name>" / "[Searched] Body: <name> (empty)". */
	CODEXTACTICS_API FString GetSearchPrompt(const FText& OperativeName, bool bEmpty);
}
