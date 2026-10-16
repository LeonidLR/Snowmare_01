#pragma once

#include "CoreMinimal.h"

/**
 * Pure bookkeeping of UEnemyRegistrySubsystem (audit 2026-10-11 #2 / P2-P3, UE-only; Godot iterated the "enemies"
 * group with get_tree().get_nodes_in_group, the UE equivalent is a registered list instead of a world actor scan).
 */
namespace EnemyRegistryRules
{
	/**
	 * Ordered set: Add appends once (a second Add of the same item keeps its first place), Remove keeps the order of the
	 * rest. Iteration order = registration order (BeginPlay order of the enemies = their spawn order).
	 */
	template <typename ElementType>
	struct TOrderedRegistry
	{
		/** False when the item is already registered. */
		bool Add(const ElementType& Item)
		{
			if (Items.Contains(Item))
			{
				return false;
			}
			Items.Add(Item);
			return true;
		}

		/** False when the item was not registered. */
		bool Remove(const ElementType& Item)
		{
			return Items.RemoveSingle(Item) > 0; // stable: the others keep their order
		}

		bool Contains(const ElementType& Item) const
		{
			return Items.Contains(Item);
		}

		/** Drops every item the predicate rejects (stale weak pointers), keeping the order; returns how many went. */
		template <typename PredicateType>
		int32 RemoveAllBut(PredicateType Keep)
		{
			return Items.RemoveAll([&Keep](const ElementType& Item) { return !Keep(Item); });
		}

		int32 Num() const
		{
			return Items.Num();
		}

		const TArray<ElementType>& GetItems() const
		{
			return Items;
		}

	private:
		TArray<ElementType> Items;
	};
}
