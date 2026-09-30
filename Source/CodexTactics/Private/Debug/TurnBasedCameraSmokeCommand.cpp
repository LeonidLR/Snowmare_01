// Dev-only console command for a headless check of the turn-based camera choreography on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.TurnBasedCameraSmoke
// Godot main.gd _enter_turn_based_combat (14 m), _on_gorky17_turn_changed (operative 16 m / enemy 11.5 m),
// _on_gorky17_enemy_movement_started / _finished (overview 17 m), _perform_dramatic_tactical_attack (frame both,
// shot at 0.4 s, hit at 0.75 s, glide back 1.1 s; orders wait meanwhile). Cinematics are forced on (headless run).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/TacticalCameraPawn.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Tactics/GorkyGridManager.h"
#include "Tactics/GorkyLineOfSight.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "Tactics/TurnBasedRules.h"
#include "TimerManager.h"

namespace TurnBasedCameraSmoke
{
	constexpr float StepSeconds = 0.05f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		TWeakObjectPtr<AEnemyCharacter> Enemy;
		bool bEnemyFocused = false;
		float EnemyHealth = 0.f;
		bool bHitEarly = false;
		bool bMoveRefused = false;
		FIntPoint Target = FIntPoint::ZeroValue;
		bool bPrepared = false;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("TurnBasedCameraSmoke"));
		return false;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 70.f)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in stage %d"), State.Stage);
			return World ? Finish(State, false) : false;
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		UTurnBasedCombatSubsystem* TurnBased = World->GetSubsystem<UTurnBasedCombatSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		const APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		ATacticalCameraPawn* Camera = PC ? Cast<ATacticalCameraPawn>(PC->GetPawn()) : nullptr;
		if (!Camera)
		{
			return State.Time < 5.f;
		}
		auto Next = [&State]() { ++State.Stage; State.StageTime = 0.f; };
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			if (!State.bPrepared)
			{
				// The wave first (the camera switches to its combat zoom on its next tick), the fight a moment later.
				Flow->TriggerCombatZone();
				Flow->FinishCutscene();
				Flow->FinishPreparation();
				State.bPrepared = true;
				State.StageTime = 2.5f;
				return true;
			}
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy();
			}
			AOperativeCharacter* Leader = Squad->GetLeader();
			State.Enemy = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound,
				Leader->GetActorLocation() + Leader->GetActorForwardVector() * 700.f + FVector(0.f, 0.f, 20.f));
			if (!State.Enemy.IsValid())
			{
				return Finish(State, false);
			}
			State.Enemy->GetHealthComponent()->SetMaxHealth(2000.f, true); // survives the shot
			TurnBased->bForceCinematicsForTesting = true;
			Check(State, Flow->RequestEnterTurnBased(true) == EGameFlowResult::Ok && TurnBased->IsActive() && TurnBased->AreCinematicsActive(),
				TEXT("turn-based combat with cinematics"));
			Check(State, Camera->IsSmoothFocusing(), TEXT("the camera glides to the first operative"));
			Next();
			return true;
		}
		case 1:
			if (State.StageTime < 1.f)
			{
				return true;
			}
			Check(State, !Camera->IsSmoothFocusing() && FMath::IsNearlyEqual(Camera->GetCurrentDistance(), 1600.f, 5.f),
				FString::Printf(TEXT("squad turn framing: 16 m (%.0f cm)"), Camera->GetCurrentDistance()));
			TurnBased->PassSquadTurn();
			Next();
			return true;
		case 2: // Enemy phase: the camera goes to the enemy, then back to the squad overview.
			State.bEnemyFocused |= Camera->GetFollowTarget() == State.Enemy.Get();
			if (TurnBased->GetRound() < 2 || TurnBased->GetPhase() != ETurnPhase::Squad || TurnBased->IsBusy())
			{
				return true;
			}
			Check(State, State.bEnemyFocused, TEXT("the camera followed the enemy on its turn"));
			Check(State, Camera->GetFollowTarget() == TurnBased->GetActiveUnit(), TEXT("back on the active operative for the squad turn"));
			Next();
			return true;
		case 3: // Into a fire lane (no LoS / lane shot would skip the cinematic).
		{
			if (TurnBased->IsBusy() || State.StageTime < 1.f)
			{
				return true;
			}
			const FTurnUnitState* EnemyState = TurnBased->GetUnitState(State.Enemy.Get());
			const AOperativeCharacter* Unit = TurnBased->GetActiveUnit();
			const FTurnUnitState* UnitState = TurnBased->GetUnitState(Unit);
			UGorkyGridManager* Grid = TurnBased->GetGrid();
			if (!EnemyState || !UnitState || !Grid)
			{
				return Finish(State, false);
			}
			State.Target = EnemyState->GridPos;
			if (!(TurnBasedRules::IsTargetInPattern(Unit->CurrentWeapon, State.Target - UnitState->GridPos)
				&& GorkyLineOfSight::HasLineOfSight(UnitState->GridPos, State.Target, *Grid)))
			{
				for (const TPair<FIntPoint, int32>& Entry : Grid->GetReachableCells(UnitState->GridPos, UnitState->AP - 3))
				{
					if (Entry.Key != UnitState->GridPos && Grid->IsCellWalkable(Entry.Key)
						&& TurnBasedRules::IsTargetInPattern(Unit->CurrentWeapon, State.Target - Entry.Key)
						&& GorkyLineOfSight::HasLineOfSight(Entry.Key, State.Target, *Grid))
					{
						TurnBased->MoveActiveUnitTo(Entry.Key);
						break;
					}
				}
			}
			Next();
			return true;
		}
		case 4:
			if (TurnBased->IsBusy())
			{
				return true;
			}
			TurnBased->bGuaranteeAllHits = true;
			State.EnemyHealth = State.Enemy->GetHealthComponent()->GetCurrentHealth();
			TurnBased->AttackCellCinematic(State.Target);
			Check(State, TurnBased->IsDramaticShotActive() && Camera->IsSmoothFocusing()
				&& Camera->GetTargetDistance() >= 1000.f && Camera->GetTargetDistance() <= 1900.f,
				FString::Printf(TEXT("dramatic framing of shooter and target (%.0f cm)"), Camera->GetTargetDistance()));
			Next();
			return true;
		case 5:
			if (State.StageTime < 0.6f)
			{
				State.bHitEarly |= State.Enemy->GetHealthComponent()->GetCurrentHealth() < State.EnemyHealth;
				if (State.StageTime > 0.2f && !State.bMoveRefused)
				{
					State.bMoveRefused = !TurnBased->MoveActiveUnitTo(TurnBased->GetUnitState(TurnBased->GetActiveUnit())->GridPos + FIntPoint(1, 0));
				}
				return true;
			}
			if (State.StageTime < 1.2f)
			{
				return true;
			}
			Check(State, !State.bHitEarly && State.Enemy->GetHealthComponent()->GetCurrentHealth() < State.EnemyHealth,
				TEXT("the round lands after the shot (0.75 s), not at the click"));
			Check(State, State.bMoveRefused, TEXT("orders wait while the shot plays"));
			Next();
			return true;
		case 6:
			if (TurnBased->IsDramaticShotActive() && State.StageTime < 4.f)
			{
				return true;
			}
			Check(State, !TurnBased->IsDramaticShotActive() && Camera->GetFollowTarget() == TurnBased->GetActiveUnit()
				&& FMath::IsNearlyEqual(Camera->GetCurrentDistance(), Camera->Config.DistanceCombat, 5.f),
				FString::Printf(TEXT("glide back to the shooter at the combat distance (%.0f cm)"), Camera->GetCurrentDistance()));
			Flow->ExitTurnBased(); // the subsystem ends the fight on the flow change
			Next();
			return true;
		default:
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			Check(State, !TurnBased->IsActive() && FMath::IsNearlyEqual(Camera->GetTargetDistance(), Camera->Config.DistanceCombat, 5.f),
				FString::Printf(TEXT("leaving restores the pre-combat zoom (%.0f cm)"), Camera->GetTargetDistance()));
			return Finish(State, true);
		}
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
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.TurnBasedCameraSmoke"),
		TEXT("Dev check: turn-based camera choreography (focus, dramatic shot, overview, zoom); logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
