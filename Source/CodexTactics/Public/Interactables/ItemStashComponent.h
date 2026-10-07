#pragma once

#include "CoreMinimal.h"
#include "Characters/TransferRules.h"
#include "Components/ActorComponent.h"
#include "Interactables/LootRules.h"
#include "ItemStashComponent.generated.h"

/**
 * Sprint 13: a container of inventory items (ETransferItem -> quantity) shared by every item store in the world: a
 * pile dropped on the ground (ADroppedItemActor) and a supply crate (ALootCrateActor, two-way storage). Capacity is
 * counted in units (rounds / pieces) over all items; 0 = unlimited. Engineering items are stored as items, never armed.
 * No Godot counterpart (Godot crates were take-only, nothing could be dropped).
 */
UCLASS(ClassGroup = (CodexTactics), meta = (BlueprintSpawnableComponent))
class CODEXTACTICS_API UItemStashComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UItemStashComponent();

	/** Total units the stash holds (all items together); 0 = unlimited. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Stash", meta = (ClampMin = "0"))
	int32 Capacity = 0;

	int32 GetCount(ETransferItem Item) const;

	/** Units of every item together. */
	int32 GetTotal() const;

	bool IsEmpty() const { return GetTotal() <= 0; }

	/** Units that still fit (MAX_int32 when unlimited). */
	int32 GetFreeSpace() const;

	/** Puts up to Count in (clamped by the free space unless bIgnoreCapacity); returns how many went in. */
	int32 Add(ETransferItem Item, int32 Count, bool bIgnoreCapacity = false);

	/** Removes up to Max; returns how many came out. */
	int32 Take(ETransferItem Item, int32 Max);

	void Clear();

	/** Replaces the contents (save-game load). */
	void SetContents(const TMap<ETransferItem, int32>& InItems);

	const TMap<ETransferItem, int32>& GetItems() const { return Items; }

	/** Items with a positive count in ETransferItem order. */
	TArray<ETransferItem> GetItemTypes() const;

	/** Bumped on every change (UI refresh). */
	int32 GetRevision() const { return Revision; }

	/** Fired after every change. */
	FSimpleMulticastDelegate OnChanged;

private:
	void Changed();

	UPROPERTY(VisibleInstanceOnly, Category = "CodexTactics|Stash")
	TMap<ETransferItem, int32> Items;

	int32 Revision = 0;
};

namespace ItemStash
{
	/** The loot-dialog kind of a stash item (every ETransferItem has one). */
	CODEXTACTICS_API ELootItem ToLootItem(ETransferItem Item);

	/** The stash item of a loot kind; false for the bonus weapon / clothing (not storable). */
	CODEXTACTICS_API bool FromLootItem(ELootItem Loot, ETransferItem& OutItem);
}
