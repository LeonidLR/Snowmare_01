// Dev-only console command for a headless check of the squad number keys in the turn-based fight on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.TurnSelectSmoke
// Godot main.gd _select_squad_member_by_index / select_squad_unit: with Susanin recruited (member 4) the turn-based
// fight starts; the keys 2, 3, 4, 1 (real key events through Enhanced Input) make the engineer, the medic, Susanin and
// the commander the active operative. The hovered cell gets the cursor frame: red over the enemy, yellow elsewhere.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/RecruitSubsystem.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "Tactics/GorkyGridManager.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "Tactics/TurnGridOverlayActor.h"
#include "TimerManager.h"

namespace TurnSelectSmoke
{
	constexpr float StepSeconds = 0.25f;

	struct FState
	{
		int32 Step = 0;
		int32 Failures = 0;
		TWeakObjectPtr<AOperativeCharacter> Susanin;
		TWeakObjectPtr<AEnemyCharacter> Hound;
	};

	struct FKeyCase
	{
		FKey Key;
		int32 SquadIndex;
		const TCHAR* Name;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("TurnSelectSmoke"));
		return false;
	}

	void PressKey(APlayerController* PC, const FKey& Key, EInputEvent Event)
	{
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, Event, Event == IE_Pressed ? 1.f : 0.f));
	}

	bool Step(UWorld* World, FState& State)
	{
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		UTurnBasedCombatSubsystem* TurnBased = World->GetSubsystem<UTurnBasedCombatSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Flow || !TurnBased || !Squad || !PC || !Leader)
		{
			Check(State, false, TEXT("subsystems / controller / leader"));
			return Finish(State, false);
		}
		static const FKeyCase Cases[] = {
			{ EKeys::Two, 1, TEXT("2 -> engineer") }, { EKeys::Three, 2, TEXT("3 -> medic") },
			{ EKeys::Four, 3, TEXT("4 -> Susanin") }, { EKeys::One, 0, TEXT("1 -> commander") } };
		const int32 Step = State.Step++;
		if (Step == 0)
		{
			// Susanin recruited next to the squad, then a wave with one enemy in front.
			URecruitSubsystem* Recruits = World->GetSubsystem<URecruitSubsystem>();
			AOperativeCharacter* Susanin = Recruits ? Recruits->GetOrSpawnSusanin() : nullptr;
			if (!Susanin)
			{
				Check(State, false, TEXT("Susanin spawned"));
				return Finish(State, false);
			}
			Susanin->TeleportTo(Leader->GetActorLocation() - Leader->GetActorRightVector() * 300.f, Leader->GetActorRotation(), false, true);
			Recruits->RecruitIntoSquad(true);
			State.Susanin = Susanin;
			Check(State, Squad->GetMembers().Contains(Susanin) && Susanin->SquadIndex == 3, TEXT("Susanin recruited as member 4"));
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			AEnemyCharacter* Kept = nullptr;
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				if (!Kept && !It->IsDying())
				{
					Kept = *It;
					Kept->SetActorLocation(Leader->GetActorLocation() - Leader->GetActorForwardVector() * 4000.f);
					Kept->CustomTimeDilation = 0.f;
				}
				else
				{
					It->Destroy();
				}
			}
			State.Hound = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound,
				Leader->GetActorLocation() + Leader->GetActorForwardVector() * 700.f + FVector(0.f, 0.f, 20.f));
			if (State.Hound.IsValid())
			{
				State.Hound->GetHealthComponent()->SetMaxHealth(100000.f); // the squad shoots it in real time until the fight starts
			}
			return true;
		}
		if (Step == 4)
		{
			Check(State, Flow->RequestEnterTurnBased(true) == EGameFlowResult::Ok && TurnBased->IsActive(), TEXT("turn-based fight started"));
			Check(State, State.Susanin.IsValid() && TurnBased->GetUnitState(State.Susanin.Get()) != nullptr,
				FString::Printf(TEXT("Susanin is in the fight (%d operatives, %d enemies)"), TurnBased->GetSquadCount(), TurnBased->GetEnemyCount()));
			// Cursor frame (Godot set_hovered_cell): over the enemy red, over an empty cell next to the leader yellow.
			const ATurnGridOverlayActor* Overlay = TurnBased->GetOverlay();
			// An enemy that is really on the grid (the spawned hound may not have joined the fight).
			const UGorkyGridManager* CursorGrid = TurnBased->GetGrid();
			AActor* GridEnemy = nullptr;
			for (int32 X = 0; CursorGrid && !GridEnemy && X < CursorGrid->GridSize.X; ++X)
			{
				for (int32 Y = 0; !GridEnemy && Y < CursorGrid->GridSize.Y; ++Y)
				{
					if (CursorGrid->GetOccupantType(FIntPoint(X, Y)) == EGorkyOccupantType::Enemy)
					{
						GridEnemy = CursorGrid->GetOccupant(FIntPoint(X, Y));
					}
				}
			}
			if (GridEnemy && Overlay && CursorGrid)
			{
				TurnBased->SetHoveredPoint(GridEnemy->GetActorLocation() + FVector(40.f, 40.f, 50.f), GridEnemy); // a hit on its body
				const bool bEnemyFrame = Overlay->IsCursorOnEnemy() && Overlay->GetCellCount(ETurnOverlayLayer::CursorEnemy) >= 12;
				const FTurnUnitState* HoundState = TurnBased->GetUnitState(GridEnemy);
				UE_LOG(LogCodexTactics, Display, TEXT("Cursor over hound: state %d cell (%d, %d) occupant %d, cursor (%d, %d) enemy %d lines %d, phase %d"),
					HoundState ? 1 : 0, HoundState ? HoundState->GridPos.X : -1, HoundState ? HoundState->GridPos.Y : -1,
					HoundState ? static_cast<int32>(TurnBased->GetGrid()->GetOccupantType(HoundState->GridPos)) : -1,
					Overlay->GetCursorCell().X, Overlay->GetCursorCell().Y, Overlay->IsCursorOnEnemy() ? 1 : 0,
					Overlay->GetCellCount(ETurnOverlayLayer::CursorEnemy), static_cast<int32>(TurnBased->GetPhase()));
				const FIntPoint LeaderCell = TurnBased->GetGrid()->WorldToGrid(TurnBased->GetActiveUnit()->GetActorLocation());
				FIntPoint Empty(-999, -999);
				for (const FIntPoint Offset : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
				{
					if (TurnBased->GetGrid()->IsValidCell(LeaderCell + Offset) && TurnBased->GetGrid()->GetOccupantType(LeaderCell + Offset) == EGorkyOccupantType::None)
					{
						Empty = LeaderCell + Offset;
						break;
					}
				}
				TurnBased->SetHoveredPoint(TurnBased->GetGrid()->GridToWorld(Empty));
				const bool bMoveFrame = !Overlay->IsCursorOnEnemy() && Overlay->GetCursorCell() == Empty
					&& Overlay->GetCellCount(ETurnOverlayLayer::CursorMove) == 4 && Overlay->GetCellCount(ETurnOverlayLayer::CursorEnemy) == 0;
				Check(State, bEnemyFrame && bMoveFrame, FString::Printf(TEXT("cursor frame: red over an enemy %d, yellow over an empty cell %d"),
					bEnemyFrame ? 1 : 0, bMoveFrame ? 1 : 0));
			}
			else
			{
				Check(State, false, TEXT("an enemy on the grid and the overlay for the cursor check"));
			}
			return true;
		}
		// From step 8: every case takes three steps (press, release, check).
		const int32 CaseStep = Step - 8;
		if (CaseStep < 0)
		{
			return true;
		}
		const int32 CaseIndex = CaseStep / 3;
		if (CaseIndex >= UE_ARRAY_COUNT(Cases))
		{
			return Finish(State, true);
		}
		const FKeyCase& Case = Cases[CaseIndex];
		switch (CaseStep % 3)
		{
		case 0:
			PressKey(PC, Case.Key, IE_Pressed);
			break;
		case 1:
			PressKey(PC, Case.Key, IE_Released);
			break;
		default:
		{
			const AOperativeCharacter* Active = TurnBased->GetActiveUnit();
			Check(State, Active && Active->SquadIndex == Case.SquadIndex, FString::Printf(TEXT("key %s (active: %s, phase %d, busy %d)"),
				Case.Name, Active ? *Active->DisplayName.ToString() : TEXT("none"), static_cast<int32>(TurnBased->GetPhase()), TurnBased->IsBusy() ? 1 : 0));
			break;
		}
		}
		return true;
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
		FTimerHandle StartHandle;
		World->GetTimerManager().SetTimer(StartHandle, FTimerDelegate::CreateLambda([WeakWorld, State, Handle]()
		{
			if (UWorld* W = WeakWorld.Get())
			{
				W->GetTimerManager().SetTimer(*Handle, FTimerDelegate::CreateLambda([WeakWorld, State, Handle]()
				{
					UWorld* W2 = WeakWorld.Get();
					if (W2 && !Step(W2, *State))
					{
						W2->GetTimerManager().ClearTimer(*Handle);
					}
				}), StepSeconds, true);
			}
		}), 3.f, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.TurnSelectSmoke"),
		TEXT("Dev check: number keys 1-4 select the active operative (Susanin as 4) in the turn-based fight; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
