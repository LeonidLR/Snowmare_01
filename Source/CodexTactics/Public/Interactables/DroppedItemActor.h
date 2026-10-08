#pragma once

#include "CoreMinimal.h"
#include "Interactables/InteractableActor.h"
#include "Characters/TransferRules.h"
#include "DroppedItemActor.generated.h"

class UItemStashComponent;

/**
 * Sprint 13: a pile of inventory items lying on the ground (dropped from the inventory drawer onto the ground). A small
 * coloured placeholder box (ammo yellow, medicine red, food brown, engineering grey, matches orange) with an overhead
 * label ("Medkit x2"), snapped to the ground and the navmesh, no navigation / pawn blocking. Clicking it walks the
 * leader up (the usual interaction flow, planned in the tactical pause); on arrival he picks up as much as he can carry
 * (capacity clamp, the rest stays); the pile is destroyed when empty. Engineering items lie as items, never armed.
 * Contents live in a UItemStashComponent (the same storage as a supply crate). No Godot counterpart.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API ADroppedItemActor : public AInteractableActor
{
	GENERATED_BODY()

public:
	ADroppedItemActor();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual bool HandleDirectInteraction(AOperativeCharacter* Leader) override;
	virtual FActionMenuRequest BuildActionMenu(const AOperativeCharacter* Leader) const override;
	virtual bool GetOverheadLabel(FOverheadLabel& OutLabel) const override;
	virtual bool CanReceiveTrap() const override { return false; }

	UItemStashComponent* GetStash() const { return Stash; }

	/**
	 * Spawns a pile at Point snapped to the ground (downward trace) and projected onto the navmesh, or adds to a pile
	 * already lying within MergeRadius. Returns the pile (null on failure). Nothing is taken from anyone here.
	 */
	static ADroppedItemActor* SpawnOrMerge(UWorld* World, const FVector& Point, ETransferItem Item, int32 Count);

	/** Ground / navmesh point a pile dropped at Point lands on. */
	static FVector FindGroundPoint(UWorld* World, const FVector& Point);

	/** Piles within this distance of a new drop take it in, cm. */
	static constexpr float MergeRadius = 60.f;

	/** Label line ("Medkit x2", one line per item). */
	FString GetContentsText() const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Stash")
	TObjectPtr<UItemStashComponent> Stash;

	/** Half size of the placeholder box, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Stash", meta = (ClampMin = "5"))
	float PileHalfSize = 18.f;

private:
	void HandleStashChanged();
	void UpdateVisuals();
};
