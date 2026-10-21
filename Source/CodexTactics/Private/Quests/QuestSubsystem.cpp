#include "Quests/QuestSubsystem.h"
#include "Core/PlayerCommands.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/Actor.h"
#include "Interactables/GateActor.h"
#include "Quests/GateProgressRules.h"
#include "TimerManager.h"
#include "UI/GameMessageSubsystem.h"

bool UQuestSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UQuestSubsystem::InteractWith(EInteractableType ObjectType, AActor* ObjectActor, AActor* InstigatorActor)
{
	const FText ObjectiveBefore = GetObjective();
	const FQuestChainState StateBefore = State;
	FQuestInteractionResult Result = State.Interact(ObjectType);
	// User request 2026-10-11: a gate paced by the generator and the waves does not "swing open" at the terminal.
	if (const AGateActor* Gate = AGateActor::FindLevelGate(GetWorld()); Gate && Gate->UsesGeneratorPacing())
	{
		GateProgressRules::AdjustQuestLine(ObjectType, State.bIsGatePowered, Gate->IsOpen() || Gate->IsOpening(), Result);
	}

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

	const FText ObjectiveAfter = GetObjective();
	if (!ObjectiveAfter.EqualTo(ObjectiveBefore))
	{
		OnObjectiveChanged.Broadcast(ObjectiveAfter);
	}

	// 2026-10-21 (Demo 01 A7): the tutorial follows the chain ("QuestStep.FuelDrained" first, then "QuestStep").
	const FName Step = FQuestChainState::GetStepEvent(StateBefore, State);
	if (!Step.IsNone())
	{
		FGameplayEventContext Context;
		Context.Subject = InstigatorActor;
		Context.Target = ObjectActor;
		Context.Kind = Step;
		GameplayEvents::RaiseWithContext(this, FName(*(GameplayEvents::QuestStep.ToString() + TEXT(".") + Step.ToString())), Context);
		GameplayEvents::RaiseWithContext(this, GameplayEvents::QuestStep, Context);
	}
}

FText UQuestSubsystem::GetObjective() const
{
	const AGateActor* Gate = State.bIsGatePowered ? AGateActor::FindLevelGate(GetWorld()) : nullptr;
	if (Gate && Gate->UsesGeneratorPacing() && !Gate->IsOpen() && !Gate->IsOpening())
	{
		return GateProgressRules::GetHoldObjective();
	}
	return State.GetObjective();
}

void UQuestSubsystem::CompleteChainForCombat()
{
	const FText ObjectiveBefore = GetObjective();
	State.bHasEmptyCanister = true;
	State.bHasFuelCanister = true;
	State.bIsGeneratorRunning = true;
	State.bIsGatePowered = true;
	OnGeneratorStarted.Broadcast();
	OnGateOpened.Broadcast();
	// «Start Battle» puts the squad behind the gate (CombatStart) while the waves spawn on the near side of the
	// checkpoint: the paced gate opens at once here (only the playthrough from «Start Game» holds it shut for the waves).
	for (TActorIterator<AGateActor> It(GetWorld()); It; ++It)
	{
		It->OpenGate();
	}
	const FText ObjectiveAfter = GetObjective();
	if (!ObjectiveAfter.EqualTo(ObjectiveBefore))
	{
		OnObjectiveChanged.Broadcast(ObjectiveAfter);
	}
}

void UQuestSubsystem::RestoreState(const FQuestChainState& Saved)
{
	State = Saved;
	if (State.bIsGeneratorRunning)
	{
		OnGeneratorStarted.Broadcast();
	}
	if (State.bIsGatePowered)
	{
		OnGateOpened.Broadcast(); // the gate actor opens; the cutscene only follows a terminal interaction
	}
	OnObjectiveChanged.Broadcast(GetObjective());
}

void UQuestSubsystem::StartPreCombatCutscene()
{
	if (UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>())
	{
		Flow->TriggerCombatZone();
	}
}
