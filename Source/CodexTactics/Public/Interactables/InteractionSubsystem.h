#pragma once

#include "CoreMinimal.h"
#include "Interactables/ActionMenuTypes.h"
#include "Interactables/LootRules.h"
#include "GameFlow/GameFlowTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "InteractionSubsystem.generated.h"

class AInteractableActor;
class AOperativeCharacter;
class ALootCrateActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnActionMenuChanged, bool, bOpen, const FActionMenuSpec&, Menu);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLootDialogChanged, bool, bOpen, ALootCrateActor*, Crate);

/**
 * Object interaction flow: the squad leader walks to the clicked object; once within its InteractionDistance the
 * object's action menu opens (or a feed line is posted), and the menu buttons run the action / cancel.
 * In the tactical pause the approach is planned like any pause order and runs on release.
 * Use orders in a fight (user decision 2026-10-08): "Ignite" on a barrel in the real-time fight sends the operative to
 * light it (walk up, one match); in the tactical pause it is planned with a marker and runs on the release. Another move
 * order of that operative replaces it; turn-based combat drops it (UseOrderRules).
 * Godot reference: main.gd `pending_menu_target`, `_trigger_menu_for_object`, `_open_action_menu`,
 * `_on_action_confirmed`, `_on_action_cancelled`, `_close_action_menu`.
 */
UCLASS()
class CODEXTACTICS_API UInteractionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Sends the leader to Target (sprinting on a double click); the menu opens on arrival. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Interactables")
	bool RequestInteraction(AInteractableActor* Target, bool bSprint = false);

	/** Drops the pending approach and closes the menu (e.g. the player ordered a move elsewhere). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Interactables")
	void CancelInteraction();

	/** Godot _trigger_menu_for_object: opens Target's action menu for the leader at once (no walk-up). */
	void OpenMenuNow(AInteractableActor* Target);

	/**
	 * Game designer 2026-10-22 (a failed defuse: "clicking the mine again does nothing - I have to walk away and come back"): the live,
	 * known trap (a spotted mine / PMC wire - UsesDisarmApproach, not awaiting its pickup) Worker stands at the disarm spot of whose
	 * charge / wire line lies nearest to Point, within TrapApproachRules::RetryClickRadiusCm (TrapApproachRules::IsRetryClick); nullptr
	 * when none or when bPicksAnotherMember. The controller takes such a click as a click on the trap.
	 */
	AInteractableActor* FindRetryTrapAt(const FVector& Point, const AOperativeCharacter& Worker, bool bPicksAnotherMember) const;

	/** Confirm button: runs the object's action with the current leader. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Interactables")
	void ConfirmActionMenu();

	/** Relocate button ("Relocate" / "Push out"): placement mode for the menu object with the leader. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Interactables")
	void RelocateActionMenu();

	/** "Set trap" button: trap the menu object with one of the leader's grenades. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Interactables")
	void TrapActionMenu();

	/**
	 * Use order: Worker walks up to Target and runs its action (ExecuteAction: defuse a trap first, else e.g. light a
	 * barrel). Real-time fight: at once; tactical pause: planned (marker), run on the release; elsewhere: used at once.
	 * Replaces Worker's earlier use / planned move / planned relocation.
	 */
	void OrderUse(AOperativeCharacter* Worker, AInteractableActor* Target);

	/** Drops Worker's running or planned use order (another order for him; silent). */
	void CancelUseOrder(const AOperativeCharacter* Worker);

	/** Running / planned use orders (smokes). */
	int32 GetActiveUseOrderCount() const { return UseOrders.Num(); }
	int32 GetPlannedUseOrderCount() const { return PlannedUseOrders.Num(); }
	bool HasUseOrder(const AOperativeCharacter* Worker) const;

	/** Cancel / close button. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Interactables")
	void CancelActionMenu();

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Interactables")
	AInteractableActor* GetPendingInteraction() const { return Pending.Get(); }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Interactables")
	bool IsActionMenuOpen() const { return bMenuOpen && MenuTarget.IsValid(); }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Interactables")
	AInteractableActor* GetMenuTarget() const { return MenuTarget.Get(); }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Interactables")
	const FActionMenuSpec& GetActionMenu() const { return Menu; }

	/** Shows the loot dialog of an opened crate (Godot _open_loot_dialog). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Loot")
	void OpenLootDialog(ALootCrateActor* Crate);

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Loot")
	void CloseLootDialog();

	/**
	 * Loot button of one item (Godot _on_loot_single_item_pressed). Rev 2026-10-19: the leader searches the crate (chest-search clip)
	 * and the stack moves into his kit at the clip's grab moment (HandleSearchFinished); refused while the window is not usable.
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Loot")
	void LootItem(ELootItem Item);

	/** "Take ALL" (Godot _on_loot_all_pressed): the same search; everything moves at the grab and the window closes. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Loot")
	void LootAll();

	/** The crate's search reached its grab moment (Collector null: interrupted, nothing moved): feed line, window refresh / close. */
	void HandleSearchFinished(ALootCrateActor* Crate, AOperativeCharacter* Collector, bool bAll, const FText& Taken);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Loot")
	ALootCrateActor* GetLootCrate() const { return LootCrate.Get(); }

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Loot")
	FOnLootDialogChanged OnLootDialogChanged;

	/** Menu opened / closed (the HUD widget listens). */
	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Interactables")
	FOnActionMenuChanged OnActionMenuChanged;

