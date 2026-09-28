// Dev-only console command for a headless check of the checkpoint quest chain on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.QuestChainSmoke
// Uses the player's click path (UInteractionSubsystem::RequestInteraction) for: APC without canister (refusal),
// canister, APC, generator, gate terminal. Then checks the generator heat, the opened gate, the cutscene ->
// preparation transition and that the leader can walk through the gate.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/GateActor.h"
#include "Interactables/HeatSourceComponent.h"
#include "Interactables/InteractableActor.h"
#include "Interactables/InteractionSubsystem.h"
#include "Quests/QuestSubsystem.h"
#include "TimerManager.h"

namespace QuestChainSmoke
{
	constexpr float NavWarmupSeconds = 3.f;
	constexpr float StepSeconds = 0.5f;
	constexpr float StepTimeout = 25.f;
	const FVector BeyondGate(0.f, -2600.f, 100.f);

	struct FState
	{
		TArray<EInteractableType> Sequence = { EInteractableType::Vehicle, EInteractableType::Canister,
			EInteractableType::Vehicle, EInteractableType::Generator, EInteractableType::GateTerminal };
		int32 Index = 0;
		bool bRequested = false;
		float StepTime = 0.f;
		float TotalTime = 0.f;
		bool bWalkOrdered = false;
		bool bFailed = false;
	};

	AInteractableActor* FindInteractable(UWorld* World, EInteractableType Type)
	{
		for (TActorIterator<AInteractableActor> It(World); It; ++It)
		{
			if (It->ObjectType == Type)
			{
				return *It;
			}
		}
		return nullptr;
	}

	void Finish(UWorld* World, bool bPass, const TCHAR* Reason)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s"), Reason);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bPass ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("QuestChainSmoke"));
	}

	/** Returns false when the smoke finished. */
	bool Step(UWorld* World, FState& State)
	{
		State.StepTime += StepSeconds;
		State.TotalTime += StepSeconds;
		UInteractionSubsystem* Interactions = World->GetSubsystem<UInteractionSubsystem>();
		UQuestSubsystem* Quests = World->GetSubsystem<UQuestSubsystem>();
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		AOperativeCharacter* Leader = World->GetSubsystem<USquadSubsystem>()->GetLeader();

		if (State.Index < State.Sequence.Num())
		{
			AInteractableActor* Target = FindInteractable(World, State.Sequence[State.Index]);
			if (!Target)
			{
				Finish(World, false, TEXT("missing quest object"));
				return false;
			}
			if (!State.bRequested)
			{
				Interactions->RequestInteraction(Target);
				State.bRequested = true;
				State.StepTime = 0.f;
			}
			else if (!Interactions->GetPendingInteraction())
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke step %d %s done at %.1fs: empty=%d fuel=%d generator=%d gate=%d | %s"),
					State.Index, *UEnum::GetValueAsString(State.Sequence[State.Index]), State.TotalTime,
					Quests->HasEmptyCanister() ? 1 : 0, Quests->HasFuelCanister() ? 1 : 0,
					Quests->IsGeneratorRunning() ? 1 : 0, Quests->IsGatePowered() ? 1 : 0, *Quests->GetObjective().ToString());
				++State.Index;
				State.bRequested = false;
			}
			else if (State.StepTime > StepTimeout)
			{
				Finish(World, false, TEXT("interaction timed out (leader never reached the object)"));
				return false;
			}
			return true;
		}

		AGateActor* Gate = nullptr;
		for (TActorIterator<AGateActor> It(World); It; ++It)
		{
			Gate = *It;
		}
		AInteractableActor* Generator = FindInteractable(World, EInteractableType::Generator);
		const bool bHeat = Generator && Generator->HeatSource->IsHeatActive();
		const bool bGateOpen = Gate && Gate->IsOpen();
		const bool bPreparation = Flow->GetPhase() == ECodexGamePhase::Preparation;

		if (bGateOpen && bPreparation && !State.bWalkOrdered)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke gate open, phase Preparation at %.1fs, heat=%d"), State.TotalTime, bHeat ? 1 : 0);
			Leader->OrderMoveTo(BeyondGate, true);
			State.bWalkOrdered = true;
			State.StepTime = 0.f;
			return true;
		}
		if (State.bWalkOrdered)
		{
			if (FVector::Dist2D(Leader->GetActorLocation(), BeyondGate) < 60.f)
			{
				Finish(World, bHeat && bGateOpen && Quests->IsGatePowered(), TEXT("leader walked through the gate"));
				return false;
			}
			if (State.StepTime > StepTimeout)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke leader stuck at (%.0f, %.0f)"), Leader->GetActorLocation().X, Leader->GetActorLocation().Y);
				Finish(World, false, TEXT("leader could not pass the gate"));
				return false;
			}
			return true;
		}
		if (State.StepTime > StepTimeout)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke gateOpen=%d phase=%s"), bGateOpen ? 1 : 0, *UEnum::GetValueAsString(Flow->GetPhase()));
			Finish(World, false, TEXT("gate / cutscene did not complete"));
			return false;
		}
		return true;
	}

	void Start(UWorld* World)
	{
		TSharedRef<FState> State = MakeShared<FState>();
		TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
		TWeakObjectPtr<UWorld> WeakWorld(World);
		World->GetTimerManager().SetTimer(*Handle, FTimerDelegate::CreateLambda([WeakWorld, State, Handle]()
		{
			if (UWorld* W = WeakWorld.Get())
			{
				if (!Step(W, *State))
				{
					W->GetTimerManager().ClearTimer(*Handle);
				}
			}
		}), StepSeconds, true);
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld]()
		{
			if (UWorld* W = WeakWorld.Get())
			{
				Start(W);
			}
		}), NavWarmupSeconds, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.QuestChainSmoke"),
		TEXT("Dev check: runs the checkpoint quest chain through the click/approach path and walks through the opened gate."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
