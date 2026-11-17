#pragma once

#include "CoreMinimal.h"
#include "Misc/Optional.h"

class FJsonObject;
class FJsonValue;
struct FLevelPropsObject;
struct FLootContents;

/**
 * Which REAL game object a levelgen prop becomes on a live map (G003b, designer 2026-11-17: the generator is tested with all
 * assets - barrels explode, mines trigger, loot is picked up, turrets fire). Pure: reads Content/Data/Balance/props_balance.json
 * "gameplay" (class keys -> native class paths, the reveal range, the loot rolls and the item map). No world, no UObjects
 * (the loot contents are a plain USTRUCT). Godot reference: none (UE-only level authoring). Tests: CodexTactics.LevelGen.Props.Gameplay.*.
 */
namespace PropsGameplayRules
{
	/**
	 * The gameplay class key of an object: "crate_container" for a crate holding loot or a crate-mine (bContainer), a mines-kind object
	 * by its trigger ("mine_tripwire" / "mine_proximity"; no trigger = the variant), anything else = its variant.
	 */
	CODEXTACTICS_API FString ClassKeyFor(const FString& LayerKind, const FLevelPropsObject& Object, bool bContainer);

	/** gameplay.classes[Key] (a native class path such as /Script/CodexTactics.BarrelActor); empty = none (greybox fallback). */
	CODEXTACTICS_API FString ClassPathFor(const FJsonObject* Balance, const FString& Key);

	/** gameplay.reveal_cm: the range a hidden mine / wire is spotted at (default 300, designer #4). */
	CODEXTACTICS_API float RevealCm(const FJsonObject* Balance);

	/** gameplay.spawn_frame_budget_ms (default 20). */
	CODEXTACTICS_API double SpawnFrameBudgetMs(const FJsonObject* Balance);

	/** gameplay.loot.mined_crate_table (default "ammo_common"). */
	CODEXTACTICS_API FString MinedCrateTable(const FJsonObject* Balance);

	/**
	 * Deterministic loot of one object: gameplay.loot.crate_rolls / pile_rolls weighted draws from loot_tables[Table].entries (keys
	 * sorted, an FPropsRng seeded by Fnv1a(ObjectId)), each drawn item adding its gameplay.loot.items[item].crate / .pile counts.
	 * A pile draws only items that have a "pile" entry. Returns field / item name -> count (crate: FLootContents fields; pile:
	 * ETransferItem names). Unknown table = empty.
	 */
	CODEXTACTICS_API TMap<FString, int32> RollLoot(const FJsonObject* Balance, const FString& Table, const FString& ObjectId, bool bPile);

	/**
	 * Sets every int field of Contents to 0, then adds Fields (FLootContents property names, e.g. RifleAmmo, Medkits). Returns the
	 * names it did not know (left out).
	 */
	CODEXTACTICS_API TArray<FString> ApplyToContents(const TMap<FString, int32>& Fields, FLootContents& Contents);

	/** A level turret's side (mirrors ETurretFaction; this header stays free of actor types). */
	enum class ETurretSide : uint8
	{
		Hostile,
		Neutral,
		Friendly
	};

	/** "hostile" / "neutral" / "friendly" (case-insensitive); false for anything else (Out unchanged). */
	CODEXTACTICS_API bool ParseTurretSide(const FString& Text, ETurretSide& Out);

	/** One level turret of a map: its id and the fields its props object states (faction / powered; unset = the map's rule). */
	struct FTurretInput
	{
		FString Id;
		TOptional<ETurretSide> Side;
		TOptional<bool> bPowered;
	};

	/** What a level turret starts as. */
	struct FTurretSetup
	{
		ETurretSide Side = ETurretSide::Hostile;
		bool bPowered = false;
	};

	/** The turret fields of a props object (Extra "faction" string, "powered" bool); a bad value counts as unset. */
	CODEXTACTICS_API FTurretInput TurretInputOf(const FLevelPropsObject& Object);

	/**
	 * The share of a map's turrets that start powered (designer 2026-11-17, default 0.25): the props file's top-level
	 * "turret_powered_share" if it is a number, else gameplay.turrets.powered_share, else 0.25; clamped to [0, 1].
	 */
	CODEXTACTICS_API double TurretPoweredShare(const FJsonObject* Balance, const TMap<FString, TSharedPtr<FJsonValue>>* FileExtra);

	/**
	 * Start state of every level turret of a map. A turret's own "powered" / "faction" win. Of the rest, the first
	 * K = clamp(round(Share * Turrets.Num()) - explicitly powered, 0, unset) in (Fnv1a(id), id) order start powered with
	 * gameplay.turrets.powered_faction (default hostile); the others stay unpowered (inactive) with gameplay.turrets.default_faction
	 * (default hostile). Deterministic, independent of the input order. Keyed by id.
	 */
	CODEXTACTICS_API TMap<FString, FTurretSetup> ResolveTurrets(const FJsonObject* Balance, double Share, const TArray<FTurretInput>& Turrets);

	/** gameplay.traps_faction: true (default "hostile") = level mines / wires are the enemy's (hidden, trip on operatives); "squad" / "friendly" = false. */
	CODEXTACTICS_API bool AreTrapsHostile(const FJsonObject* Balance);
}
