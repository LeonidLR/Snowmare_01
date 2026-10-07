#pragma once

#include "CoreMinimal.h"
#include "Characters/TransferRules.h"
#include "Engine/TimerHandle.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "SquadTransferSubsystem.generated.h"

class ADroppedItemActor;
class AOperativeCharacter;
class UInstancedStaticMeshComponent;
class UItemStashComponent;

/** Purple glowing ring under the cursor while an item is being handed over (Godot TransferDonutCursor). */
UCLASS(NotBlueprintable)
class CODEXTACTICS_API ATransferCursorActor : public AActor
{
	GENERATED_BODY()

public:
	ATransferCursorActor();

	/** Ring at a ground point; bOverMate: larger (Godot scale 1.25 over a squad mate, 0.9 elsewhere). */
	void ShowAt(const FVector& Ground, bool bOverMate);

private:
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Ring;

	bool bMaterialReady = false;
};

/** What a drop / hand-over request ended in (Sprint 13). */
UENUM()
enum class ETransferRequestOutcome : uint8
{
	/** Nothing happened (no recipient, nothing to give, recipient full, orders locked); the feed says why. */
	Failed,
	/** The items changed hands at once (in range). */
	Transferred,
	/** The sender walks to the recipient; the hand-over happens on arrival (see HasPendingTransfer). */
	Approaching,
	/** Too far and the sender cannot walk over now: «Слишком далеко для передачи (макс. 2 метра)». */
	Blocked,
	/** More than one quantity possible: the split dialog is open (HUD). */
	DialogOpened,
	/** A ground drop out of reach while the operative cannot walk over: the items were put down at his feet. */
	DroppedAtFeet
};

/** Sprint 13: what a drag & drop moves where. */
enum class ETransferAction : uint8
{
	/** Operative -> squad mate. */
	Give,
	/** Operative -> the ground (a pile, ADroppedItemActor). */
	DropToGround,
	/** Operative -> a supply crate's stash. */
	Store,
	/** A crate / pile stash -> operative. */
	Take
};

/** Sprint 13: one hand-over / drop / store / take request (the split dialog carries it until confirmed). */
struct CODEXTACTICS_API FTransferRequest
{
	ETransferAction Action = ETransferAction::Give;
	/** Who walks and acts: the giver (Give / DropToGround / Store) or the taker (Take). */
	TWeakObjectPtr<AOperativeCharacter> Operative;
	/** Give: the squad mate. */
	TWeakObjectPtr<AOperativeCharacter> Recipient;
	/** Store / Take: the crate or the pile. */
	TWeakObjectPtr<AActor> Container;
	/** DropToGround: where the items go. */
	FVector Point = FVector::ZeroVector;
	ETransferItem Item = ETransferItem::Medkit;
	int32 Quantity = 0;

	static FTransferRequest MakeGive(AOperativeCharacter* Sender, AOperativeCharacter* InRecipient, ETransferItem InItem, int32 InQuantity = 0);
	static FTransferRequest MakeDrop(AOperativeCharacter* Sender, const FVector& InPoint, ETransferItem InItem, int32 InQuantity = 0);
	static FTransferRequest MakeStore(AOperativeCharacter* Sender, AActor* InContainer, ETransferItem InItem, int32 InQuantity = 0);
	static FTransferRequest MakeTake(AOperativeCharacter* Taker, AActor* InContainer, ETransferItem InItem, int32 InQuantity = 0);
};

/**
 * Hand-over of items between operatives.
 * Sprint 13 (current UI): a drag from the inventory drawer dropped on a squad mate's model or action bar portrait
 * (ACodexTacticsHUD::HandleTransferDropOnActor) -> optional quantity split dialog -> RequestTransfer: within 2 m the
 * items change hands at once (ExecuteTransferQuantity); farther away a free sender walks to 1.5 m of the recipient and
 * hands over on arrival, a blocked one (see FTransferRangeContext) gets «Слишком далеко…» in the feed.
 * Legacy click mode (Godot): the cursor ring follows the mouse, a click on a squad mate (or within 2.2 m of one) hands a
 * pack over; RMB / Esc cancels. No UI starts it since Sprint 13 (the «ПЕРЕД» dialog was retired).
 * Godot reference: main.gd _start_transfer_mode, _cancel_transfer_mode, _process_transfer_preview,
 * _handle_transfer_click, _transfer_item_to_target.
 */
