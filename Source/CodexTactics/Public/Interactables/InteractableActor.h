#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Quests/QuestChain.h"
#include "InteractableActor.generated.h"

class AOperativeCharacter;
class UBoxComponent;
class UHeatSourceComponent;
class UStaticMeshComponent;

/**
 * Quest object of the checkpoint (gate terminal, canister, abandoned APC, backup generator, gate).
 * Clicking it sends the squad leader to it; once any living operative is within InteractionDistance of the
 * collision box, the interaction runs through UQuestSubsystem. A generator carries a heat source that turns on
 * when the generator starts.
 * Godot reference: Scenes/movements/interactable.gd (quest part; barrels, traps and repair come with their systems).
 */
UCLASS()
class CODEXTACTICS_API AInteractableActor : public AActor
{
	GENERATED_BODY()

public:
	AInteractableActor();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	/** Runs the interaction for the operative that reached the object. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Interactables")
	void Interact(AOperativeCharacter* User);

	/** Distance from Location to the object's collision box, cm (0 inside). */
	float GetDistanceTo(const FVector& Location) const;

	/** Nearest reachable point next to the object for an operative coming from FromLocation. */
	FVector GetApproachPoint(const FVector& FromLocation) const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Interactables")
	EInteractableType ObjectType = EInteractableType::GateTerminal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Interactables")
	FText DisplayName;

	/** Interaction happens once an operative is this close to the object box, cm (architect spec: 150). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Interactables", meta = (ClampMin = "0"))
	float InteractionDistance = 150.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Interactables")
	TObjectPtr<UBoxComponent> Box;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Interactables")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** Warm zone, used by generators. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Interactables")
	TObjectPtr<UHeatSourceComponent> HeatSource;

private:
	UFUNCTION()
	void HandleGeneratorStarted();
};
