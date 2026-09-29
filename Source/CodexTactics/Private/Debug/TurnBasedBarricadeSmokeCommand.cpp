// Dev-only console command for a headless turn-based barricade relocation check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.TurnBasedBarricadeSmoke
// A barricade stands 4 m in front of the commander, a hound far behind; turn-based combat starts, the barricade covers
// the cells of Godot's five footprint samples, the commander walks up to it. A click (no Shift) picks it up, two
// 45° steps turn it by 90° (target cells recomputed), a click on a target cell moves and turns it for 2 AP
// (Godot main.gd barricade relocation, turn_based_combat_manager.gd get_barricade_cells_at / can_place_barricade_at /
// relocate_barricade / _register_barricade_cells).

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
#include "Interactables/BarricadeActor.h"
#include "Tactics/GorkyGridManager.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "TimerManager.h"

namespace TurnBasedBarricadeSmoke
{
	constexpr float StepSeconds = 0.25f;

	struct FState
	{
		int32 Stage = 0;
		float Time = 0.f;
		int32 Failures = 0;
		TWeakObjectPtr<ABarricadeActor> Barricade;
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
		FPlatformMisc::RequestExit(false, TEXT("TurnBasedBarricadeSmoke"));
		return false;
	}

	TArray<FIntPoint> CellsOf(const UGorkyGridManager* Grid, const AActor* Actor)
	{
		TArray<FIntPoint> Cells;
		for (int32 X = 0; X < Grid->GridSize.X; ++X)
		{
			for (int32 Y = 0; Y < Grid->GridSize.Y; ++Y)
			{
				if (Grid->GetOccupant(FIntPoint(X, Y)) == Actor)
				{
					Cells.Add(FIntPoint(X, Y));
				}
			}
		}
		return Cells;
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
		UGorkyGridManager* Grid = TurnBased->GetGrid();
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
			State.Barricade = World->SpawnActor<ABarricadeActor>(Feet + Forward * 400.f + FVector(0.f, 0.f, 50.f), Forward.Rotation() + FRotator(0.f, 90.f, 0.f), Params);
			World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound, Leader->GetActorLocation() - Forward * 600.f);
			Check(State, Flow->RequestEnterTurnBased(true) == EGameFlowResult::Ok && TurnBased->IsActive(), TEXT("turn-based combat started"));
			Grid = TurnBased->GetGrid();
			const TArray<FIntPoint> Footprint = CellsOf(Grid, State.Barricade.Get());
			Check(State, Footprint.Num() >= 2 && Footprint.Num() <= 4, FString::Printf(TEXT("barricade covers %d cells (Godot samples)"), Footprint.Num()));
			const FTurnUnitState* LeaderState = TurnBased->GetUnitState(TurnBased->GetActiveUnit());
			// Walk to the free cell next to the footprint that is closest to the commander.
			FIntPoint Best(-1, -1);
			int32 BestDistance = MAX_int32;
			for (const FIntPoint& Cell : Footprint)
			{
				for (int32 DX = -1; DX <= 1; ++DX)
				{
					for (int32 DY = -1; DY <= 1; ++DY)
					{
						const FIntPoint Near = Cell + FIntPoint(DX, DY);
						const FIntPoint Delta = Near - LeaderState->GridPos;
						const int32 Distance = FMath::Max(FMath::Abs(Delta.X), FMath::Abs(Delta.Y));
						if ((Near == LeaderState->GridPos || Grid->IsCellWalkable(Near)) && Distance < BestDistance)
						{
							Best = Near;
							BestDistance = Distance;
						}
					}
				}
			}
			Check(State, Best.X >= 0 && (Best == LeaderState->GridPos || TurnBased->MoveActiveUnitTo(Best)), TEXT("commander walks up to the barricade"));
			State.Stage = 1;
			return true;
		}
		case 1:
		{
			if (TurnBased->IsUnitMoving())
			{
				return true;
			}
			ABarricadeActor* Barricade = State.Barricade.Get();
			TurnBased->HandleWorldClick(Barricade->GetActorLocation(), Barricade, /*bShift*/ false);
			Check(State, TurnBased->IsRelocatingBarricade(), TEXT("click picks the barricade up"));
			const int32 Before = TurnBased->GetRelocateCells().Num();
			const float StartYaw = TurnBased->GetRelocateYaw();
			TurnBased->RotateRelocation(+1);
			TurnBased->RotateRelocation(+1);
			Check(State, FMath::IsNearlyEqual(FRotator::NormalizeAxis(TurnBased->GetRelocateYaw() - StartYaw), 90.f, 0.5f),
				FString::Printf(TEXT("two 45° steps: %.0f° -> %.0f°"), StartYaw, TurnBased->GetRelocateYaw()));
			const TMap<FIntPoint, int32>& Cells = TurnBased->GetRelocateCells();
			Check(State, Cells.Num() > 0, FString::Printf(TEXT("target cells at the new angle: %d (before %d)"), Cells.Num(), Before));
			if (Cells.IsEmpty())
			{
				return Finish(State, false);
			}
			// Prefer a target that really moves the barricade.
			const TArray<FIntPoint> Old = CellsOf(Grid, Barricade);
			FIntPoint Target(-1, -1);
			for (const TPair<FIntPoint, int32>& Entry : Cells)
			{
				if (!Old.Contains(Entry.Key))
				{
					Target = Entry.Key;
					break;
				}
			}
			if (Target.X < 0)
			{
				TArray<FIntPoint> Keys;
				Cells.GetKeys(Keys);
				Target = Keys[0];
			}
			const float Yaw = TurnBased->GetRelocateYaw();
			const TArray<FIntPoint> Expected = TurnBased->GetBarricadeCellsAt(Barricade, Target, Yaw);
			State.APBefore = TurnBased->GetUnitState(TurnBased->GetActiveUnit())->AP;
			TurnBased->HandleWorldClick(Grid->GridToWorld(Target), nullptr, false);
			Check(State, !TurnBased->IsRelocating(), TEXT("click on a target cell places it"));
			TArray<FIntPoint> Now = CellsOf(Grid, Barricade);
			Now.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X != B.X ? A.X < B.X : A.Y < B.Y; });
			TArray<FIntPoint> Want = Expected;
			Want.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X != B.X ? A.X < B.X : A.Y < B.Y; });
			Check(State, Now == Want, FString::Printf(TEXT("grid footprint = the new samples (%d cells)"), Now.Num()));
			Check(State, FMath::IsNearlyZero(FRotator::NormalizeAxis(Barricade->GetActorRotation().Yaw - Yaw), 0.5f), TEXT("barricade turned"));
			Check(State, FVector::Dist2D(Barricade->GetActorLocation(), Grid->GridToWorld(Target)) < 5.f, TEXT("barricade on the target cell"));
			Check(State, TurnBased->GetUnitState(TurnBased->GetActiveUnit())->AP == State.APBefore - 2, TEXT("2 AP spent"));
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
		TEXT("CodexTactics.TurnBasedBarricadeSmoke"),
		TEXT("Dev check: turn-based barricade relocation with rotation; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
