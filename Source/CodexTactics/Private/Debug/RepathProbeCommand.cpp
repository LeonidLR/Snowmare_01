// Dev-only measurement on L_MovementTest (audit 2026-10-11 #1, enemy re-path throttle):
//   UnrealEditor-Cmd ... -ExecCmds="CodexTactics.RepathProbe"                       (throttle on, the default)
//   UnrealEditor-Cmd ... -ExecCmds="Codex.AI.Repath 0,CodexTactics.RepathProbe"     (every tick: the old behaviour)
// Sudden wave start (SmokeUtils::StartWaveFight), the squad holds its fire and cannot die; the leader walks back and forth
// (the followers follow), so the enemies chase moving targets. For 20 s it counts the enemy controllers' MoveTo calls
// (each one a synchronous pathfind) against the per-tick asks and the living enemy-seconds, and logs
// «Probe repath: … req/enemy/s». PASS when enemies fought and, with the throttle on, fewer moves reached the engine than
// were asked for.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Debug/SmokeUtils.h"
#include "Characters/EnemyAIController.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/HealthComponent.h"
#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"

namespace RepathProbe
{
	constexpr float StepSeconds = 0.1f;
	constexpr float WarmupSeconds = 3.f;
	constexpr float ArrivalSeconds = 4.f;
	constexpr float MeasureSeconds = 20.f;
	constexpr float LeaderLegSeconds = 4.f;
	constexpr float LeaderLegCm = 600.f;

	struct FState
	{
		float Time = 0.f;
		int32 Stage = 0;
		double LastWorldTime = 0.0;
		double EnemySeconds = 0.0;
		int32 MaxAlive = 0;
		/** Sum / count of speed / max walk speed of enemies running in from > 6 m (no melee, no stagger). */
		double SpeedRatioSum = 0.0;
		int32 SpeedSamples = 0;
		FVector LeaderHome = FVector::ZeroVector;
		int32 Leg = 0;
		float LegTime = 0.f;
	};

	int32 CountLiveEnemies(UWorld* World, const TArray<AOperativeCharacter*>& Squad, FState& State)
	{
		int32 Alive = 0;
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			if (It->IsDying() || !It->GetHealthComponent() || !It->GetHealthComponent()->IsAlive())
			{
				continue;
			}
			++Alive;
			float Nearest = TNumericLimits<float>::Max();
			for (const AOperativeCharacter* Member : Squad)
			{
				Nearest = FMath::Min(Nearest, static_cast<float>(FVector::Dist2D(Member->GetActorLocation(), It->GetActorLocation())));
			}
			const float MaxSpeed = It->GetCharacterMovement() ? It->GetCharacterMovement()->MaxWalkSpeed : 0.f;
			if (Nearest > 600.f && MaxSpeed > 1.f)
			{
				State.SpeedRatioSum += It->GetVelocity().Size2D() / MaxSpeed;
				++State.SpeedSamples;
			}
		}
		return Alive;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		if (State.Stage == 0)
		{
			if (State.Time < WarmupSeconds)
			{
				return true;
			}
			SmokeUtils::StartWaveFight(World->GetSubsystem<UGameFlowSubsystem>()); // sudden horde start, its free pause released
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->bTacticalCeaseFire = true; // nobody dies: the enemies keep chasing for the whole window
				Member->HealthComponent->SetMaxHealth(100000.f);
			}
			State.Stage = 1;
			State.Time = 0.f;
			return true;
		}
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->ColdLevel = 0.f; // no freezing / panic while we watch
		}
		if (State.Stage == 1)
		{
			if (State.Time < ArrivalSeconds)
			{
				return true; // the wave spawns and starts its run
			}
			AEnemyAIController::ResetTotalMoveCounters();
			State.LastWorldTime = World->GetTimeSeconds();
			State.LeaderHome = Squad->GetLeader() ? Squad->GetLeader()->GetActorLocation() : FVector::ZeroVector;
			State.Stage = 2;
			State.Time = 0.f;
			return true;
		}
		// Measuring: enemy-seconds by world time (the ticker step is wall time).
		const double Now = World->GetTimeSeconds();
		const int32 Alive = CountLiveEnemies(World, Squad->GetMembers(), State);
		State.EnemySeconds += Alive * (Now - State.LastWorldTime);
		State.LastWorldTime = Now;
		State.MaxAlive = FMath::Max(State.MaxAlive, Alive);
		// The leader walks back and forth along X: the chased targets move.
		State.LegTime += StepSeconds;
		if (AOperativeCharacter* Leader = Squad->GetLeader(); Leader && (State.Leg == 0 || State.LegTime >= LeaderLegSeconds))
		{
			State.LegTime = 0.f;
			Leader->OrderMoveTo(State.LeaderHome + FVector(State.Leg % 2 == 0 ? LeaderLegCm : -LeaderLegCm, 0.f, 0.f), false);
			++State.Leg;
		}
		if (State.Time < MeasureSeconds)
		{
			return true;
		}
		const int64 Requests = AEnemyAIController::GetTotalMoveRequestCount();
		const int64 Asks = AEnemyAIController::GetTotalMoveAskCount();
		const int64 Pathfinds = AEnemyAIController::GetTotalPathfindCount();
		const FRepathSettings Settings = AEnemyAIController::GetRepathSettings();
		const double PerEnemy = State.EnemySeconds > 0.0 ? Requests / State.EnemySeconds : 0.0;
		const double AsksPerEnemy = State.EnemySeconds > 0.0 ? Asks / State.EnemySeconds : 0.0;
		const double PathfindsPerEnemy = State.EnemySeconds > 0.0 ? Pathfinds / State.EnemySeconds : 0.0;
		UE_LOG(LogCodexTactics, Display,
			TEXT("Probe repath: throttle %s (interval %.2f s, min goal delta %.0f cm): %lld MoveTo requests (%lld path queries), %lld per-tick asks, %.1f enemy-seconds (max %d alive) -> %.2f req/enemy/s, %.2f pathfinds/enemy/s, %.2f asks/enemy/s; running in from > 6 m at %.0f %% of max speed"),
			Settings.bEnabled ? TEXT("ON") : TEXT("OFF"), Settings.IntervalSeconds, Settings.MinGoalDeltaCm, Requests, Pathfinds, Asks, State.EnemySeconds,
			State.MaxAlive, PerEnemy, PathfindsPerEnemy, AsksPerEnemy, State.SpeedSamples > 0 ? 100.0 * State.SpeedRatioSum / State.SpeedSamples : 0.0);
		const bool bPass = State.EnemySeconds > 1.0 && Asks > 0 && (!Settings.bEnabled || Pathfinds < Asks);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bPass ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("RepathProbe"));
		return false;
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
		TEXT("CodexTactics.RepathProbe"),
		TEXT("Dev measurement: enemy MoveTo requests per enemy per second in a 20 s wave chase (Codex.AI.Repath 0 / 1)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
