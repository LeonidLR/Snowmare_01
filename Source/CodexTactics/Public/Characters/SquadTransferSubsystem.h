#pragma once

#include "CoreMinimal.h"
#include "Characters/TransferRules.h"
#include "Subsystems/WorldSubsystem.h"
#include "SquadTransferSubsystem.generated.h"

class AOperativeCharacter;
class UInstancedStaticMeshComponent;

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

/**
 * Hand-over of items between operatives: the dialog picks an item, the cursor ring follows the mouse, a click on a
 * squad mate (or within 2.2 m of one) hands it over; RMB / Esc cancels.
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

private:
	AOperativeCharacter* FindMate(const FVector& CursorPoint, AActor* HitActor, bool bAllowLeader) const;
	void Post(const FText& Speaker, const FString& Text) const;

	bool bTransferring = false;
	ETransferItem Item = ETransferItem::Medkit;

	UPROPERTY(Transient)
	TObjectPtr<ATransferCursorActor> Cursor;
};
