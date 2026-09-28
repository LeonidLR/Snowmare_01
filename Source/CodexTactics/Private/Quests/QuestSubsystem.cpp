#include "Quests/QuestSubsystem.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "UI/GameMessageSubsystem.h"

bool UQuestSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UQuestSubsystem::InteractWith(EInteractableType ObjectType, AActor* ObjectActor)
{
	const FText ObjectiveBefore = State.GetObjective();
	const FQuestInteractionResult Result = State.Interact(ObjectType);

	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(Result.Speaker, Result.Text);
	}

	switch (Result.Event)
	{
	case EQuestEvent::CanisterPickedUp:
		if (ObjectActor)
		{
			ObjectActor->SetActorHiddenInGame(true);
			ObjectActor->SetActorEnableCollision(false);
		}
		break;
	case EQuestEvent::GeneratorStarted:
		OnGeneratorStarted.Broadcast();
		break;
	case EQuestEvent::GateOpened:
		OnGateOpened.Broadcast();
		{
			FTimerHandle Handle;
			GetWorld()->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateUObject(this, &UQuestSubsystem::StartPreCombatCutscene),
				FMath::Max(CutsceneDelayAfterGate, KINDA_SMALL_NUMBER), false);
		}
		break;
	default:
		break;
	}

	const FText ObjectiveAfter = State.GetObjective();
	if (!ObjectiveAfter.EqualTo(ObjectiveBefore))
	{
		OnObjectiveChanged.Broadcast(ObjectiveAfter);
	}
}

void UQuestSubsystem::CompleteChainForCombat()
{
	const FText ObjectiveBefore = State.GetObjective();
	State.bHasEmptyCanister = true;
	State.bHasFuelCanister = true;
	State.bIsGeneratorRunning = true;
	State.bIsGatePowered = true;
	OnGeneratorStarted.Broadcast();
	OnGateOpened.Broadcast();
	const FText ObjectiveAfter = State.GetObjective();
	if (!ObjectiveAfter.EqualTo(ObjectiveBefore))
	{
		OnObjectiveChanged.Broadcast(ObjectiveAfter);
	}
}

void UQuestSubsystem::StartPreCombatCutscene()
{
	if (UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>())
	{
		Flow->TriggerCombatZone();
	}
}
