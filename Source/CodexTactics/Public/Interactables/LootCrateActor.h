#pragma once

#include "CoreMinimal.h"
#include "Interactables/InteractableActor.h"
#include "Interactables/LootRules.h"
#include "LootCrateActor.generated.h"

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

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Loot")
	TArray<FLootEntry> GetItems() const { return Contents.GetItems(); }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Loot")
	bool IsLooted() const { return bLooted; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Loot")
	bool IsDestroyed() const { return bDestroyed; }

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
	void FinishOpening(TWeakObjectPtr<AOperativeCharacter> WeakLeader);
	void FinishDefusal(TWeakObjectPtr<AOperativeCharacter> WeakUser);

	UPROPERTY(VisibleInstanceOnly, Category = "CodexTactics|Loot")
	bool bLooted = false;

	UPROPERTY(VisibleInstanceOnly, Category = "CodexTactics|Loot")
	bool bDestroyed = false;
};
