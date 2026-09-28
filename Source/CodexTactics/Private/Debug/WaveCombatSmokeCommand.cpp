// Dev-only console command for a headless check of enemy archetypes and wave spawning on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.WaveCombatSmoke

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/EncounterQueries.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Data/CombatTypes.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "TimerManager.h"

namespace WaveCombatSmoke
{
	constexpr float NavWarmupSeconds = 2.0f;
	constexpr float StepSeconds = 0.25f;

	struct FState
	{
		int32 Stage = 0;
		float StageTime = 0.0f;
		TWeakObjectPtr<AEnemyCharacter> Hound;
		TWeakObjectPtr<AEnemyCharacter> Spitter;
		TWeakObjectPtr<AEnemyCharacter> Brute;
		TArray<FString> Failures;
	};

	void Check(FState& State, bool bOk, const TCHAR* What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke check %-55s %s"), What, bOk ? TEXT("ok") : TEXT("FAIL"));
		if (!bOk)
		{
			State.Failures.Add(What);
		}
	}

	void NextStage(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.0f;
	}

	bool Step(UWorld* World, FState& State)
	{
		State.StageTime += StepSeconds;
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		UWaveSubsystem* WaveSub = World->GetSubsystem<UWaveSubsystem>();

		if (!Flow || !WaveSub)
		{
			Check(State, false, TEXT("subsystems present"));
			return false;
		}

		switch (State.Stage)
		{
		case 0:
			// Spawn 3 enemy archetypes: Hound, Spitter, Brute
			State.Hound = WaveSub->SpawnEnemy(EEnemyArchetype::FrostHound, FVector(500.f, -2200.f, 100.f));
			State.Spitter = WaveSub->SpawnEnemy(EEnemyArchetype::Spitter, FVector(600.f, -2200.f, 100.f));
			State.Brute = WaveSub->SpawnEnemy(EEnemyArchetype::Brute, FVector(700.f, -2200.f, 100.f));

			Check(State, State.Hound.IsValid(), TEXT("Hound spawned"));
			Check(State, State.Spitter.IsValid(), TEXT("Spitter spawned"));
			Check(State, State.Brute.IsValid(), TEXT("Brute spawned"));
			Check(State, WaveSub->GetAliveEnemyCount() == 3, TEXT("WaveSubsystem tracks 3 alive enemies"));

			NextStage(State);
			break;

		case 1:
			// Verify Enemy tags and CombatQueries detection
			Check(State, State.Hound->ActorHasTag(FName(TEXT("Enemy"))), TEXT("Hound tagged Enemy"));
			Check(State, State.Spitter->ActorHasTag(FName(TEXT("Enemy"))), TEXT("Spitter tagged Enemy"));
			Check(State, State.Brute->ActorHasTag(FName(TEXT("Enemy"))), TEXT("Brute tagged Enemy"));
			Check(State, CombatQueries::HasEnemiesWithin(World, FVector(600.f, -2200.f, 100.f), 500.f), TEXT("CombatQueries detects spawned enemies"));

			NextStage(State);
			break;

		case 2:
			// Verify stats per archetype
			Check(State, State.Hound->GetHealthComponent()->GetMaxHealth() == 45.0f, TEXT("Hound has 45 HP"));
			Check(State, State.Spitter->GetHealthComponent()->GetMaxHealth() == 70.0f, TEXT("Spitter has 70 HP"));
			Check(State, State.Brute->GetHealthComponent()->GetMaxHealth() == 220.0f, TEXT("Brute has 220 HP"));
			Check(State, State.Brute->GetHealthComponent()->GetArmorTier() == EArmorTier::Heavy, TEXT("Brute has Heavy Armor"));

			NextStage(State);
			break;

		case 3:
			// Test lethal damage and death reaction
			{
				FDamageSpec Lethal;
				Lethal.Amount = 5000.0f;
				Lethal.ArmorPenetration = 1.0f;
				Lethal.DamageType = EDamageType::Kinetic;
				State.Hound->GetHealthComponent()->TakeDamage(Lethal);
				State.Spitter->GetHealthComponent()->TakeDamage(Lethal);
				State.Brute->GetHealthComponent()->TakeDamage(Lethal);
			}

			Check(State, State.Hound->IsDying(), TEXT("Hound is dying"));
			Check(State, State.Spitter->IsDying(), TEXT("Spitter is dying"));
			Check(State, State.Brute->IsDying(), TEXT("Brute is dying"));
			Check(State, !State.Hound->ActorHasTag(FName(TEXT("Enemy"))), TEXT("Dying enemy loses Enemy tag"));

			NextStage(State);
			break;

		case 4:
			// Verify alive enemy count pruned to zero
			Check(State, WaveSub->GetAliveEnemyCount() == 0, TEXT("Alive enemy count pruned to 0"));

			// Finish smoke test
			if (State.Failures.Num() == 0)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: PASS"));
			}
			else
			{
				UE_LOG(LogCodexTactics, Error, TEXT("Smoke RESULT: FAIL (%d failures)"), State.Failures.Num());
			}

			FPlatformMisc::RequestExit(false, TEXT("WaveCombatSmoke"));
			return false;
		}

		return true;
	}

	void Start(UWorld* World)
	{
		TSharedRef<FState> State = MakeShared<FState>();
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			UWorld* W = WeakWorld.Get();
			return W && Step(W, *State);
		}), StepSeconds);
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
		TEXT("CodexTactics.WaveCombatSmoke"),
		TEXT("Dev check: enemy archetypes, stats, tagging, and wave subsystem alive tracking."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
