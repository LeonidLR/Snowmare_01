// Dev-only console command for a headless level-wave check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.LevelWaveSmoke
// 1. the game flow took the level (3 waves, preparation 60 s, rest 20 s);
// 2. «Начать бой» -> wave 1 appears at once like Godot _spawn_custom_json_wave (the level's wave 1 count, nothing queued)
//    around the level's enemy spawn points, and the wave's cold_drain_mult reaches the squad's cold;
// 3. (user report 2026-10-04: wave 2 spawned into one point, the enemies inside each other, standing still) for waves 1
//    and 2: nobody spawns inside another enemy, everyone stands on the navmesh, and 6 s later they are on the move.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "CodexTactics.h"
#include "Data/WaveConfigTypes.h"
#include "Core/CodexTacticsGameMode.h"
#include "Combat/EnemySpawnPoint.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "NavigationSystem.h"

namespace LevelWaveSmoke
{
	constexpr float StepSeconds = 0.25f;
	/** Enemies are spread round the point (WaveSubsystem::FindFreeSpawnSpot, up to 6 m). */
	constexpr float SpawnPointTolerance = 750.f;
	/** Closer than this the capsules (radius ~45-55 cm) overlap. */
	constexpr float MinSeparation = 80.f;

	struct FState
	{
		int32 Stage = 0;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		int32 Wave = 1;
		TMap<TWeakObjectPtr<AEnemyCharacter>, FVector> Start;
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

	/** Separation, navmesh and spawn-point checks right after a wave spawned; remembers where everyone started. */
	void CheckSpawned(UWorld* World, FState& State)
	{
		TArray<FVector> Points;
		for (TActorIterator<AEnemySpawnPoint> It(World); It; ++It)
		{
			Points.Add(It->GetActorLocation());
		}
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		TArray<AEnemyCharacter*> Enemies;
		int32 NearPoint = 0;
		int32 OnNav = 0;
		State.Start.Reset();
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			if (It->IsDying())
			{
				continue;
			}
			Enemies.Add(*It);
			State.Start.Add(*It, It->GetActorLocation());
			for (const FVector& Point : Points)
			{
				if (FVector::Dist2D(Point, It->GetActorLocation()) <= SpawnPointTolerance)
				{
					++NearPoint;
					break;
				}
			}
			FNavLocation Projected;
			const FVector Feet = It->GetActorLocation() - FVector(0.f, 0.f, It->GetSimpleCollisionHalfHeight());
			OnNav += Nav && Nav->ProjectPointToNavigation(Feet, Projected, FVector(100.f, 100.f, 250.f)) ? 1 : 0;
		}
		float Closest = TNumericLimits<float>::Max();
		for (int32 A = 0; A < Enemies.Num(); ++A)
		{
			for (int32 B = A + 1; B < Enemies.Num(); ++B)
			{
				Closest = FMath::Min(Closest, static_cast<float>(FVector::Dist2D(Enemies[A]->GetActorLocation(), Enemies[B]->GetActorLocation())));
			}
		}
		Check(State, Points.Num() > 0 && Enemies.Num() > 0 && NearPoint == Enemies.Num(),
			FString::Printf(TEXT("wave %d: %d / %d enemies around the %d spawn points"), State.Wave, NearPoint, Enemies.Num(), Points.Num()));
		Check(State, Closest >= MinSeparation, FString::Printf(TEXT("wave %d: nobody inside another (closest pair %.0f cm)"), State.Wave, Closest));
		Check(State, OnNav == Enemies.Num(), FString::Printf(TEXT("wave %d: %d / %d on the navmesh"), State.Wave, OnNav, Enemies.Num()));
	}

	/** 6 s after the spawn: (nearly) everyone has walked off its spawn spot. */
	void CheckMoving(FState& State)
	{
		int32 Moved = 0;
		int32 Alive = 0;
		for (const TPair<TWeakObjectPtr<AEnemyCharacter>, FVector>& Entry : State.Start)
		{
			if (const AEnemyCharacter* Enemy = Entry.Key.Get(); Enemy && !Enemy->IsDying())
			{
				++Alive;
				Moved += FVector::Dist2D(Enemy->GetActorLocation(), Entry.Value) > 150.f ? 1 : 0;
			}
		}
		Check(State, Alive > 0 && Moved * 10 >= Alive * 9, FString::Printf(TEXT("wave %d: %d / %d on the move after 6 s"), State.Wave, Moved, Alive));
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 45.f)
		{
			return World ? Finish(State, false) : false;
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		if (!Flow || !Waves)
		{
			return Finish(State, false);
		}
		switch (State.Stage)
		{
		case 0:
		{
			if (State.Time < 2.f)
			{
				return true;
			}
			const FGameFlowConfig& Config = Flow->GetConfig();
			Check(State, Config.TotalWaves == 3, FString::Printf(TEXT("level waves: %d"), Config.TotalWaves));
			Check(State, FMath::IsNearlyEqual(Config.PreparationDuration, 60.f) && FMath::IsNearlyEqual(Config.WaveRestDuration, 20.f),
				FString::Printf(TEXT("preparation %.0f s, rest %.0f s"), Config.PreparationDuration, Config.WaveRestDuration));
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			Check(State, Flow->GetPhase() == ECodexGamePhase::WaveCombat, TEXT("wave 1 started"));
			// The wave 1 size comes from the level data (edited in the Wave Editor).
			const ACodexTacticsGameMode* GameMode = World->GetAuthGameMode<ACodexTacticsGameMode>();
			const ULevelConfigAsset* Level = GameMode ? GameMode->GetActiveLevelConfig() : nullptr;
			const int32 Expected = Level && Level->Config.Waves.Num() > 0 ? Level->Config.Waves[0].GetTotalEnemyCount() : 12;
			Check(State, Waves->GetAliveEnemyCount() == Expected && Waves->GetRemainingSpawnCount() == 0,
				FString::Printf(TEXT("whole wave at once: alive %d, queued %d"), Waves->GetAliveEnemyCount(), Waves->GetRemainingSpawnCount()));
			const float ColdMult = Level && Level->Config.Waves.Num() > 0 ? Level->Config.Waves[0].Modifiers.ColdDrainMult : 1.1f;
			Check(State, FMath::IsNearlyEqual(Waves->GetColdDrainMultiplier(), ColdMult),
				FString::Printf(TEXT("cold drain multiplier %.2f"), Waves->GetColdDrainMultiplier()));
			CheckSpawned(World, State);
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
		}
		case 1:
		case 3:
			if (State.StageTime < 6.f)
			{
				return true;
			}
			CheckMoving(State);
			if (State.Stage == 3)
			{
				return Finish(State, true);
			}
			// Wave 2 straight away (the reported case), without wave 1 in the way.
			Waves->ClearAllEnemies();
			State.Wave = 2;
			Waves->StartWave(2);
			State.Stage = 2;
			State.StageTime = 0.f;
			return true;
		case 2:
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			CheckSpawned(World, State);
			State.Stage = 3;
			State.StageTime = 0.f;
			return true;
		default:
			return Finish(State, false);
		}
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
		TEXT("Dev check: level config drives the flow; waves 1 and 2 spawn spread out on the navmesh and walk; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
