// Dev-only console command for a headless start-menu check on L_MovementTest (needs -ForceMainMenu):
//   Scripts/smoke.ps1 -Command CodexTactics.MainMenuSmoke -Extra "-ForceMainMenu"
// 1. the menu is open and the world paused; 2. "Start Battle" closes it, completes the quest chain, heals / warms the
// squad and starts the pre-combat cutscene; 3. Ctrl + X (quick restart) reloads straight into combat mode;
// 4. a normal restart ("Restart") shows the menu again.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Containers/Ticker.h"
#include "Core/MissionSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Quests/QuestSubsystem.h"

namespace MainMenuSmoke
{
	constexpr float StepSeconds = 0.25f;

	struct FState
	{
		int32 Stage = 0;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		TWeakObjectPtr<UWorld> LastWorld;
	};

	UWorld* FindGameWorld()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::Game && Context.World() && Context.World()->HasBegunPlay())
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	void Check(FState& State, bool bOk, const TCHAR* What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("MainMenuSmoke"));
		return false;
	}

	/** Runs on the core ticker, so it survives the level reloads. */
	bool Step(FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		if (State.Time > 60.f)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in stage %d"), State.Stage);
			return Finish(State, false);
		}
		UWorld* World = FindGameWorld();
		const bool bNewWorld = World && World != State.LastWorld.Get();
		if (!World || State.StageTime < 1.f)
		{
			return true;
		}
		UMissionSubsystem* Mission = World->GetSubsystem<UMissionSubsystem>();
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		switch (State.Stage)
		{
		case 0: // First load: menu open, world paused.
			State.LastWorld = World;
			Check(State, Mission->IsMainMenuOpen(), TEXT("menu open at start"));
			Check(State, UGameplayStatics::IsGamePaused(World), TEXT("world paused behind the menu"));
			for (AOperativeCharacter* Member : World->GetSubsystem<USquadSubsystem>()->GetMembers())
			{
				Member->ColdLevel = 50.f;
				Member->HealthComponent->ApplyDirectHealthLoss(30.f, TEXT("Smoke"));
			}
			Mission->StartMission(EMissionStartMode::Combat);
			Check(State, !Mission->IsMainMenuOpen() && !UGameplayStatics::IsGamePaused(World), TEXT("combat start closes the menu"));
			Check(State, World->GetSubsystem<UQuestSubsystem>()->IsGatePowered() && World->GetSubsystem<UQuestSubsystem>()->IsGeneratorRunning(),
				TEXT("quest chain completed"));
			Check(State, Flow->GetPhase() == ECodexGamePhase::Cutscene, TEXT("pre-combat cutscene started"));
			{
				bool bFresh = true;
				for (const AOperativeCharacter* Member : World->GetSubsystem<USquadSubsystem>()->GetMembers())
				{
					bFresh &= Member->ColdLevel == 0.f && Member->HealthComponent->GetCurrentHealth() >= Member->HealthComponent->GetMaxHealth();
				}
				Check(State, bFresh, TEXT("squad healed and warmed"));
			}
			Mission->RestartMission(/*bQuick*/ true);
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
		case 1: // After Ctrl + X: straight into combat mode.
			if (!bNewWorld)
			{
				return true;
			}
			State.LastWorld = World;
			Check(State, !Mission->IsMainMenuOpen() && Mission->GetStartMode() == EMissionStartMode::Combat, TEXT("quick restart repeats combat mode"));
			Check(State, Flow->GetPhase() == ECodexGamePhase::Cutscene, TEXT("quick restart: cutscene again"));
			Mission->RestartMission(/*bQuick*/ false);
			State.Stage = 2;
			State.StageTime = 0.f;
			return true;
		case 2: // After "Restart": menu again.
			if (!bNewWorld)
			{
				return true;
			}
			Check(State, Mission->IsMainMenuOpen(), TEXT("normal restart shows the menu"));
			return Finish(State, true);
		default:
			return Finish(State, false);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float)
		{
			return Step(*State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.MainMenuSmoke"),
		TEXT("Dev check (run with -ForceMainMenu): start menu, combat mode, quick restart, normal restart; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
