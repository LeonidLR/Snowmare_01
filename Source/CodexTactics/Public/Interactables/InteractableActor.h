#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interactables/ActionMenuTypes.h"
#include "Quests/QuestChain.h"
#include "InteractableActor.generated.h"

class AOperativeCharacter;
class UBoxComponent;
class UHeatSourceComponent;
class UStaticMeshComponent;

/**
 * Interactive object. Clicking it sends the squad leader to it; on arrival the object's action menu opens
 * (BuildActionMenu) and the confirm button runs ExecuteAction. The base class is the checkpoint quest object
 * (gate terminal, canister, abandoned APC, backup generator, gate) routed through UQuestSubsystem; a generator
 * carries a heat source that turns on when it starts. Subclasses (barrels, deployables) override the menu.
 * Godot reference: Scenes/movements/interactable.gd, main.gd `_trigger_menu_for_object` / `_on_action_confirmed`.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API AInteractableActor : public AActor
{
	GENERATED_BODY()

public:
	AInteractableActor();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	/** Runs the quest interaction for the operative that reached the object. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Interactables")
	void Interact(AOperativeCharacter* User);

	/** Menu (or feed line) for the leader standing at the object. */
	virtual FActionMenuRequest BuildActionMenu(const AOperativeCharacter* Leader) const;

	/** Confirm button of the menu. Default: the quest interaction. */
	virtual void ExecuteAction(AOperativeCharacter* User);

	/** Godot can_be_relocated: the object can be pushed / carried to a new spot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Interactables")
	bool bCanBeRelocated = false;

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
