// Dev-only console command for a headless mission check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.MissionSmoke
// 1. start objective; 2. preparation / wave objectives follow the game flow; 3. an operative killed by wounds fails
// the mission (GameOver, reason, time stopped); 4. restart reloads the level into a fresh exploration.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Containers/Ticker.h"
#include "Core/MissionRules.h"
#include "Core/MissionSubsystem.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "UObject/UObjectGlobals.h"

namespace MissionSmoke
{
	constexpr float StepSeconds = 0.25f;

	struct FState
	{
		int32 Stage = 0;
		float Time = 0.f;
		int32 Failures = 0;
	};

	int32 GFailures = 0;
	FDelegateHandle GReloadHandle;

	void Check(FState& State, bool bOk, const TCHAR* What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), What);
		State.Failures += bOk ? 0 : 1;
	}

	void Finish(int32 Failures, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("MissionSmoke"));
	}

	void HandleReloaded(UWorld* World)
	{
		FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(GReloadHandle);
		const UMissionSubsystem* Mission = World ? World->GetSubsystem<UMissionSubsystem>() : nullptr;
		const UGameFlowSubsystem* Flow = World ? World->GetSubsystem<UGameFlowSubsystem>() : nullptr;
		const bool bFresh = Mission && Flow && !Mission->IsMissionFailed() && Flow->GetPhase() == ECodexGamePhase::Exploration;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: restart reloaded a fresh exploration"), bFresh ? TEXT("ok  ") : TEXT("FAIL"));
		Finish(GFailures + (bFresh ? 0 : 1), true);
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		UWorld* World = WeakWorld.Get();
		State.Time += StepSeconds;
		if (!World || State.Time > 30.f)
		{
			Finish(State.Failures + 1, false);
			return false;
		}
		UMissionSubsystem* Mission = World->GetSubsystem<UMissionSubsystem>();
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		switch (State.Stage++)
		{
		case 0:
			Check(State, Mission->GetObjective().EqualTo(MissionRules::GetStartObjective()), TEXT("start objective"));
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke objective: %s"), *Mission->GetObjective().ToString());
			Check(State, Mission->GetObjective().ToString().StartsWith(TEXT("ПОДГОТОВКА К ОБОРОНЕ")), TEXT("preparation objective"));
			Flow->FinishPreparation();
			return true;
		case 1:
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke objective: %s"), *Mission->GetObjective().ToString());
			Check(State, Mission->GetObjective().ToString().StartsWith(TEXT("ОБОРОНА: Отразить волну 1! Врагов: ")), TEXT("wave objective"));
			return true;
		case 2:
		{
			AOperativeCharacter* Engineer = Squad->GetMembers().Num() > 1 ? Squad->GetMembers()[1] : nullptr;
			if (Engineer)
			{
				Engineer->ColdLevel = 10.f;
				Engineer->HealthComponent->ApplyDirectHealthLoss(10000.f, TEXT("Smoke"));
			}
			Check(State, Mission->IsMissionFailed(), TEXT("operative death fails the mission"));
			Check(State, Flow->GetPhase() == ECodexGamePhase::GameOver, TEXT("game over phase"));
			Check(State, Engineer && Mission->GetFailureReason().EqualTo(MissionRules::GetFailureReason(Engineer->DisplayName, 10.f)),
				TEXT("wounds reason"));
			Check(State, World->GetWorldSettings()->GetEffectiveTimeDilation() < 0.01f, TEXT("time stopped"));
			GFailures = State.Failures;
			GReloadHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddStatic(&HandleReloaded);
			Mission->RestartMission();
			return false;
		}
		default:
			return false;
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld, State]()
		{
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
			{
				return Step(WeakWorld, *State);
			}), StepSeconds);
		}), 2.f, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.MissionSmoke"),
		TEXT("Dev check: objective banner texts, mission failed on an operative death, restart; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
