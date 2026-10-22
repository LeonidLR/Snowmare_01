#pragma once

#include "CoreMinimal.h"
#include "Core/SaveGameRules.h"
#include "Engine/TimerHandle.h"
#include "Subsystems/WorldSubsystem.h"
#include "SaveGameSubsystem.generated.h"

class FJsonObject;

/** A save was applied to a world (after LoadGame finished; also for a load that reopened the map). */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnSaveAppliedNative, UWorld*, const FString& /*SlotName*/);

/**
 * Save slots as JSON files in Saved/SaveGames/<slot>.json (Godot user://saves/<slot>.json, same keys; format version
 * SaveGameRules::CurrentSaveVersion). Saved: the squad (position, health, cold, stats, stance, weapon + ammo per weapon,
 * items, bonus items, guard, fire posture, leader), game state (flow phase, wave, preparation time, solo, squad posture,
 * Commander Mode), quest chain, world (supply crates, piles on the ground, every interactable — relocated / burning
 * barrels, barricades, turrets, mines, tripwires, traps, generator, hidden quest objects, narrative elements read —,
 * gates, dialogue triggers, patrol routes, living enemies with their patrol / search; dead stay dead) and Susanin.
 * Loading restores them on the current level (LoadGame) or reopens the saved map first (LoadGameWithTravel, the
 * "Continue" / "Load Game" path of a menu).
 *
 * Save policy (user decision 2026-10-08): saving only outside combat (CanSaveNow). Rev 5f (user decision 2026-10-14, after a
 * save bug): NO automatic saves - the only saves are the tutorial checkpoints (UTutorialSubsystem::SaveCheckpoint, slot
 * "checkpoint") and the player's own: F5 (QuickSave) and the pause menu's SAVE GAME (SaveToSlotWithMessage). Nothing else
 * calls SaveGame; CodexTactics.Save.Policy.NoAutosave guards that. Legacy "autosave" files on disk are hidden (GetAllSaves).
 * Godot reference: Scripts/managers/save_manager.gd, main.gd _perform_quick_save / _on_save_slot_requested /
 * _on_load_slot_requested.
 */
