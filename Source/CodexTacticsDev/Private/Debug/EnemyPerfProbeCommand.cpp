// Dev-only measurement on L_MovementTest (audit 2026-10-11 #2 / P2-P3, enemy registry):
//   powershell -File Scripts/smoke_parallel.ps1 -Commands EnemyPerfProbe -Jobs 1      (fixed 60 fps step, like the smokes)
//   UnrealEditor-Cmd ... -ExecCmds="CodexTactics.EnemyPerfProbe [seconds]"
// Sudden wave start (SmokeUtils::StartWaveFight, the level's horde wave: 50+ enemies); nobody dies (the squad and every
// enemy get a huge health pool), the squad keeps its normal fire posture, so every operative runs its real-time target
// search over the whole wave each frame and the enemies chase / strike. After a 4 s run-in it measures for [seconds]
// (default 15) of game time: the game-thread time of each world tick (FWorldDelegates::OnWorldTickStart ->
// OnWorldTickEnd: actors, components, tickable subsystems, timers, navigation) and of the whole frame (tick start to
// tick start), and logs «Probe perf: ...» with the mean / p50 / p95 and the enemies alive. Not a smoke: the numbers are
// for before / after comparisons on the same machine (run it alone: parallel engines skew the wall clock).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Debug/SmokeUtils.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/HealthComponent.h"
#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/PlatformTime.h"

namespace EnemyPerfProbe
{
	constexpr float WarmupSeconds = 3.f;
	constexpr float ArrivalSeconds = 4.f;
	constexpr float UnkillableHealth = 1000000.f;

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		int32 Stage = 0;
		double StageStart = 0.0;
		float MeasureSeconds = 15.f;
		double TickStartedAt = 0.0;
		double LastTickStart = 0.0;
		TArray<double> TickMs;
		TArray<double> FrameMs;
		int64 AliveSum = 0;
		int32 AliveMin = MAX_int32;
		int32 AliveMax = 0;
		int32 Operatives = 0;
		FDelegateHandle StartHandle;
		FDelegateHandle EndHandle;
	};

	double Percentile(TArray<double> Values, double Fraction)
	{
		if (Values.IsEmpty())
		{
			return 0.0;
		}
		Values.Sort();
		return Values[FMath::Clamp(FMath::FloorToInt32(Fraction * (Values.Num() - 1)), 0, Values.Num() - 1)];
	}

	double Mean(const TArray<double>& Values)
	{
		double Sum = 0.0;
		for (const double Value : Values)
		{
			Sum += Value;
		}
		return Values.IsEmpty() ? 0.0 : Sum / Values.Num();
	}

	/** Every enemy unkillable (new wave members included); returns the living ones. */
	int32 KeepEnemiesAlive(UWorld* World)
	{
		int32 Alive = 0;
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			UHealthComponent* Health = It->GetHealthComponent();
			if (!Health || It->IsDying())
			{
				continue;
			}
			if (Health->GetMaxHealth() < UnkillableHealth)
			{
				Health->SetMaxHealth(UnkillableHealth);
			}
			++Alive;
		}
		return Alive;
	}

	void Finish(const TSharedRef<FState>& State)
	{
		FWorldDelegates::OnWorldTickStart.Remove(State->StartHandle);
		FWorldDelegates::OnWorldTickEnd.Remove(State->EndHandle);
		const int32 Frames = State->TickMs.Num();
		UE_LOG(LogCodexTactics, Display,
			TEXT("Probe perf: %d frames over %.1f s game time, %d operatives, enemies alive avg %.1f (min %d, max %d); world tick mean %.3f ms, p50 %.3f, p95 %.3f, max %.3f; frame mean %.3f ms, p50 %.3f, p95 %.3f"),
			Frames, State->MeasureSeconds, State->Operatives, Frames > 0 ? static_cast<double>(State->AliveSum) / Frames : 0.0,
			State->AliveMin == MAX_int32 ? 0 : State->AliveMin, State->AliveMax, Mean(State->TickMs), Percentile(State->TickMs, 0.5),
			Percentile(State->TickMs, 0.95), Percentile(State->TickMs, 1.0), Mean(State->FrameMs), Percentile(State->FrameMs, 0.5),
			Percentile(State->FrameMs, 0.95));
		const bool bPass = Frames > 30 && State->AliveMin != MAX_int32 && State->AliveMin >= 1;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bPass ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("EnemyPerfProbe"));
	}

	bool Step(const TSharedRef<FState>& State)
	{
		UWorld* World = State->World.Get();
		if (!World)
		{
			return false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		const double Now = World->GetTimeSeconds();
		if (Squad)
		{
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->ColdLevel = 0.f; // no freezing / panic while we watch
				if (Member->HealthComponent && Member->HealthComponent->GetMaxHealth() < UnkillableHealth)
				{
					Member->HealthComponent->SetMaxHealth(UnkillableHealth);
				}
			}
		}
		switch (State->Stage)
		{
		case 0:
			if (Now < WarmupSeconds)
			{
				return true;
			}
			SmokeUtils::StartWaveFight(World->GetSubsystem<UGameFlowSubsystem>()); // the level's wave at once, free pause released
			State->Stage = 1;
			State->StageStart = Now;
			return true;
		case 1:
			KeepEnemiesAlive(World);
			if (Now - State->StageStart < ArrivalSeconds)
			{
				return true;
			}
			State->Operatives = Squad ? Squad->GetMembers().Num() : 0;
			State->StageStart = Now;
			State->Stage = 2;
			State->StartHandle = FWorldDelegates::OnWorldTickStart.AddLambda([Weak = State->World, Raw = &State.Get()](UWorld* TickWorld, ELevelTick, float)
			{
				if (TickWorld == Weak.Get())
				{
					const double Seconds = FPlatformTime::Seconds();
					if (Raw->LastTickStart > 0.0)
					{
						Raw->FrameMs.Add((Seconds - Raw->LastTickStart) * 1000.0);
					}
					Raw->LastTickStart = Seconds;
					Raw->TickStartedAt = Seconds;
				}
			});
			State->EndHandle = FWorldDelegates::OnWorldTickEnd.AddLambda([Weak = State->World, Raw = &State.Get()](UWorld* TickWorld, ELevelTick, float)
			{
				if (TickWorld == Weak.Get() && Raw->TickStartedAt > 0.0)
				{
					Raw->TickMs.Add((FPlatformTime::Seconds() - Raw->TickStartedAt) * 1000.0);
					Raw->TickStartedAt = 0.0;
				}
			});
			return true;
		default:
		{
			const int32 Alive = KeepEnemiesAlive(World);
			State->AliveSum += Alive;
			State->AliveMin = FMath::Min(State->AliveMin, Alive);
			State->AliveMax = FMath::Max(State->AliveMax, Alive);
			if (Now - State->StageStart < State->MeasureSeconds)
			{
				return true;
			}
			Finish(State);
			return false;
		}
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		TSharedRef<FState> State = MakeShared<FState>();
		State->World = World;
		if (Args.Num() > 0)
		{
			State->MeasureSeconds = FMath::Clamp(FCString::Atof(*Args[0]), 2.f, 120.f);
		}
		// Every frame (the stages run on game time: the fixed step may run faster than the wall clock).
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float)
		{
			return Step(State);
		}), 0.f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.EnemyPerfProbe"),
		TEXT("Dev measurement: game-thread ms per world tick / frame in an unkillable wave fight (50+ enemies) [seconds, default 15]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
