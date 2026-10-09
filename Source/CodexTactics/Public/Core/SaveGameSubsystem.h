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
 * Save policy (user decision 2026-10-08): saving only outside combat (CanSaveNow); autosave right before a fight starts
 * (wave / ambush start, written with the pre-combat phase) and right after it ends (wave cleared / victory).
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
	 * Godot save_game: writes the slot unconditionally (the policy is checked by the player-facing entry points QuickSave /
	 * SaveToSlotWithMessage and by the autosave). CustomTitle empty: the slot name, or "Autosave / Quicksave: <stage>".
	 */
	bool SaveGame(const FString& SlotName, const FString& CustomTitle = FString(), const FString& Author = TEXT("Commander"));

	/** Godot load_game: applies the slot to the current world (loading is always allowed). */
	bool LoadGame(const FString& SlotName);

	/**
	 * Reopens the save's map (version 1 saves: the current level) and applies the slot once it has begun play (the
	 * mission starts without the start menu / intro). For "Continue" / "Load Game" from a menu map. False without the slot.
	 */
	bool LoadGameWithTravel(const FString& SlotName);

	/** Newest slot (any type) for "Continue"; empty without saves. */
	FString GetContinueSlot() const;

	bool HasSave(const FString& SlotName) const;
	bool DeleteSave(const FString& SlotName) const;
	bool GetSaveInfo(const FString& SlotName, FSaveSlotInfo& OutInfo) const;
	/** Every slot, newest first (Godot get_all_saves). */
	TArray<FSaveSlotInfo> GetAllSaves() const;
	FString SuggestNextSlotName() const;

	/** Save policy now (SaveGameRules::GetSaveBlockReason of the flow phase). */
	ESaveBlockReason GetSaveBlockReason() const;

	/** The player may save now; OutReason gets the hint ("Saving is disabled during combat") otherwise. */
	bool CanSaveNow(FText* OutReason = nullptr) const;

	/** F5 (Godot _perform_quick_save): slot «quicksave», feed line; refused by the save policy (feed line with the reason). */
	bool QuickSave();

	/**
	 * F9: reopens the newest save (GetContinueSlot, any type) with LoadGameWithTravel, so the level starts straight in the
	 * saved state. Loading is always allowed (also in combat); feed line "No saves to load" without saves.
	 */
	bool QuickLoad();

	/** Save from the save / load dialog with the feed line (Godot _on_save_slot_requested); refused by the save policy. */
	bool SaveToSlotWithMessage(const FString& SlotName);

	/** Load from the dialog with the feed line (Godot _on_load_slot_requested). */
	bool LoadFromSlotWithMessage(const FString& SlotName);

	/** Writes the autosave slot for Moment (BeforeCombat: with the pre-combat phase). False when off or failed. */
	bool Autosave(EAutosaveMoment Moment);

	/**
	 * Automatic saves on (default) — off for headless dev checks started with -ExecCmds (they must not write
	 * Saved/SaveGames behind each other's back); a check that tests the autosave turns it on. CVar Codex.Save.Autosave 0
	 * turns it off everywhere.
	 */
	bool bAutosaveEnabled = true;

	/** Autosaves written in this world and the last one's moment (smokes). */
	int32 GetAutosaveCount() const { return AutosaveCount; }
	EAutosaveMoment GetLastAutosaveMoment() const { return LastAutosaveMoment; }

	/** Current stage for the save metadata (Godot compute_current_stage_name). */
	FString GetCurrentStageName() const;

	FString GetSaveDirectory() const;

	/** The save data of the world as it is now (what SaveGame would write; smokes diff it). */
	TSharedRef<FJsonObject> CaptureWorldState() const;

	/** Broadcast after every applied load (in-place or after travel). */
	static FOnSaveAppliedNative& OnSaveApplied();

	/** Tests / smokes write their slots elsewhere. */
	FString SaveDirectoryOverride;

private:
	/** BeforeCombat autosave: the flow is already in the fight, the save records the moment before it. */
	struct FPreCombatOverride
	{
		bool bActive = false;
		bool bAmbush = false;
		int32 WaveIndex = 1;
	};

	TSharedRef<FJsonObject> BuildSaveData(const FString& SlotName, const FString& Title, const FString& Author,
		const FPreCombatOverride& PreCombat) const;
	void ApplySaveData(const TSharedRef<FJsonObject>& Data);
	bool WriteSave(const FString& SlotName, const FString& CustomTitle, const FString& Author, const FPreCombatOverride& PreCombat);
	FString GetSavePath(const FString& SlotName) const;
	void Post(const FString& Speaker, const FString& Text) const;

	void HandleFlowTransition(ECodexGamePhase OldPhase, ECodexGamePhase NewPhase, ECodexCombatMode NewMode);
	void ApplyPendingLoad();

	/**
	 * Black full-screen cover (top of the viewport, above HUD and UMG) while a pending load is applied, so the level's own
	 * start is never seen before the save; HideLoadCover fades it out once the saved state and the camera are in place.
	 */
	void ShowLoadCover(UWorld& InWorld);
	void HideLoadCover();
	void RemoveLoadCover();

	FDelegateHandle FlowTransitionHandle;
	FTimerHandle PendingLoadTimer;
	FTimerHandle LoadCoverTimer;
	TSharedPtr<class SWidget> LoadCoverWidget;
	/** Platform time the cover fade-out started (< 0: fully opaque). */
	double LoadCoverFadeStart = -1.0;
	int32 AutosaveCount = 0;
	EAutosaveMoment LastAutosaveMoment = EAutosaveMoment::None;
};
