#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "InteractionSubsystem.generated.h"

class AInteractableActor;
class AOperativeCharacter;

/**
 * Pending interaction: the squad leader walks to the clicked object and the interaction runs as soon as the
 * nearest operative is within the object's InteractionDistance.
 * Godot reference: main.gd `pending_interactable` handling (nearest living squad member interacts).
 */
UCLASS()
class CODEXTACTICS_API UInteractionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Sends the leader to Target and interacts on arrival. Returns false without a leader or target. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Interactables")
	bool RequestInteraction(AInteractableActor* Target);

	/** Drops the pending interaction (e.g. the player ordered a move elsewhere). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Interactables")
	void CancelInteraction();

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Interactables")
	AInteractableActor* GetPendingInteraction() const { return Pending.Get(); }

private:
	/** Interacts if an operative is in range; returns true when the interaction ran. */
	bool TryInteract();

	TWeakObjectPtr<AInteractableActor> Pending;
};
