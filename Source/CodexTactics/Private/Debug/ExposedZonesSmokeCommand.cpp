// Dev-only console command for a headless exposed-zones check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.ExposedZonesSmoke
// A wave starts, one brute 6 m ahead, turn-based combat. The squad passes its turn three times without moving:
// after the first pass an uncovered quadrant warns (1 turn), after the second it is in danger (2 turns), after the third
// 1-2 reinforcements of the wave's non-elite pool arrive on its edge (Godot tactical_exposed_zones_manager.gd) and
// join the enemy phase. Only one breach per fight.
// With the argument "shot" (rendering on: UnrealEditor.exe -game -ExecCmds="CodexTactics.ExposedZonesSmoke shot") it saves
// Saved/Screenshots/WindowsEditor/ExposedZones.png at the danger state (red outlines) and exits.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "UnrealClient.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "TimerManager.h"

namespace ExposedZonesSmoke
{
	constexpr float StepSeconds = 0.25f;

	struct FState
	{
		int32 Stage = 0;
		float Time = 0.f;
		int32 Failures = 0;
		int32 Passes = 0;
		int32 EnemiesBefore = 0;
		bool bShot = false;
		float ShotTime = -1.f;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("ExposedZonesSmoke"));
		return false;
	}

	int32 MaxTurns(const FExposedZones& Zones)
	{
		int32 Max = 0;
		for (int32 Quadrant = 0; Quadrant < FExposedZones::NumQuadrants; ++Quadrant)
		{
			Max = FMath::Max(Max, Zones.GetTurns(Quadrant));
		}
		return Max;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 90.f)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in stage %d after %d passes"), State.Stage, State.Passes);
			return Finish(State, false);
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		UTurnBasedCombatSubsystem* TurnBased = World->GetSubsystem<UTurnBasedCombatSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		if (State.Stage == 0)
		{
			if (State.Time < 3.f)
			{
				return true;
			}
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy(); // one known enemy on the grid
			}
			const AOperativeCharacter* Leader = Squad->GetLeader();
			World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound,
				Leader->GetActorLocation() + Leader->GetActorForwardVector() * 600.f + FVector(0.f, 0.f, 20.f));
			const EGameFlowResult Result = Flow->RequestEnterTurnBased(true);
			Check(State, Result == EGameFlowResult::Ok && TurnBased->IsActive(), TEXT("turn-based combat started"));
			Check(State, TurnBased->GetExposedZones().GetPool() == TArray<EEnemyArchetype>{ EEnemyArchetype::FrostHound },
				TEXT("reinforcement pool = the grid's hounds"));
			State.Stage = 1;
			return true;
		}
		if (!TurnBased->IsActive())
		{
			Check(State, false, TEXT("combat still running"));
			return Finish(State, false);
		}
		if (TurnBased->GetPhase() != ETurnPhase::Squad || TurnBased->IsUnitMoving())
		{
			return true;
		}
		const FExposedZones& Zones = TurnBased->GetExposedZones();
		switch (State.Passes)
		{
		case 0:
			break;
		case 1:
			Check(State, MaxTurns(Zones) == 1 && Zones.GetReinforcementsSpawned() == 0, TEXT("pass 1: an uncovered quadrant warns (1 turn)"));
			break;
		case 2:
			if (State.bShot)
			{
				if (State.ShotTime < 0.f)
				{
					State.ShotTime = State.Time;
					FScreenshotRequest::RequestScreenshot(FPaths::ScreenShotDir() / TEXT("ExposedZones.png"), /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
				}
				return State.Time - State.ShotTime < 1.5f || Finish(State, true);
			}
			Check(State, MaxTurns(Zones) == 2 && Zones.GetReinforcementsSpawned() == 0, TEXT("pass 2: danger (2 turns), no breach yet"));
			break;
		case 3:
		{
			const int32 Arrived = TurnBased->GetEnemyCount() - State.EnemiesBefore;
			Check(State, Zones.GetReinforcementsSpawned() == 1 && Arrived >= 1 && Arrived <= 2,
				FString::Printf(TEXT("pass 3: breach, %d reinforcement(s) on the grid"), Arrived));
			int32 Hounds = 0;
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				Hounds += It->GetArchetype() == EEnemyArchetype::FrostHound && TurnBased->GetUnitState(*It) ? 1 : 0;
			}
			Check(State, Hounds == TurnBased->GetEnemyCount(), TEXT("reinforcements are hounds (non-elite pool)"));
			State.EnemiesBefore = TurnBased->GetEnemyCount();
			break;
		}
		default:
			Check(State, Zones.GetReinforcementsSpawned() == 1 && TurnBased->GetEnemyCount() <= State.EnemiesBefore,
				TEXT("pass 4: no second breach in the fight"));
			return Finish(State, true);
		}
		State.EnemiesBefore = State.Passes < 3 ? TurnBased->GetEnemyCount() : State.EnemiesBefore;
		++State.Passes;
		TurnBased->PassSquadTurn();
		return true;
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		{
			FTimerHandle PlaceHandle;
			TWeakObjectPtr<UWorld> PlaceWorld(World);
			World->GetTimerManager().SetTimer(PlaceHandle, FTimerDelegate::CreateLambda([PlaceWorld]()
			{
				SmokeUtils::PlaceSquadAtTestStart(PlaceWorld.Get());
			}), 0.5f, false);
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		State->bShot = Args.Contains(TEXT("shot"));
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.ExposedZonesSmoke"),
		TEXT("Dev check: exposed quadrants warn, then let 1-2 reinforcements in once; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
