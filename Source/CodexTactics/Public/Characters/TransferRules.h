#pragma once

#include "CoreMinimal.h"
#include "TransferRules.generated.h"

class AOperativeCharacter;
class UItemStashComponent;

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
	/** "+1 Turret", "+30 M16 rounds", … (Godot custom_feedback / item_names). */
	FString Feedback;
	/** Units that changed hands (TransferQuantity; 1 / a pack for Transfer). */
	int32 Moved = 0;
	/** TransferQuantity moved less than asked because the recipient ran out of room. */
	bool bClampedByCapacity = false;
};

/** What a hand-over request does with the distance between sender and recipient (Sprint 13). */
enum class ETransferRangeDecision : uint8
{
	/** Within MaxTransferDistance: hand over at once. */
	InRange,
	/** Too far, the sender is free: he walks to the recipient and hands over on arrival. */
	Approach,
	/** Too far and the sender cannot walk over now: "Too far to hand over (max 2 m)". */
	Blocked
};

/**
 * Situation of the sender for ETransferRangeDecision. «Blocked» (Sprint 13 definition): a real-time or turn-based wave
 * fight is on (bInCombat / bTurnBased — in turn-based a walk costs AP and belongs to the grid), a living enemy currently
 * targets the sender (bUnderFire), or the sender cannot take a move order (!bCanMove: dead, raging, no path).
 */
struct CODEXTACTICS_API FTransferRangeContext
{
	float Distance = 0.f;
	bool bInCombat = false;
	bool bTurnBased = false;
	bool bUnderFire = false;
	bool bCanMove = true;
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

	/** Sprint 13: sender and recipient must stand within 2 m (horizontal distance) for a hand-over. */
	inline constexpr float MaxTransferDistance = 200.f;

	/** Sprint 13: an approaching sender stops 1.5 m from the recipient. */
	inline constexpr float ApproachStopDistance = 150.f;

	/** Sprint 13: an operative walking to drop something on the ground stops 1 m from the spot. */
	inline constexpr float GroundDropStopDistance = 100.f;

	/** True for the ammo items (rounds / fuel / coolant / batteries). */
	CODEXTACTICS_API bool IsAmmoItem(ETransferItem Item);

	/** Quantity step of the split dialog: 5 for every ammo type, 1 for countable items (deployables, medkits, food, matches). */
	CODEXTACTICS_API int32 GetItemQuantityStep(ETransferItem Item);

	/** Smallest hand-over: one step, or the whole remainder when fewer than a step are left (0 when MaxQuantity <= 0). */
	CODEXTACTICS_API int32 GetMinQuantity(ETransferItem Item, int32 MaxQuantity);

	/**
	 * Snaps Desired to the dialog grid: [GetMinQuantity, MaxQuantity], multiples of the step below MaxQuantity, MaxQuantity
	 * itself (the whole stack, "ALL") allowed even off the grid.
	 */
	CODEXTACTICS_API int32 QuantizeQuantity(ETransferItem Item, int32 Desired, int32 MaxQuantity);

	/** [-] / [+] of the dialog: one step down (Direction < 0) or up from Current, snapped by QuantizeQuantity. */
	CODEXTACTICS_API int32 StepQuantity(ETransferItem Item, int32 Current, int32 Direction, int32 MaxQuantity);

	/** The split dialog only opens when there is a choice (MaxQuantity above the minimum); otherwise the transfer is instant. */
	CODEXTACTICS_API bool NeedsQuantityDialog(ETransferItem Item, int32 MaxQuantity);

	/** How many more the recipient can take: up to the carry maximum for deployables, unlimited (MAX_int32) otherwise. */
	CODEXTACTICS_API int32 GetRecipientCapacity(const AOperativeCharacter& Recipient, ETransferItem Item);

	/** min(available at the sender, recipient capacity). */
	CODEXTACTICS_API int32 GetMaxTransferQuantity(const AOperativeCharacter& Sender, const AOperativeCharacter& Recipient, ETransferItem Item);

	/** Horizontal distance check against MaxTransferDistance. */
	CODEXTACTICS_API bool IsWithinTransferRange(const FVector& SenderLocation, const FVector& RecipientLocation);

	/** Different operatives standing within MaxTransferDistance of each other. */
	CODEXTACTICS_API bool CanTransferTo(const AOperativeCharacter& Sender, const AOperativeCharacter& Recipient);

	/** In range / walk over / blocked (see FTransferRangeContext). */
	CODEXTACTICS_API ETransferRangeDecision DecideRange(const FTransferRangeContext& Context);

	/**
	 * Moves exactly Quantity (clamped to what the sender has and the recipient can take; Moved / bClampedByCapacity report
	 * a partial move). bRecipientFull when the recipient cannot take a single one. Ammo comes out of the reserve.
	 */
	CODEXTACTICS_API FTransferResult TransferQuantity(AOperativeCharacter& Sender, AOperativeCharacter& Recipient, ETransferItem Item, int32 Quantity);

	/** Takes up to Max of Item out of an operative (ammo from the reserve); returns how many came out. */
	CODEXTACTICS_API int32 RemoveFromOperative(AOperativeCharacter& Operative, ETransferItem Item, int32 Max);

	/** Gives Count of Item to an operative without any capacity check (callers clamp with GetRecipientCapacity). */
	CODEXTACTICS_API void AddToOperative(AOperativeCharacter& Operative, ETransferItem Item, int32 Count);

	/**
	 * Operative -> stash (ground pile / crate): up to Quantity, clamped by his stock and the stash's free space
	 * (bClampedByCapacity / bRecipientFull report the stash running out of room).
	 */
	CODEXTACTICS_API FTransferResult StoreInStash(AOperativeCharacter& Sender, UItemStashComponent& Stash, ETransferItem Item, int32 Quantity);

	/** Stash -> operative: up to Quantity, clamped by what lies there and the operative's capacity (leftover stays). */
	CODEXTACTICS_API FTransferResult TakeFromStash(UItemStashComponent& Stash, AOperativeCharacter& Recipient, ETransferItem Item, int32 Quantity);

	/** Nominative item name ("Medkit", "M16 rounds", ...). */
	CODEXTACTICS_API FString GetItemName(ETransferItem Item);

	/** Accusative name for the prompt ("Medkit", "M16 rounds (x30)", ...; Godot _start_transfer_mode item_names). */
	CODEXTACTICS_API FString GetPromptName(ETransferItem Item);
}
