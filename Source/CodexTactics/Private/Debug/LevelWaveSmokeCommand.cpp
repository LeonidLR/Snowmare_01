// Dev-only console command for a headless level-wave check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.LevelWaveSmoke
// 1. the game flow took the imported level (DA_Level_level_01_outpost: 3 waves, preparation 60 s, rest 20 s);
// 2. «Начать бой» -> wave 1 appears at once like Godot _spawn_custom_json_wave (12 enemies, nothing queued), at the
// level's enemy spawn points, and the wave's cold_drain_mult (1.1) reaches the squad's cold.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "CodexTactics.h"
#include "Combat/EnemySpawnPoint.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"

namespace LevelWaveSmoke
{
	constexpr float StepSeconds = 0.25f;
	/** Spawn collision handling may push crowded enemies off the point. */
	constexpr float SpawnPointTolerance = 1200.f;

	struct FState
	{
		int32 Stage = 0;
		float Time = 0.f;
		int32 Failures = 0;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("LevelWaveSmoke"));
		return false;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 20.f)
		{
			return Finish(State, false);
		}
		if (State.Time < 2.f)
		{
			return true;
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		if (!Flow || !Waves)
		{
			return Finish(State, false);
		}

		const FGameFlowConfig& Config = Flow->GetConfig();
		Check(State, Config.TotalWaves == 3, FString::Printf(TEXT("level waves: %d"), Config.TotalWaves));
		Check(State, FMath::IsNearlyEqual(Config.PreparationDuration, 60.f) && FMath::IsNearlyEqual(Config.WaveRestDuration, 20.f),
			FString::Printf(TEXT("preparation %.0f s, rest %.0f s"), Config.PreparationDuration, Config.WaveRestDuration));

		Flow->TriggerCombatZone();
		Flow->FinishCutscene();
		Flow->FinishPreparation();
		Check(State, Flow->GetPhase() == ECodexGamePhase::WaveCombat, TEXT("wave 1 started"));
		Check(State, Waves->GetAliveEnemyCount() == 12 && Waves->GetRemainingSpawnCount() == 0,
			FString::Printf(TEXT("whole wave at once: alive %d, queued %d"), Waves->GetAliveEnemyCount(), Waves->GetRemainingSpawnCount()));
		Check(State, FMath::IsNearlyEqual(Waves->GetColdDrainMultiplier(), 1.1f),
			FString::Printf(TEXT("cold drain multiplier %.2f"), Waves->GetColdDrainMultiplier()));

		TArray<FVector> Points;
		for (TActorIterator<AEnemySpawnPoint> It(World); It; ++It)
		{
			Points.Add(It->GetActorLocation());
		}
		int32 NearPoint = 0;
		int32 Enemies = 0;
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			++Enemies;
			for (const FVector& Point : Points)
			{
				if (FVector::Dist2D(Point, It->GetActorLocation()) <= SpawnPointTolerance)
				{
					++NearPoint;
					break;
				}
			}
		}
		Check(State, Points.Num() == 4 && Enemies > 0 && NearPoint == Enemies,
			FString::Printf(TEXT("%d / %d enemies at the %d spawn points"), NearPoint, Enemies, Points.Num()));
		return Finish(State, true);
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.LevelWaveSmoke"),
		TEXT("Dev check: imported level config drives the flow; wave 1 spawns at once at the spawn points; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
