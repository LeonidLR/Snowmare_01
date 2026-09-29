// Dev-only console command for a headless turn-based deployable check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.TurnBasedDeploySmoke
// Turn-based combat with one hound far away. 1. The commander sets a turret up on a free neighbour cell: 3 AP, the
// turret is on the grid with a unit state, one turret spent. 2. The commander has no mines, a squad mate hands one
// over; the mine goes 3 cells away: the commander walks up, the mine lands on the grid, walk + 2 AP spent
// (Godot turn_based_combat_manager.gd can_place_tactical_deployable / deploy_tactical_object, main.gd
// _handle_tactical_deployable_placement).

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
#include "Tactics/GorkyGridManager.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "TimerManager.h"

namespace TurnBasedDeploySmoke
{
	constexpr float StepSeconds = 0.25f;

	struct FState
	{
		int32 Stage = 0;
		float Time = 0.f;
		int32 Failures = 0;
		FIntPoint MineCell = FIntPoint(-1, -1);
		int32 APBefore = 0;
		int32 MineCost = 0;
		int32 MateMinesBefore = 0;
		TWeakObjectPtr<AOperativeCharacter> Mate;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("TurnBasedDeploySmoke"));
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
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
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
			AOperativeCharacter* Leader = Squad->GetLeader();
			World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound,
				Leader->GetActorLocation() - Leader->GetActorForwardVector() * 700.f);
			Check(State, Flow->RequestEnterTurnBased(true) == EGameFlowResult::Ok && TurnBased->IsActive(), TEXT("turn-based combat started"));
			AOperativeCharacter* Unit = TurnBased->GetActiveUnit();
			UGorkyGridManager* Grid = TurnBased->GetGrid();
			const FTurnUnitState* UnitState = TurnBased->GetUnitState(Unit);

			// 1. Turret on a free neighbour cell.
			Unit->AddDeployable(EDeployableType::Turret, 1 - Unit->GetDeployableCount(EDeployableType::Turret));
			FIntPoint TurretCell(-1, -1);
			FIntPoint Dir(0, 0);
			for (const FIntPoint& D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
			{
				const FIntPoint Near = UnitState->GridPos + D;
				const FIntPoint Far = UnitState->GridPos - D * 3;
				if (Grid->IsCellWalkable(Near) && !Grid->GetOccupant(Near) && Grid->IsCellWalkable(Far) && !Grid->GetOccupant(Far))
				{
					TurretCell = Near;
					Dir = D;
					break;
				}
			}
			if (TurretCell.X < 0)
			{
				Check(State, false, TEXT("free cells around the commander"));
				return Finish(State, false);
			}
			const int32 APStart = UnitState->AP;
			Check(State, TurnBased->HandleDeployPlacement(EDeployableType::Turret, Grid->GridToWorld(TurretCell), 0.f), TEXT("turret placement accepted"));
			AActor* Turret = Grid->GetOccupant(TurretCell);
			Check(State, Turret && Grid->GetOccupantType(TurretCell) == EGorkyOccupantType::Turret && TurnBased->GetUnitState(Turret),
				TEXT("turret on the grid with a unit state"));
			Check(State, UnitState->AP == APStart - 3, FString::Printf(TEXT("turret: 3 AP (%d -> %d)"), APStart, UnitState->AP));
			Check(State, Unit->GetDeployableCount(EDeployableType::Turret) == 0, TEXT("one turret spent"));

			// 2. Mine 3 cells the other way; a squad mate hands it over.
			Unit->AddDeployable(EDeployableType::Mine, -Unit->GetDeployableCount(EDeployableType::Mine));
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member != Unit)
				{
					Member->AddDeployable(EDeployableType::Mine, 1);
					State.Mate = Member;
					State.MateMinesBefore = Member->GetDeployableCount(EDeployableType::Mine);
					break;
				}
			}
			State.MineCell = UnitState->GridPos - Dir * 3;
			const FTurnDeployCheck MineCheck = TurnBased->CanPlaceDeployable(EDeployableType::Mine, State.MineCell, 0.f);
			Check(State, MineCheck.bCanPlace && !MineCheck.Path.IsEmpty(), FString::Printf(TEXT("mine needs a walk: %s, %d AP"), *MineCheck.Reason, MineCheck.APCost));
			State.APBefore = UnitState->AP;
			State.MineCost = MineCheck.APCost;
			Check(State, TurnBased->HandleDeployPlacement(EDeployableType::Mine, Grid->GridToWorld(State.MineCell), 0.f) && TurnBased->IsUnitMoving(),
				TEXT("mine placement: the commander walks up"));
			State.Stage = 1;
			return true;
		}
		case 1:
		{
			if (TurnBased->IsUnitMoving())
			{
				return true;
			}
			UGorkyGridManager* Grid = TurnBased->GetGrid();
			const FTurnUnitState* UnitState = TurnBased->GetUnitState(TurnBased->GetActiveUnit());
			Check(State, Grid->GetOccupantType(State.MineCell) == EGorkyOccupantType::Mine, TEXT("mine on the grid"));
			Check(State, UnitState && UnitState->AP == State.APBefore - State.MineCost,
				FString::Printf(TEXT("walk + 2 AP spent (%d -> %d)"), State.APBefore, UnitState ? UnitState->AP : -1));
			Check(State, UnitState && FMath::Max(FMath::Abs(UnitState->GridPos.X - State.MineCell.X), FMath::Abs(UnitState->GridPos.Y - State.MineCell.Y)) == 1,
				TEXT("commander stands next to the mine"));
			Check(State, State.Mate.IsValid() && State.Mate->GetDeployableCount(EDeployableType::Mine) == State.MateMinesBefore - 1,
				TEXT("the squad mate handed the mine over"));
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
		TEXT("CodexTactics.TurnBasedDeploySmoke"),
		TEXT("Dev check: turn-based turret and mine set-up on the grid (walk, AP, hand-over); logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
