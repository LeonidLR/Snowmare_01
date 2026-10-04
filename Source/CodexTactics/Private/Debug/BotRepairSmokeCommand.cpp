// Dev-only console command for a headless check of the playtest bot repairing the diesel generator on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.BotRepairSmoke
// «Начать бой», the generator breaks in the preparation, the bot starts: the engineer walks up and repairs it (the
// menu confirmed, 2.5 s), and the preparation does not end before the repair.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Bot/PlaytestBotSubsystem.h"
#include "CodexTactics.h"
#include "Core/MissionSessionSubsystem.h"
#include "Core/MissionSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/InteractableActor.h"
#include "TimerManager.h"

namespace BotRepairSmoke
{
	struct FState
	{
		float Time = 0.f;
		int32 Stage = 0;
		bool bPreparationHeld = true;
		TWeakObjectPtr<AInteractableActor> Generator;
	};

	bool Finish(bool bPass)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bPass ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("BotRepairSmoke"));
		return false;
	}

	bool Step(UWorld* World, FState& State)
	{
		State.Time += 0.1f;
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		UPlaytestBotSubsystem* Bot = World->GetSubsystem<UPlaytestBotSubsystem>();
		if (!Flow || !Bot)
		{
			return Finish(false);
		}
		if (State.Stage == 0)
		{
			if (UMissionSubsystem* Mission = World->GetSubsystem<UMissionSubsystem>())
			{
				Mission->StartMission(EMissionStartMode::Combat);
			}
			Flow->FinishCutscene();
			for (TActorIterator<AInteractableActor> It(World); It; ++It)
			{
				if (It->ObjectType == EInteractableType::Generator)
				{
					State.Generator = *It;
					It->TakeGeneratorDamage(100000.f);
				}
			}
			AInteractableActor* Generator = State.Generator.Get();
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: generator broken in the preparation (%s)"),
				Generator && Generator->bGeneratorBroken && Flow->GetPhase() == ECodexGamePhase::Preparation ? TEXT("ok  ") : TEXT("FAIL"),
				*UEnum::GetValueAsString(Flow->GetPhase()));
			if (!Generator || !Generator->bGeneratorBroken)
			{
				return Finish(false);
			}
			Bot->StartBot(EBotProfile::Normal, false, false);
			State.Stage = 1;
			State.Time = 0.f;
			return true;
		}
		const AInteractableActor* Generator = State.Generator.Get();
		const bool bRepaired = Generator && !Generator->bGeneratorBroken;
		if (!bRepaired)
		{
			State.bPreparationHeld &= Flow->GetPhase() == ECodexGamePhase::Preparation || Flow->GetPhase() == ECodexGamePhase::Cutscene;
			return State.Time < 60.f ? true : Finish(false);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: repaired by the bot in %.1f s (repairs %d), the preparation waited %d"),
			Bot->GetGeneratorRepairs() == 1 && State.bPreparationHeld ? TEXT("ok  ") : TEXT("FAIL"), State.Time, Bot->GetGeneratorRepairs(),
			State.bPreparationHeld ? 1 : 0);
		return Finish(Bot->GetGeneratorRepairs() >= 1 && State.bPreparationHeld);
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
		FTimerHandle StartHandle;
		World->GetTimerManager().SetTimer(StartHandle, FTimerDelegate::CreateLambda([WeakWorld, State, Handle]()
		{
			if (UWorld* W = WeakWorld.Get())
			{
				W->GetTimerManager().SetTimer(*Handle, FTimerDelegate::CreateLambda([WeakWorld, State, Handle]()
				{
					UWorld* W2 = WeakWorld.Get();
					if (W2 && !Step(W2, *State))
					{
						W2->GetTimerManager().ClearTimer(*Handle);
					}
				}), 0.1f, true);
			}
		}), 3.f, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(TEXT("CodexTactics.BotRepairSmoke"),
		TEXT("Dev check: the playtest bot's engineer repairs a broken generator in the preparation; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
