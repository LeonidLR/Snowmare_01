#pragma once

#include "CoreMinimal.h"
#include "QuestChain.generated.h"

/** Interactive object kinds of the checkpoint quest chain. Godot: InteractableObject.ObjectType / QuestManager.ObjectType. */
UENUM(BlueprintType)
enum class EInteractableType : uint8
{
	GateTerminal,
	Canister,
	Vehicle,
	Generator,
	Gate
};

/** Side effect a quest interaction triggers. */
UENUM(BlueprintType)
enum class EQuestEvent : uint8
{
	None,
	/** The canister was picked up: hide it in the world. */
	CanisterPickedUp,
	GeneratorStarted,
	GateOpened
};

/** Outcome of one interaction: the radio line to show and the event to apply. */
struct CODEXTACTICS_API FQuestInteractionResult
{
	FText Speaker;
	FText Text;
	EQuestEvent Event = EQuestEvent::None;
};

/**
 * Pure state of the checkpoint quest chain: canister -> diesel from the APC -> generator -> gate terminal -> gate.
 * Godot reference: Scenes/movements/quest_manager.gd (lines verbatim) and main.gd `_update_objective_by_state`.
 */
struct CODEXTACTICS_API FQuestChainState
{
	bool bHasEmptyCanister = false;
	bool bHasFuelCanister = false;
	bool bIsGeneratorRunning = false;
	bool bIsGatePowered = false;

	/** Applies an interaction with an object of the given type. */
	FQuestInteractionResult Interact(EInteractableType Type);

	/** Objective banner text for the current state (without the "OBJECTIVE: " prefix). */
	FText GetObjective() const;
};
