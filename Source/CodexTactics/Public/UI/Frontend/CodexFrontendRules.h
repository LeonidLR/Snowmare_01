#pragma once

#include "CoreMinimal.h"
#include "CodexFrontendRules.generated.h"

/** Main menu entries (frontend phase 1). Each one has a camera anchor id (ACodexMenuCameraAnchor::AnchorId). */
UENUM(BlueprintType)
enum class ECodexMainMenuEntry : uint8
{
	Continue,
	NewGame,
	LoadGame,
	Options,
	Credits,
	Quit
};

/** Yes / No or a single OK button. */
UENUM(BlueprintType)
enum class ECodexConfirmType : uint8
{
	YesNo,
	Ok
};

UENUM(BlueprintType)
enum class ECodexConfirmResult : uint8
{
	Confirmed,
	Cancelled
};

/** Save / load slot screen mode. */
UENUM(BlueprintType)
enum class ECodexSaveScreenMode : uint8
{
	Save,
	Load
};

/** Why saving is (not) allowed right now. */
struct FCodexSaveGate
{
	bool bAllowed = true;
	/** Player-facing reason when not allowed (shown on the pause menu SAVE GAME entry). */
	FText Reason;
};

/**
 * Pure rules of the frontend / pause menu (no Godot reference: the Godot start menu had two mode buttons and the
 * pause menu allowed saving everywhere; the professional frontend is a 2026-10-08 user decision). The save policy
 * itself is the save system's (USaveGameSubsystem::CanSaveNow).
 */
namespace CodexFrontendRules
{
	/** Stable id of an entry: the menu button's EntryId and the default camera anchor id ("Continue", "NewGame", ...). */
	CODEXTACTICS_API FName GetEntryId(ECodexMainMenuEntry Entry);

	/** Entry for an id, false for unknown ids. */
	CODEXTACTICS_API bool ParseEntryId(FName Id, ECodexMainMenuEntry& OutEntry);

	/** CONTINUE needs a save; LOAD GAME stays enabled (the slot screen says "No saved games"). */
	CODEXTACTICS_API bool IsEntryEnabled(ECodexMainMenuEntry Entry, bool bHasSaves);

	/** "h:mm:ss" from one hour on, else "m:ss"; a negative time (unknown) is "--:--". */
	CODEXTACTICS_API FString FormatPlayTime(float Seconds);

	/** Camera blend time: the anchor's override when >= 0, else the project default (never negative). */
	CODEXTACTICS_API float ResolveBlendTime(float AnchorOverride, float DefaultSeconds);

	/** "SAVE" for a new slot name, "OVERWRITE" for an existing one. */
	CODEXTACTICS_API FText GetSaveButtonLabel(bool bSlotExists);
}
