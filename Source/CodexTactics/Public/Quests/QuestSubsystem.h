#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Quests/QuestChain.h"
#include "QuestSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGeneratorStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGateOpened);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnObjectiveChanged, const FText&, Objective);

/**
 * Checkpoint quest chain of the level: routes interactions into FQuestChainState, posts the radio lines to
 * UGameMessageSubsystem and broadcasts generator / gate / objective events.
 * Opening the gate starts the pre-combat cutscene 1 s later (Godot main.gd `_on_gate_opened`).
 * Godot reference: Scenes/movements/quest_manager.gd, main.gd `_update_objective_by_state`.
 */
UCLASS()
class CODEXTACTICS_API UQuestSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	/** Applies an interaction with a quest object; ObjectActor is hidden when a canister is picked up. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Quests")
	void InteractWith(EInteractableType ObjectType, AActor* ObjectActor);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Quests")
	bool HasEmptyCanister() const { return State.bHasEmptyCanister; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Quests")
	bool HasFuelCanister() const { return State.bHasFuelCanister; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Quests")
	bool IsGeneratorRunning() const { return State.bIsGeneratorRunning; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Quests")
	bool IsGatePowered() const { return State.bIsGatePowered; }

	/** Objective banner text (without the «ЦЕЛЬ: » prefix). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Quests")
	FText GetObjective() const { return State.GetObjective(); }

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Quests")
	FOnGeneratorStarted OnGeneratorStarted;

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Quests")
	FOnGateOpened OnGateOpened;

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Quests")
	FOnObjectiveChanged OnObjectiveChanged;

	/**
	 * «Начать бой» from the main menu: every chain step done at once (generator running, gate powered and open).
	 * Godot _on_start_combat_pressed sets the quest_manager flags and calls _on_gate_opened.
	 */
	void CompleteChainForCombat();

	/** Delay between opening the gate and the pre-combat cutscene, s (Godot: 1.0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Quests", meta = (ClampMin = "0"))
	float CutsceneDelayAfterGate = 1.f;

private:
	void StartPreCombatCutscene();

	FQuestChainState State;
};
