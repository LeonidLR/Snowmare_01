// Dev-only console command for a headless check of two turn-based rules on L_MovementTest (user decisions 2026-10-04):
//   Scripts/smoke.ps1 -Command CodexTactics.TurnRulesSmoke
// 1. a crouched operative pays double AP per step (two orthogonal steps: 4 AP instead of 2);
// 2. a medkit used by the active operative ends his turn (the next operative becomes active).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/PersonalItemRules.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Tactics/GorkyGridManager.h"
#include "Tactics/TurnBasedCombatSubsystem.h"

namespace TurnRulesSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
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
		FPlatformMisc::RequestExit(false, TEXT("TurnRulesSmoke"));
		return false;
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
		UTurnBasedCombatSubsystem* TurnBased = World->GetSubsystem<UTurnBasedCombatSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f)
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
			World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Brute,
				Leader->GetActorLocation() + Leader->GetActorForwardVector() * 1050.f + FVector(0.f, 0.f, 20.f));
			Check(State, Flow->RequestEnterTurnBased(true) == EGameFlowResult::Ok && TurnBased->IsActive(), TEXT("turn-based combat started"));
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
		}
		case 1:
		{
			if (TurnBased->IsBusy() || State.StageTime < 0.5f)
			{
				return true;
			}
			AOperativeCharacter* Unit = TurnBased->GetActiveUnit();
			Check(State, Unit && TurnBased->SetActiveUnitStance(EOperativeStance::Crouching), TEXT("active operative crouches"));
			const FTurnUnitState* UnitState = TurnBased->GetUnitState(Unit);
			State.APBefore = UnitState ? UnitState->AP : 0;
			// Two free orthogonal steps away (any side).
			bool bMoved = false;
			const FIntPoint Directions[] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
			for (const FIntPoint& Dir : Directions)
			{
				const FIntPoint Target = UnitState->GridPos + Dir * 2;
				if (TurnBased->GetGrid()->IsValidCell(Target) && TurnBased->GetGrid()->IsCellWalkable(Target)
					&& TurnBased->GetGrid()->IsCellWalkable(UnitState->GridPos + Dir) && TurnBased->MoveActiveUnitTo(Target))
				{
					bMoved = true;
					break;
				}
			}
			Check(State, bMoved, TEXT("crouched walk of two cells ordered"));
			State.Stage = 2;
			State.StageTime = 0.f;
			return true;
		}
		case 2:
		{
			if (TurnBased->IsBusy() || TurnBased->IsUnitMoving())
			{
				return true;
			}
			AOperativeCharacter* Unit = TurnBased->GetActiveUnit();
			const FTurnUnitState* UnitState = TurnBased->GetUnitState(Unit);
			const int32 Spent = State.APBefore - (UnitState ? UnitState->AP : 0);
			Check(State, Spent == 4, FString::Printf(TEXT("crouched: two steps cost %d AP (standing: 2)"), Spent));
			// A medkit ends his turn.
			FDamageSpec Wound;
			Wound.Amount = 30.f;
			Unit->HealthComponent->ApplyDamage(Wound);
			Unit->MedkitsCount = FMath::Max(Unit->MedkitsCount, 1);
			const bool bUsed = Unit->UsePersonalItem(EPersonalItem::Medkit);
			const bool bEnded = bUsed && TurnBased->EndTurnAfterMedkit(Unit);
			Check(State, bEnded && TurnBased->GetActiveUnit() != Unit, FString::Printf(TEXT("medkit used (%d) ends the turn: next active %s"),
				bUsed ? 1 : 0, TurnBased->GetActiveUnit() ? *TurnBased->GetActiveUnit()->DisplayName.ToString() : TEXT("-")));
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
		TEXT("CodexTactics.TurnRulesSmoke"),
		TEXT("Dev check: crouched steps cost double AP, a medkit ends the turn (turn-based); logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