UCLASS()
class CODEXTACTICS_API USquadTransferSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	/** Godot _start_transfer_mode: posts the prompt, shows the ring. */
	void StartTransferMode(ETransferItem Item);
	void CancelTransferMode();
	bool IsTransferring() const { return bTransferring; }
	ETransferItem GetTransferItem() const { return Item; }

	/** Godot _process_transfer_preview: the ring on the hovered mate or the cursor point. */
	void UpdatePreview(const FVector& CursorPoint, AActor* HitActor);

	/** Godot _handle_transfer_click. True when the item changed hands. */
	bool HandleClick(const FVector& CursorPoint, AActor* HitActor);

	/** Godot _transfer_item_to_target with its feed lines. */
	bool TransferItem(AOperativeCharacter* Sender, AOperativeCharacter* Recipient, ETransferItem InItem);

	ATransferCursorActor* GetCursor() const { return Cursor; }

	// --- Sprint 13: drag & drop hand-over ---

	/**
	 * The squad mate a drop meant: HitActor (or its owner) when it is a living squad member other than Sender, else the
	 * nearest such member within PickRadius of Point (a drop farther from everyone is a ground drop). Null when none.
	 */
	AOperativeCharacter* ResolveDropRecipient(const AOperativeCharacter* Sender, AActor* HitActor, const FVector& Point,
		float PickRadius = DropPickRadius) const;

	/** A drop this close to a squad mate (cm) still means him; farther it lands on the ground. */
	static constexpr float DropPickRadius = 80.f;

	/** Sender's situation for TransferRules::DecideRange (wave fight, turn-based, enemy targeting him, alive / not raging). */
	FTransferRangeContext BuildRangeContext(const AOperativeCharacter& Sender, const AOperativeCharacter& Recipient) const;

	/** The same for any request (Distance to the recipient / point / container box). */
	FTransferRangeContext BuildRequestContext(const FTransferRequest& Request) const;

	/** Stash of a crate (authored contents moved in when bSync) or a pile; null for anything else. */
	static UItemStashComponent* GetContainerStash(AActor* Container, bool bSync);

	/** Units the request could move at most now (stock / container contents vs. capacity / free space). */
	int32 GetMaxQuantity(const FTransferRequest& Request) const;

	/**
	 * Sprint 13 generic request. In range (2 m; container: to its box) it executes at once. Out of range a free operative
	 * walks over and acts on arrival (Approaching); a blocked one gets «Слишком далеко…» — except a ground drop, which
	 * then lands at his feet (DroppedAtFeet). A new request replaces a pending one.
	 */
	ETransferRequestOutcome Request(const FTransferRequest& InRequest);

	/** Executes a request with its feed line, no range check; returns the units moved. */
	int32 Execute(const FTransferRequest& InRequest);

	/** Click on a pile (interaction arrival): Leader takes everything he can carry; the rest stays. */
	void PickUpAll(AOperativeCharacter* Leader, ADroppedItemActor* Pile);

	/**
	 * Hands Quantity over: in range at once; out of range the sender walks over (Approaching) or is Blocked (feed message).
	 * A new request replaces a pending approach.
	 */
	ETransferRequestOutcome RequestTransfer(AOperativeCharacter* Sender, AOperativeCharacter* Recipient, ETransferItem InItem, int32 Quantity);

	/**
	 * Moves exactly Quantity (partial when the recipient runs out of room) with the feed line; no range check.
	 * Returns how many changed hands.
	 */
	int32 ExecuteTransferQuantity(AOperativeCharacter* Sender, AOperativeCharacter* Recipient, ETransferItem InItem, int32 Quantity);

	/** An operative is walking over to hand over / drop / store / take something. */
	bool HasPendingTransfer() const { return Pending.Request.Operative.IsValid(); }
	AOperativeCharacter* GetPendingSender() const { return Pending.Request.Operative.Get(); }
	ETransferAction GetPendingAction() const { return Pending.Request.Action; }

	/** Drops the pending approach (bNotify: «Передача отменена» in the feed). The sender is not stopped. */
	void CancelPendingTransfer(bool bNotify);

	/** Smokes: treat every sender as under fire (forces the Blocked branch out of range). */
	bool bForceUnderFireForTesting = false;

private:
	AOperativeCharacter* FindMate(const FVector& CursorPoint, AActor* HitActor, bool bAllowLeader) const;
	void Post(const FText& Speaker, const FString& Text) const;

	/** Where the request's target is now (recipient / point / container). */
	static FVector GetTargetLocation(const FTransferRequest& Request);
	/** Distance from the acting operative to the target (container: to its box). */
	static float GetRequestDistance(const FTransferRequest& Request);
	/** Move order to the approach point; false when the operative refuses / has no path. */
	bool IssueApproach(AOperativeCharacter& Walker, const FTransferRequest& Request);
	/** Timer: act on arrival, follow a moving recipient, cancel on another order / death / timeout. */
	void TickPending();

	struct FPendingTransfer
	{
		FTransferRequest Request;
		/** Where the move order was aimed and where the target stood then. */
		FVector Destination = FVector::ZeroVector;
		FVector RecipientAnchor = FVector::ZeroVector;
		/** Path end the sender's AI is following for our order (captured once the move runs). */
		TOptional<FVector> PathDestination;
		double StartTime = 0.0;
		double OrderTime = 0.0;
	};
	FPendingTransfer Pending;
	FTimerHandle PendingTimer;

	bool bTransferring = false;
	ETransferItem Item = ETransferItem::Medkit;

	UPROPERTY(Transient)
	TObjectPtr<ATransferCursorActor> Cursor;
};
