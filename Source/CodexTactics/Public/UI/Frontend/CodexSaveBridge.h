#pragma once

#include "CoreMinimal.h"
#include "UI/Frontend/CodexFrontendRules.h"

class USaveGameSubsystem;
class UWorld;

/** What a save slot line shows. */
struct FCodexSaveSlotView
{
	FString SlotName;
	FString Title;
	FString DateTime;
	/** Level / stage name. */
	FString Level;
	/** "h:mm:ss" / "m:ss", "--:--" while the save system does not record play time. */
	FString PlayTime;
	int64 Timestamp = 0;
};

/**
 * The ONLY place where the frontend touches the save system (USaveGameSubsystem, owned by the save-system work in
 * Core/SaveGame*). Keeps the menus independent of the save format: when the save system grows (level path, play time,
 * CanSaveNow), only this file changes.
 * Saves go to the session's directory override when one is set (UCodexFrontendSubsystem::SaveDirectoryOverride, smokes).
 */
namespace CodexSaveBridge
{
	/** The world's save subsystem with the session's directory override applied, or null. */
	CODEXTACTICS_API USaveGameSubsystem* GetSaves(const UWorld* World);

	/** Every slot, newest first. */
	CODEXTACTICS_API TArray<FCodexSaveSlotView> ListSlots(const UWorld* World);

	CODEXTACTICS_API bool HasAnySave(const UWorld* World);

	/** Newest slot name, empty without saves. */
	CODEXTACTICS_API FString GetLatestSlot(const UWorld* World);

	CODEXTACTICS_API bool SlotExists(const UWorld* World, const FString& SlotName);

	CODEXTACTICS_API FString SuggestSlotName(const UWorld* World);

	/** Saves the running mission (feed line); false when refused. */
	CODEXTACTICS_API bool SaveToSlot(UWorld* World, const FString& SlotName);

	/**
	 * Loads a slot from a menu: in place when the slot belongs to the running mission level (feed line), else the save
	 * system reopens the slot's map and applies it there (USaveGameSubsystem::LoadGameWithTravel). OutTravelled tells which.
	 */
	CODEXTACTICS_API bool LoadSlot(UWorld* World, const FString& SlotName, bool& bOutTravelled);

	/** Opens the slot's map and applies the slot once it has started (frontend CONTINUE / LOAD GAME). */
	CODEXTACTICS_API bool LoadWithTravel(UWorld* World, const FString& SlotName);

	CODEXTACTICS_API bool DeleteSlot(const UWorld* World, const FString& SlotName);

	/** May the player save right now (pause menu SAVE GAME)? The save policy is USaveGameSubsystem::CanSaveNow. */
	CODEXTACTICS_API FCodexSaveGate GetSaveGate(const UWorld* World);
}
