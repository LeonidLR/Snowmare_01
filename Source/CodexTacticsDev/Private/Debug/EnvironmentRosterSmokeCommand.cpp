// Dev-only console command for a headless check of the enemy roster by time of day + weather (environment phase 2b;
// EnvironmentRosterRules hooked into UWaveSubsystem::StartWave; docs/systems/environment-weather.md §4.2, roster v2):
//   Scripts/smoke.ps1 -Command CodexTactics.EnvironmentRosterSmoke -Map /Game/Maps/L_MovementTest
// 1. the environment is initialised and not locked; the level's written wave 2 has MARKSMAN entries (level_01_outpost);
// 2. Blizzard at night (forced, clock stopped): wave 2 starts with NO marksman - every one became 2 FROSTBITTEN (resolved entries
//    and the living enemies), the whole resolved wave is alive;
// 3. Clear at midday: wave 2 again, the marksmen are back as written (same frame as 2; the wave is cleared before it ticks).
// Prints "Smoke ok/FAIL" lines and "Smoke RESULT: PASS|FAIL", then exits.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "CodexTactics.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsGameMode.h"
#include "Data/WaveConfigTypes.h"
#include "Engine/World.h"
#include "Environment/EnvironmentSubsystem.h"
#include "HAL/IConsoleManager.h"

namespace EnvironmentRosterSmoke
{
	constexpr float StepSeconds = 0.25f;
	constexpr float WarmupSeconds = 2.f;
	constexpr int32 RosterWave = 2;

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
		FPlatformMisc::RequestExit(false, TEXT("EnvironmentRosterSmoke"));
		return false;
	}

	int32 CountType(const TArray<FEnemySpawnEntry>& Spawns, EEnemyArchetype Type)
	{
		int32 Count = 0;
		for (const FEnemySpawnEntry& Entry : Spawns)
		{
			Count += Entry.EnemyType == Type ? Entry.Count : 0;
		}
		return Count;
	}

	int32 CountAlive(const UWaveSubsystem& Waves, EEnemyArchetype Type)
	{
		int32 Count = 0;
		for (const AEnemyCharacter* Enemy : Waves.GetAliveEnemies())
		{
			Count += Enemy && Enemy->GetArchetype() == Type ? 1 : 0;
		}
		return Count;
	}

	/** Starts the roster wave afresh under Weather at Hour (clock stopped so the conditions hold). */
	void StartUnder(UEnvironmentSubsystem& Environment, UWaveSubsystem& Waves, EWeatherState Weather, float Hour)
	{
		Environment.SetClockScale(0.f);
		Environment.ForceWeather(Weather, true);
		Environment.SetHour(Hour);
		Waves.ClearAllEnemies();
		Waves.StartWave(RosterWave);
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 60.f)
		{
			return World ? Finish(State, false) : false;
		}
		if (State.Time < WarmupSeconds)
		{
			return true;
		}
		UEnvironmentSubsystem* Environment = UEnvironmentSubsystem::Get(World);
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		const ACodexTacticsGameMode* GameMode = World->GetAuthGameMode<ACodexTacticsGameMode>();
		const ULevelConfigAsset* Level = GameMode ? GameMode->GetActiveLevelConfig() : nullptr;
		if (!Environment || !Waves || !Level || !Level->Config.Waves.IsValidIndex(RosterWave - 1))
		{
			Check(State, false, TEXT("environment / wave subsystem / level wave 2 present"));
			return Finish(State, false);
		}
		const TArray<FEnemySpawnEntry>& Written = Level->Config.Waves[RosterWave - 1].Spawns;
		const int32 WrittenMarksmen = CountType(Written, EEnemyArchetype::Marksman);
		const int32 WrittenFrostbitten = CountType(Written, EEnemyArchetype::Frostbitten);
		switch (State.Stage)
		{
		case 0:
		{
			Check(State, Environment->IsInitialised() && !Environment->IsLocked(), TEXT("environment initialised, not locked"));
			Check(State, WrittenMarksmen > 0, FString::Printf(TEXT("written wave %d has %d MARKSMAN"), RosterWave, WrittenMarksmen));
			StartUnder(*Environment, *Waves, EWeatherState::Blizzard, 23.f);
			Check(State, Environment->GetWeather() == EWeatherState::Blizzard && Environment->GetPhase() == ETimeOfDayPhase::Night,
				FString::Printf(TEXT("conditions: %s"), *Environment->Describe()));
			const TArray<FEnemySpawnEntry>& Resolved = Waves->GetCurrentWaveSpawns();
			Check(State, CountType(Resolved, EEnemyArchetype::Marksman) == 0 && CountAlive(*Waves, EEnemyArchetype::Marksman) == 0,
				FString::Printf(TEXT("Blizzard Night: no marksman (resolved %d, alive %d)"), CountType(Resolved, EEnemyArchetype::Marksman),
					CountAlive(*Waves, EEnemyArchetype::Marksman)));
			const int32 Frostbitten = CountAlive(*Waves, EEnemyArchetype::Frostbitten);
			Check(State, Frostbitten == WrittenFrostbitten + 2 * WrittenMarksmen,
				FString::Printf(TEXT("Blizzard Night: %d FROSTBITTEN alive (written %d + 2 x %d marksmen)"), Frostbitten, WrittenFrostbitten, WrittenMarksmen));
			Check(State, Waves->GetAliveEnemyCount() == Waves->GetCurrentWaveSpawnTotal(),
				FString::Printf(TEXT("Blizzard Night: the resolved wave is alive (%d / %d)"), Waves->GetAliveEnemyCount(), Waves->GetCurrentWaveSpawnTotal()));
			// Stage 1 in the same frame: the waves never tick (a worktree without the Biochemical_Monster_2 pack crashes when the tactical
			// sight ghosts a CUTTER of wave 2 - missing skeleton; content gap, not the roster).
		}
			[[fallthrough]];
		case 1:
		{
			State.Stage = 1;
			StartUnder(*Environment, *Waves, EWeatherState::Clear, 12.f);
			Check(State, Environment->GetWeather() == EWeatherState::Clear && Environment->GetPhase() == ETimeOfDayPhase::Midday,
				FString::Printf(TEXT("conditions: %s"), *Environment->Describe()));
			const int32 Marksmen = CountAlive(*Waves, EEnemyArchetype::Marksman);
			Check(State, Marksmen == WrittenMarksmen, FString::Printf(TEXT("Clear Midday: %d / %d marksmen as written"), Marksmen, WrittenMarksmen));
			Waves->ClearAllEnemies();
			return Finish(State, true);
		}
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
		TEXT("CodexTactics.EnvironmentRosterSmoke"),
		TEXT("Dev check: wave 2 under Blizzard at night has no marksmen (2 frostbitten each), under Clear at midday as written; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
