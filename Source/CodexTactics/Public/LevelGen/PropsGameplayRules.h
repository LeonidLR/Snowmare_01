#pragma once

#include "CoreMinimal.h"

class FJsonObject;
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
}
