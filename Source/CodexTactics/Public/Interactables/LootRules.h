#pragma once

#include "CoreMinimal.h"
#include "LootRules.generated.h"

/** Loot item kinds of a supply crate (Godot loot_crate.gd keys, same order as get_items_list). */
UENUM(BlueprintType)
enum class ELootItem : uint8
{
	Medkit,
	CannedFood,
	Bread,
	Chocolate,
	Matches,
	RifleAmmo,
	PistolAmmo,
	ShotgunAmmo,
	FlameFuel,
	CryoAmmo,
	PlasmaAmmo,
	Turret,
	Barricade,
	Mine,
	BonusWeapon,
	BonusClothing
};

/** One line of the loot dialog. */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FLootEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Loot")
	ELootItem Item = ELootItem::Medkit;

	/** «Аптечка», «Патроны 5.56 мм (M16)», «Оружие (id)»… */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Loot")
	FText Name;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Loot")
	FString Icon;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Loot")
	int32 Count = 0;

	/** «шт.» / «ед.» */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Loot")
	FText Unit;

	/** «🩹 Аптечка: 2 шт.» (Godot button text). */
	FText GetLabel() const;
};

/**
 * Contents of a supply crate, editable per crate. Defaults are the Godot loot_crate.gd export defaults.
 * Godot reference: Scenes/movements/loot_crate.gd (get_items_list, is_empty_crate, loot_single_item, detonate_trap).
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FLootContents
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Provisions")
	int32 Medkits = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Provisions")
	int32 CannedFood = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Provisions")
	int32 Bread = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Provisions")
	int32 Chocolate = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Provisions")
	int32 Matches = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ammo")
	int32 RifleAmmo = 60;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ammo")
	int32 PistolAmmo = 24;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ammo")
	int32 ShotgunAmmo = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ammo")
	int32 FlameFuel = 50;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ammo")
	int32 CryoAmmo = 30;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ammo")
	int32 PlasmaAmmo = 20;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engineering")
	int32 Turrets = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engineering")
	int32 Barricades = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engineering")
	int32 Mines = 0;

	/** Extra weapon id (e.g. "plasma_carbine"), empty for none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Special")
	FString BonusWeaponId;

	/** Clothing id (future: "warm_parka"), empty for none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Special")
	FString BonusClothingId;

	/** Items still inside, in the Godot order. */
	TArray<FLootEntry> GetItems() const;

	bool IsEmpty() const;

	/** Count of one item (1 for a set bonus id). */
	int32 GetCount(ELootItem Item) const;

	/** Removes the whole stack of Item and returns how many were taken (0 if none). */
	int32 Take(ELootItem Item);

	/** Adds Count to a counted item (no-op for the bonus weapon / clothing). Sprint 13: the crate's merged view. */
	void AddCount(ELootItem Item, int32 Count);

	/** Everything burns (trap detonation). */
	void DestroyAll();
};
