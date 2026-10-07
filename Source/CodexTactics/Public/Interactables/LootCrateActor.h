#pragma once

#include "CoreMinimal.h"
#include "Interactables/InteractableActor.h"
#include "Interactables/LootRules.h"
#include "Characters/TransferRules.h"
#include "LootCrateActor.generated.h"

class UItemStashComponent;
class UMaterialInstanceDynamic;

/** Crate look (Godot loot_tier). */
UENUM(BlueprintType)
enum class ELootTier : uint8
{
	/** Blue army crate. */
	Standard,
	/** Golden stash behind riddles. */
	Maximal
};

/**
 * Supply crate. Clicking an intact crate makes the leader open the lid (no menu) and shows the loot dialog after
 * OpenSeconds; single items or everything go into the leader's supply. A trapped crate opens the defusal menu first
 * (2 s crouched work); a detonation wrecks the crate and burns everything inside. Enemies within 1.8 m set the wire
 * off. Crates can be pushed like barrels.
 * Sprint 13: two-way storage. Its items live in a UItemStashComponent (the same storage as a pile on the ground); the
 * authored FLootContents counts move into it on first use (bonus weapon / clothing stay in Contents). Operatives store
 * items by dropping a dragged inventory line on the crate or its open loot window, and take them out with a click
 * (whole stack, Godot rule: no carry limit) or by dragging a loot line onto an operative / portrait / the drawer
 * (capacity clamp). Stash capacity in units (Stash->Capacity, default 500).
 * Godot reference: Scenes/movements/loot_crate.gd, main.gd `_start_opening_crate`, `_on_action_confirmed` (branch 1),
 * `_on_loot_single_item_pressed`, `_on_loot_all_pressed`; Scenes/interactables/loot_crate.tscn (1.2 x 0.8 x 0.8 m).
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API ALootCrateActor : public AInteractableActor
{
	GENERATED_BODY()

public:
	ALootCrateActor();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool HandleDirectInteraction(AOperativeCharacter* Leader) override;
	virtual FActionMenuRequest BuildActionMenu(const AOperativeCharacter* Leader) const override;
	virtual void ExecuteAction(AOperativeCharacter* User) override;
	virtual void DetonateTrap(bool bByShot = false, const FText& InstigatorName = FText::GetEmpty()) override;
	virtual ETrapFlavor GetTrapFlavor() const override { return ETrapFlavor::Crate; }
	virtual bool CanReceiveTrap() const override { return !bTrapped && !bDestroyed; }

	/** Opens the lid: the loot dialog appears after OpenSeconds. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Loot")
	void StartOpening(AOperativeCharacter* Leader);

	/** Moves one stack into Collector's supply; returns the Godot result line («+2 🩹 Аптечка»), empty if none. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Loot")
	FText TakeItem(ELootItem Item, AOperativeCharacter* Collector);

	/** Takes everything. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Loot")
	void TakeAll(AOperativeCharacter* Collector);

	/** Everything inside: the stash merged with the authored contents not moved yet (Godot order). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Loot")
	TArray<FLootEntry> GetItems() const;

	/** Nothing left inside (also after a blast). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Loot")
	bool IsLooted() const;

	/** Sprint 13: units of a storable item inside (stash + authored contents). */
	int32 GetStoredCount(ETransferItem Item) const;

	/** Sprint 13: the storage with the authored contents moved in (store / take go through it). */
	UItemStashComponent* GetSyncedStash();

	UItemStashComponent* GetStash() const { return Stash; }

	/** Sprint 13: can items be put in now (not wrecked, not trapped). */
	bool CanStore() const { return !bDestroyed && !bTrapped; }

	/** Sprint 13 storage (shared item-container logic). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Loot")
	TObjectPtr<UItemStashComponent> Stash;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Loot")
	bool IsDestroyed() const { return bDestroyed; }

	/** Save-game load (Godot _deserialize_world_state crates): looted -> empty, defused -> no trap, destroyed -> gone. */
	void RestoreSaved(bool bInLooted, bool bInDefused, bool bInDestroyed);

	/** «📦 Армейский ящик снабжения» (Godot crate_name). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Loot")
	FText CrateName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Loot")
	ELootTier Tier = ELootTier::Standard;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Loot")
	FLootContents Contents;

	/** Lid opening before the dialog, s (Godot timer 1.8). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Loot", meta = (ClampMin = "0"))
	float OpenSeconds = 1.8f;

	/** Crouched defusal work, s (Godot timer 2.0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Loot", meta = (ClampMin = "0"))
	float DefuseSeconds = 2.f;

	/** Enemy distance that sets the tripwire off, cm (Godot 1.8 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Loot", meta = (ClampMin = "0"))
	float TrapContactDistance = 180.f;

	/** Blueprint hook for the lid animation / sound. */
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Loot", meta = (DisplayName = "On Opening"))
	void ReceiveOpening();

private:
	void UpdateVisuals();
	/** Moves the authored storable counts of Contents into the stash (idempotent; smokes may refill Contents). */
	void SyncContentsIntoStash();
	void FinishOpening(TWeakObjectPtr<AOperativeCharacter> WeakLeader);
	void FinishDefusal(TWeakObjectPtr<AOperativeCharacter> WeakUser);

	UPROPERTY(VisibleInstanceOnly, Category = "CodexTactics|Loot")
	bool bDestroyed = false;
};
