#pragma once

#include "CoreMinimal.h"
#include "Blueprint/DragDropOperation.h"
#include "Characters/TransferRules.h"
#include "InventoryDragDropOperation.generated.h"

class AOperativeCharacter;

/**
 * Sprint 13: an item line being dragged. From the inventory drawer (Sender set): onto a squad mate (model / portrait),
 * a crate (model / open loot window) or the ground. From a crate's loot window (Container set): onto an operative
 * (model / portrait) or the drawer (the leader). Available = the count at drag start (the drop re-reads the live one).
 * No Godot counterpart (Godot used a transfer dialog + click mode).
 */
UCLASS()
class CODEXTACTICS_API UInventoryDragDropOperation : public UDragDropOperation
{
	GENERATED_BODY()

public:
	UPROPERTY()
	ETransferItem Item = ETransferItem::Medkit;

	/** The operative whose inventory the line belongs to (the leader when the drag started). */
	UPROPERTY()
	TWeakObjectPtr<AOperativeCharacter> Sender;

	/** Take-out drag: the crate / pile the line belongs to (null for an inventory drag). */
	UPROPERTY()
	TWeakObjectPtr<AActor> Container;

	/** Units the sender (or the container) had at drag start. */
	UPROPERTY()
	int32 Available = 0;
};
