// Dev-only console command for a headless turn-based object relocation check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.TurnBasedPushSmoke
// A barrel stands 3 m in front of the commander, a hound far behind; turn-based combat starts and the commander walks up
// to the barrel. A click on the barrel
// picks it up (target cells shown), Esc-equivalent cancels, the panel «Бочка» picks it up again, a click on the cell
// beyond pushes it: the barrel moves one cell, the commander takes its old cell, 2 AP spent
// (Godot main.gd _start_tactical_relocate / _try_push_adjacent_barrel, turn_based_combat_manager.gd relocate_object).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarrelActor.h"
#include "Tactics/GorkyGridManager.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "TimerManager.h"

namespace TurnBasedPushSmoke
{
	constexpr float StepSeconds = 0.25f;

	struct FState
	{
		int32 Stage = 0;
		float Time = 0.f;
		int32 Failures = 0;
		TWeakObjectPtr<ABarrelActor> Barrel;
		FIntPoint BarrelCell = FIntPoint::ZeroValue;
		FIntPoint Target = FIntPoint::ZeroValue;
		int32 APBefore = 0;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("TurnBasedPushSmoke"));
		return false;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 40.f)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in stage %d"), State.Stage);
			return Finish(State, false);
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		UTurnBasedCombatSubsystem* TurnBased = World->GetSubsystem<UTurnBasedCombatSubsystem>();
		AOperativeCharacter* Leader = World->GetSubsystem<USquadSubsystem>()->GetLeader();
		switch (State.Stage)
		{
		case 0:
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
				It->Destroy();
			}
			const FVector Forward = Leader->GetActorForwardVector().GetSafeNormal2D();
			const FVector Feet = Leader->GetActorLocation() - FVector(0.f, 0.f, Leader->GetSimpleCollisionHalfHeight());
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			State.Barrel = World->SpawnActor<ABarrelActor>(Feet + Forward * 300.f + FVector(0.f, 0.f, 50.f), FRotator::ZeroRotator, Params);
			World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound, Leader->GetActorLocation() - Forward * 600.f);
			Check(State, Flow->RequestEnterTurnBased(true) == EGameFlowResult::Ok && TurnBased->IsActive(), TEXT("turn-based combat started"));
			const FTurnUnitState* BarrelState = TurnBased->GetUnitState(State.Barrel.Get());
			const FTurnUnitState* LeaderState = TurnBased->GetUnitState(TurnBased->GetActiveUnit());
			if (!BarrelState || !LeaderState)
			{
				Check(State, false, TEXT("barrel and commander on the grid"));
				return Finish(State, false);
			}
			State.BarrelCell = BarrelState->GridPos;
			// Walk next to the barrel: the free orthogonal neighbour closest to the commander.
			FIntPoint Best(-1, -1);
			int32 BestDistance = MAX_int32;
			for (const FIntPoint& Dir : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
			{
				const FIntPoint Cell = State.BarrelCell + Dir;
				const FIntPoint Delta = Cell - LeaderState->GridPos;
				const int32 Distance = FMath::Abs(Delta.X) + FMath::Abs(Delta.Y);
				if ((Cell == LeaderState->GridPos || TurnBased->GetGrid()->IsCellWalkable(Cell)) && Distance < BestDistance)
				{
					Best = Cell;
					BestDistance = Distance;
				}
			}
			Check(State, Best.X >= 0 && (Best == LeaderState->GridPos || TurnBased->MoveActiveUnitTo(Best)), TEXT("commander walks up to the barrel"));
			State.Stage = 1;
			return true;
		}
		case 1:
		{
			if (TurnBased->IsUnitMoving())
			{
				return true;
			}
			const FTurnUnitState* LeaderState = TurnBased->GetUnitState(TurnBased->GetActiveUnit());
			const FIntPoint Diff = State.BarrelCell - LeaderState->GridPos;
			Check(State, FMath::Abs(Diff.X) + FMath::Abs(Diff.Y) == 1, TEXT("barrel next to the commander"));

			TurnBased->HandleWorldClick(State.Barrel->GetActorLocation(), State.Barrel.Get(), /*bShift*/ false);
			Check(State, TurnBased->IsRelocating() && TurnBased->GetRelocatingObject() == State.Barrel.Get(), TEXT("click picks the barrel up"));
			Check(State, !TurnBased->GetRelocateCells().Contains(LeaderState->GridPos) && TurnBased->GetRelocateCells().Num() > 0
				&& TurnBased->GetRelocateCells().Num() <= 3, FString::Printf(TEXT("%d target cells, not the commander's"), TurnBased->GetRelocateCells().Num()));
			TurnBased->CancelRelocate();
			Check(State, !TurnBased->IsRelocating(), TEXT("Esc / RMB cancels"));

			Check(State, TurnBased->TryPushAdjacentBarrel() && TurnBased->IsRelocating(), TEXT("panel button picks it up"));
			if (TurnBased->GetRelocateCells().IsEmpty())
			{
				return Finish(State, false);
			}
			State.Target = State.BarrelCell + Diff; // straight on
			if (!TurnBased->GetRelocateCells().Contains(State.Target))
			{
				TArray<FIntPoint> Cells;
				TurnBased->GetRelocateCells().GetKeys(Cells);
				State.Target = Cells[0];
			}
			State.APBefore = LeaderState->AP;
			// Godot _create_relocate_ghost_preview: a hologram over the hovered cell.
			TurnBased->SetRelocationHover(TurnBased->GetGrid()->GridToWorld(State.Target));
			Check(State, TurnBased->IsRelocationGhostShown(), TEXT("hologram of the barrel over the hovered cell"));
			TurnBased->HandleWorldClick(TurnBased->GetGrid()->GridToWorld(State.Target), nullptr, false);
			Check(State, !TurnBased->IsRelocating() && TurnBased->IsUnitMoving(), TEXT("click on a target cell pushes"));
			State.Stage = 2;
			return true;
		}
		case 2:
		{
			if (TurnBased->IsUnitMoving())
			{
				return true;
			}
			const FTurnUnitState* BarrelState = TurnBased->GetUnitState(State.Barrel.Get());
			const FTurnUnitState* LeaderState = TurnBased->GetUnitState(TurnBased->GetActiveUnit());
			UGorkyGridManager* Grid = TurnBased->GetGrid();
			Check(State, BarrelState && BarrelState->GridPos == State.Target && Grid->GetOccupant(State.Target) == State.Barrel.Get(),
				TEXT("barrel on the target cell"));
			Check(State, LeaderState && LeaderState->GridPos == State.BarrelCell, TEXT("commander took the barrel's old cell"));
			Check(State, LeaderState && LeaderState->AP == State.APBefore - 2, TEXT("2 AP spent"));
			const FVector Expected = Grid->GridToWorld(State.Target);
			Check(State, FVector::Dist2D(State.Barrel->GetActorLocation(), Expected) < 5.f, TEXT("barrel actor moved with the grid"));
			Check(State, !TurnBased->IsRelocationGhostShown(), TEXT("hologram gone after the push"));
			return Finish(State, true);
		}
		default:
			return Finish(State, false);
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
		TEXT("CodexTactics.TurnBasedPushSmoke"),
		TEXT("Dev check: turn-based barrel relocation (pick up, cancel, panel button, push); logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