UCLASS()
class CODEXTACTICS_API USaveGameSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/**
	 * Godot save_game: writes the slot unconditionally (the policy is checked by the callers: QuickSave / SaveToSlotWithMessage /
	 * the tutorial checkpoint). CustomTitle empty: the slot name, or "Quicksave: <stage>" / "Checkpoint: <stage>".
	 */
	bool SaveGame(const FString& SlotName, const FString& CustomTitle = FString(), const FString& Author = TEXT("Commander"));

	/** Godot load_game: applies the slot to the current world (loading is always allowed). */
	bool LoadGame(const FString& SlotName);

	/**
	 * Reopens the save's map (version 1 saves: the current level) and applies the slot once it has begun play (the
	 * mission starts without the start menu / intro). For "Continue" / "Load Game" from a menu map. False without the slot.
	 */
	bool LoadGameWithTravel(const FString& SlotName);

	/** Newest listed slot (checkpoint / quicksave / manual; never a legacy autosave) for "Continue"; empty without saves. */
	FString GetContinueSlot() const;

	bool HasSave(const FString& SlotName) const;
	bool DeleteSave(const FString& SlotName) const;
	bool GetSaveInfo(const FString& SlotName, FSaveSlotInfo& OutInfo) const;
	/** Every slot, newest first (Godot get_all_saves); rev 5f: legacy "autosave" files only with bIncludeHidden. */
	TArray<FSaveSlotInfo> GetAllSaves(bool bIncludeHidden = false) const;
	FString SuggestNextSlotName() const;

	/** Save policy now (SaveGameRules::GetSaveBlockReason of the flow phase). */
	ESaveBlockReason GetSaveBlockReason() const;

	/** The player may save now; OutReason gets the hint ("Saving is disabled during combat") otherwise. */
	bool CanSaveNow(FText* OutReason = nullptr) const;

	/** F5 (Godot _perform_quick_save): slot «quicksave», feed line; refused by the save policy (feed line with the reason). */
	bool QuickSave();

	/**
	 * F9: reopens the newest QUICKSAVE or CHECKPOINT (SaveGameRules::PickQuickLoadIndex; rev 5f - manual saves are loaded from the
	 * menu) with LoadGameWithTravel, so the level starts straight in the saved state. Loading is always allowed (also in combat);
	 * feed line "No quicksave or checkpoint to load" without one.
	 */
	bool QuickLoad();

	/** Save from the save / load dialog with the feed line (Godot _on_save_slot_requested); refused by the save policy. */
	bool SaveToSlotWithMessage(const FString& SlotName);

	/** Load from the dialog with the feed line (Godot _on_load_slot_requested). */
	bool LoadFromSlotWithMessage(const FString& SlotName);

	/**
	 * Tutorial checkpoints may be written (default) - off for headless dev checks started with -ExecCmds (they must not write
	 * Saved/SaveGames behind each other's back) unless CVar Codex.Save.HeadlessCheckpoints is 1 (it survives a level travel); a
	 * check that tests checkpoints turns it on. Rev 5f: there is no other automatic save.
	 */
	bool bCheckpointsEnabled = true;

	/** Saves written in this world (any slot, smokes: proves that nothing saves behind the player's back). */
	int32 GetWrittenSaveCount() const { return WrittenSaveCount; }

	/** Current stage for the save metadata (Godot compute_current_stage_name). */
	FString GetCurrentStageName() const;

	FString GetSaveDirectory() const;

	/** The save data of the world as it is now (what SaveGame would write; smokes diff it). */
	TSharedRef<FJsonObject> CaptureWorldState() const;

	/** Broadcast after every applied load (in-place or after travel). */
	static FOnSaveAppliedNative& OnSaveApplied();

	/**
	 * The restore order (audit 2026-10-19; docs/systems/save-load.md "Restore order"): the names of the steps ApplySaveData runs, in
	 * order. One table in SaveGameSubsystem.cpp drives both; a new feature plugs its restore into the step it depends on.
	 */
	static TArray<FName> GetRestoreOrder();

	/** A save is being applied right now (ApplySaveData): nothing may be written meanwhile (a checkpoint fired by a restored step). */
	bool IsApplyingSave() const { return bApplyingSave; }

	/** The save's map exists in this build (a slot whose level was renamed / removed cannot be loaded with travel). */
	static bool IsSaveMapAvailable(const FSaveSlotInfo& Info);

	/** Tests / smokes write their slots elsewhere. */
	FString SaveDirectoryOverride;

	/** Everything one restore step needs (built once per ApplySaveData; public only for the step table in the .cpp). */
	struct FSaveRestoreContext
	{
		UWorld* World = nullptr;
		TSharedPtr<FJsonObject> Data;
		/** Data["world_state"] (null in a corrupt / sectionless file: the world steps do nothing). */
		TSharedPtr<FJsonObject> WorldState;
		int32 Version = 1;
		/** Set by the squad step, consumed by the game-state step. */
		TObjectPtr<class AOperativeCharacter> NewLeader = nullptr;
		/** The operatives the squad step restored with their entries (the game-state step applies their standing orders after the squad state). */
		TArray<TPair<TObjectPtr<class AOperativeCharacter>, TSharedPtr<FJsonObject>>> SquadEntries;
	};

private:
	TSharedRef<FJsonObject> BuildSaveData(const FString& SlotName, const FString& Title, const FString& Author) const;
	void ApplySaveData(const TSharedRef<FJsonObject>& Data);
	// The restore steps, in GetRestoreOrder (see the table at the top of SaveGameSubsystem.cpp).
	void RestoreReset(FSaveRestoreContext& Context);
	void RestoreEnvironment(FSaveRestoreContext& Context);
	void RestoreSquadTransfer(FSaveRestoreContext& Context);
	void RestoreTutorial(FSaveRestoreContext& Context);
	void RestoreSquad(FSaveRestoreContext& Context);
	void RestoreGameState(FSaveRestoreContext& Context);
	void RestoreQuests(FSaveRestoreContext& Context);
	void RestoreWorld(FSaveRestoreContext& Context);
	void RestoreVehicles(FSaveRestoreContext& Context);
	void RestoreEnemies(FSaveRestoreContext& Context);
	void RestoreStealth(FSaveRestoreContext& Context);
	void RestoreUI(FSaveRestoreContext& Context);
	bool WriteSave(const FString& SlotName, const FString& CustomTitle, const FString& Author);
	FString GetSavePath(const FString& SlotName) const;
	void Post(const FString& Speaker, const FString& Text) const;

	void ApplyPendingLoad();

	/**
	 * Black full-screen cover (top of the viewport, above HUD and UMG) while a pending load is applied, so the level's own
	 * start is never seen before the save; HideLoadCover fades it out once the saved state and the camera are in place.
	 */
	void ShowLoadCover(UWorld& InWorld);
	void HideLoadCover();
	void RemoveLoadCover();

	FTimerHandle PendingLoadTimer;
	FTimerHandle LoadCoverTimer;
	TSharedPtr<class SWidget> LoadCoverWidget;
	/** Platform time the cover fade-out started (< 0: fully opaque). */
	double LoadCoverFadeStart = -1.0;
	int32 WrittenSaveCount = 0;
	bool bApplyingSave = false;
};
