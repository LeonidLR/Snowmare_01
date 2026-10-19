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
 * Where an operative is with a crate (rev 2026-10-19, game designer: the animation FIRST, then the loot). Click -> Opening (he faces it
 * and plays the chest-open clip) -> Open (the loot window at the clip's lid-open moment) -> Take / Take All -> Searching (the window
 * shows "Searching..." with its buttons off; the chest-search clip) -> the items move at the clip's grab moment -> Open again (Take
 * All closes the window -> Closed).
 */
UENUM(BlueprintType)
enum class ELootSessionState : uint8
{
	Closed,
	/** The chest-open clip plays (or waits for the tactical pause to end); no loot window yet. */
	Opening,
	/** The loot window is usable. */
	Open,
	/** A take was asked for: the chest-search clip plays (or waits for the release); nothing can be taken meanwhile. */
	Searching
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

	/**
	 * Opens the lid: the operative faces the crate and plays the chest-open clip; the loot dialog appears at the clip's lid-open moment
	 * (no clip: after OpenSeconds). In the tactical pause it waits for the release (time stands still), then plays.
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Loot")
	void StartOpening(AOperativeCharacter* Leader);

	/**
	 * Rev 2026-10-19: Take (Item) / Take All (bAll) from the open window: Collector plays the chest-search clip and the items move at its
	 * grab moment (UInteractionSubsystem::HandleSearchFinished posts the result). False when the window is not usable now (opening,
	 * already searching, wrecked, empty).
	 */
	bool RequestSearch(AOperativeCharacter* Collector, ELootItem Item, bool bAll);

	/** The loot window was shown without the opening (a dev / smoke shortcut): the session is Open. */
	void MarkOpen();

	/** The loot window closed: an open session ends (a running search still finishes and moves the items). */
	void HandleDialogClosed();

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Loot")
	ELootSessionState GetSessionState() const { return SessionState; }

	/** The window shows "Searching..." and takes nothing. */
	bool IsSearching() const { return SessionState == ELootSessionState::Searching; }

	/** Seconds the last search waited for its grab moment (smokes). */
	float GetLastSearchDelaySeconds() const { return LastSearchDelaySeconds; }

	/** Moves one stack into Collector's supply; returns the Godot result line ("+2 🩹 Medkit"), empty if none. */
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

	/** "📦 Army supply crate" (Godot crate_name). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Loot")
	FText CrateName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Loot")
	ELootTier Tier = ELootTier::Standard;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Loot")
	FLootContents Contents;

	/**
	 * Lid opening before the dialog when no chest-open clip plays (prone, none assigned), s (Godot timer 1.8). With the clip
	 * (2026-10-15) the window opens as it ends; in the tactical pause at once (InteractionAnimRules::GetLootWindowDelay).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Loot", meta = (ClampMin = "0"))
	float OpenSeconds = 1.8f;

	/** Seconds the last StartOpening waited for the loot window (smokes). */
	float GetLastOpenDelaySeconds() const { return LastOpenDelaySeconds; }

	/** The search's wait for its grab when no chest-search clip plays (prone, none assigned), s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Loot", meta = (ClampMin = "0"))
	float SearchFallbackSeconds = 0.6f;

	/** The opener / collector may stand this far beyond InteractionDistance when the lid opens / the hand grabs, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Loot", meta = (ClampMin = "0"))
	float SessionReachSlackCm = 100.f;

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
	void FinishOpening();
	void FinishDefusal(TWeakObjectPtr<AOperativeCharacter> WeakUser);
	/** Plays the chest-open clip now and arms the window timer. */
	void BeginOpenClip();
	/** Plays the chest-search clip now and arms the grab timer. */
	void BeginSearchClip();
	void FinishSearch();
	/** The session operative can still reach the crate (alive, not knocked down, close enough). */
	bool IsSessionOperativeThere() const;
	/** Waits for the tactical pause to end (the opening / the search is queued), or stops waiting. */
	void SetWaitingForRelease(bool bWait);
	bool IsTimeFrozen() const;
	void ResetSession();

	UFUNCTION()
	void HandlePauseReleased();

	UPROPERTY(VisibleInstanceOnly, Category = "CodexTactics|Loot")
	bool bDestroyed = false;

	UPROPERTY(VisibleInstanceOnly, Category = "CodexTactics|Loot")
	ELootSessionState SessionState = ELootSessionState::Closed;

	TWeakObjectPtr<AOperativeCharacter> SessionOperative;
	ELootItem SearchItem = ELootItem::Medkit;
	bool bSearchAll = false;
	bool bWaitingForRelease = false;
	FTimerHandle SessionTimer;
	float LastOpenDelaySeconds = 0.f;
	float LastSearchDelaySeconds = 0.f;
};
