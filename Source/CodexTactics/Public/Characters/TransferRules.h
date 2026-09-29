#pragma once

#include "CoreMinimal.h"
#include "TransferRules.generated.h"

class AOperativeCharacter;

/** What an operative can hand to a squad mate (Godot transfer_dialog.gd item types, in the dialog order). */
UENUM(BlueprintType)
enum class ETransferItem : uint8
{
	Turret,
	Barricade,
	Mine,
	Medkit,
	RifleAmmo,
	PistolAmmo,
	CannedFood,
	ShotgunAmmo,
	Bread,
	FlameFuel,
	Chocolate,
	CryoAmmo,
	Matches,
	PlasmaAmmo
};

/** Outcome of a hand-over. */
struct CODEXTACTICS_API FTransferResult
{
	bool bDone = false;
	/** Recipient already carries the maximum (engineering items). */
	bool bRecipientFull = false;
	/** «+1 Турель», «+30 Патроны M16», … (Godot custom_feedback / item_names). */
	FString Feedback;
};

/**
 * Pure hand-over rules. Godot reference: main.gd _transfer_item_to_target (engineering items up to the recipient's
 * maximum, provisions / matches one at a time, ammo in packs: M16 30, 12k 8, fuel 25, cryo 15, plasma 10 — the reserve
 * of the weapon). Deliberate deviation: pistol ammo (12) is handed over too — Godot shows the button but has no branch.
 */
namespace TransferRules
{
	/** Godot arsenal id of an ammo item (m16, pistol, shotgun, flamethrower, cryo_emitter, plasma_carbine); empty otherwise. */
	CODEXTACTICS_API FString GetAmmoWeaponId(ETransferItem Item);

	/** Rounds per hand-over of an ammo item. */
	CODEXTACTICS_API int32 GetAmmoPack(ETransferItem Item);

	/** How many the sender can hand over now (reserve for ammo). */
	CODEXTACTICS_API int32 GetAvailable(const AOperativeCharacter& Sender, ETransferItem Item);

	/** Moves the item; nothing changes when the sender has none or the recipient is full. */
	CODEXTACTICS_API FTransferResult Transfer(AOperativeCharacter& Sender, AOperativeCharacter& Recipient, ETransferItem Item);

	/** Accusative name for the prompt («Аптечку», «Патроны M16 (30 шт.)», …; Godot _start_transfer_mode item_names). */
	CODEXTACTICS_API FString GetPromptName(ETransferItem Item);
}