protected:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

private:
	struct FUseOrder
	{
		TWeakObjectPtr<AOperativeCharacter> Worker;
		TWeakObjectPtr<AInteractableActor> Target;
		/** Where he was sent and the goal his move request reported (another order moves it away from both). */
		FVector Approach = FVector::ZeroVector;
		FVector IssuedGoal = FVector::ZeroVector;
		float Elapsed = 0.f;
		float RetryTime = 0.f;
	};

	/** Starts walking Worker to Target, or uses it at once when he is in reach. */
	void StartUse(AOperativeCharacter* Worker, AInteractableActor* Target);
	/** Faces Target and runs its action with Worker. */
	void UseNow(AOperativeCharacter& Worker, AInteractableActor& Target) const;
	void TickUseOrders(float DeltaTime);
	void PostLine(const FText& Speaker, const FText& Text) const;

	UFUNCTION()
	void HandlePauseReleased();

	UFUNCTION()
	void HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode);

	TArray<FUseOrder> UseOrders;
	TArray<FUseOrder> PlannedUseOrders;

	/** Opens the menu if the leader reached the pending object; returns true when it did. */
	bool TryOpenMenu();
	void OpenMenuFor(AInteractableActor* Target, AOperativeCharacter* Leader);
	void CloseMenu();
	/**
	 * Closes the menu / the loot dialog whose object is gone (destroyed), disarmed and waiting for its pickup, spent (tripped / detonated)
	 * or no longer rigged (AInteractableActor::ShouldCloseActionMenu). Polled every tick: the target dies by many paths.
	 */
	void CloseStaleWindows();
	/** A disarm order from the wall: steps the leader off the cover (never when the order itself would be refused). */
	void StepOffCoverForDisarm(AOperativeCharacter& Leader) const;
	/** Clears the «approaching defuser» mark of a mine (Godot _close_action_menu). */
	void ClearDefuser(AInteractableActor* Target) const;

	TWeakObjectPtr<AInteractableActor> Pending;
	TWeakObjectPtr<AInteractableActor> MenuTarget;
	/** The menu is on screen (MenuTarget alone cannot say: it is a weak pointer, stale once the object is destroyed). */
	bool bMenuOpen = false;
	/** AInteractableActor::bTrapped of the menu's object when it opened (a change = the rigging was spent / defused: the menu is stale). */
	bool bMenuTargetTrapped = false;
	TWeakObjectPtr<ALootCrateActor> LootCrate;
	FActionMenuSpec Menu;
};
