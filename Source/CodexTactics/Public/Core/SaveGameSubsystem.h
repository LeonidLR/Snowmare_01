#pragma once

#include "CoreMinimal.h"
#include "Core/SaveGameRules.h"
#include "Subsystems/WorldSubsystem.h"
#include "SaveGameSubsystem.generated.h"

class FJsonObject;

/**
 * Save slots as JSON files in Saved/SaveGames/<slot>.json (Godot user://saves/<slot>.json, same keys): squad (position,
 * health, cold, stats, stance, weapon + ammo inventory, items, guard, leader), game state (combat unlocked,
 * preparation / wave, wave index, solo), quest chain and world (supply crates). Loading restores them on the current
 * level; the flow resumes in the saved wave's preparation.
 * Godot reference: Scripts/managers/save_manager.gd, main.gd _perform_quick_save / _on_save_slot_requested /
 * _on_load_slot_requested.
 */
UCLASS()
class CODEXTACTICS_API USaveGameSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	/** Godot save_game. CustomTitle empty: the slot name, or "Autosave / Quicksave: <stage>". */
	bool SaveGame(const FString& SlotName, const FString& CustomTitle = FString(), const FString& Author = TEXT("Commander"));

	/** Godot load_game. */
	bool LoadGame(const FString& SlotName);

	bool HasSave(const FString& SlotName) const;
	bool DeleteSave(const FString& SlotName) const;
	bool GetSaveInfo(const FString& SlotName, FSaveSlotInfo& OutInfo) const;
	/** Every slot, newest first (Godot get_all_saves). */
	TArray<FSaveSlotInfo> GetAllSaves() const;
	FString SuggestNextSlotName() const;

	/** F5 (Godot _perform_quick_save): slot «quicksave», feed line; refused after game over. */
	bool QuickSave();

	/** Save from the save / load dialog with the feed line (Godot _on_save_slot_requested). */
	bool SaveToSlotWithMessage(const FString& SlotName);

	/** Load from the dialog with the feed line (Godot _on_load_slot_requested). */
	bool LoadFromSlotWithMessage(const FString& SlotName);

	/** Current stage for the save metadata (Godot compute_current_stage_name). */
	FString GetCurrentStageName() const;

	FString GetSaveDirectory() const;

	/** Tests / smokes write their slots elsewhere. */
	FString SaveDirectoryOverride;

private:
	TSharedRef<FJsonObject> BuildSaveData(const FString& SlotName, const FString& Title, const FString& Author) const;
	void ApplySaveData(const TSharedRef<FJsonObject>& Data);
	FString GetSavePath(const FString& SlotName) const;
	void Post(const FString& Speaker, const FString& Text) const;
};
