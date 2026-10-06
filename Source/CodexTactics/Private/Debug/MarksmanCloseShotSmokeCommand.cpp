// Dev-only headless repro of the 2026-10-06 bug report on L_MovementTest (not saved):
//   Scripts/smoke.ps1 -Command CodexTactics.MarksmanCloseShotSmoke -Log Smoke-MarksmanCloseShot.log
// Turn-based fight with a PRONE marksman 1.5 m (the next cell, or the one after) in front of the commander; the commander shoots him. Before the fix the
// hit ran the marksman's kiting (StartRetreat: stand up, 0.45 s later a 520 cm/s path move to a firing position ~27 m
// away) although the grid had frozen him — the actor tick was off, the movement component / path following were not —
// so he stood up and «flew» backwards off the grid. Checks: his place stays within half a cell for 3 s, he stays on the
// grid, the AI controller refuses moves while held, the guard was not needed (root cause fixed); then a forced launch
// off the grid is put back by the guard (UTurnBasedCombatSubsystem::EnforceHeldEnemies).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AIController.h"
#include "Characters/MarksmanEnemyCharacter.h"
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
#include "Navigation/PathFollowingComponent.h"
#include "Tactics/GorkyGridManager.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "Tactics/TurnClickRules.h"
#include "TimerManager.h"

namespace MarksmanCloseShotSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		int32 Stage = 0;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		TWeakObjectPtr<AMarksmanEnemyCharacter> Marksman;
		FVector Start = FVector::ZeroVector;
		float MaxDisplacement = 0.f;
		bool bLeftGrid = false;
		bool bStoodUp = false;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("MarksmanCloseShotSmoke"));
		return false;
	}

	void Next(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 40.f)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in stage %d"), State.Stage);
			return Finish(State, false);
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		UTurnBasedCombatSubsystem* TurnBased = World->GetSubsystem<UTurnBasedCombatSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AMarksmanEnemyCharacter* Marksman = State.Marksman.Get();
		if (State.Stage > 0 && (!Marksman || !TurnBased->IsActive()))
		{
			Check(State, false, TEXT("marksman alive and the turn-based fight still on"));
			return Finish(State, false);
		}
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->HealthComponent->SetMaxHealth(100000.f);
			}
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy(); // only our marksman
			}
			AOperativeCharacter* Leader = Squad->GetLeader();
			const FVector Forward = Leader->GetActorForwardVector().GetSafeNormal2D();
			Marksman = Cast<AMarksmanEnemyCharacter>(World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Marksman,
				Leader->GetActorLocation() + Forward * 150.f + FVector(0.f, 0.f, 20.f), (-Forward).Rotation()));
			if (!Marksman)
			{
				Check(State, false, TEXT("marksman spawned"));
				return Finish(State, false);
			}
			State.Marksman = Marksman;
			Marksman->GetHealthComponent()->SetMaxHealth(100000.f);
			Marksman->SetMarksmanStance(EOperativeStance::Prone); // the sniper lying in wait, as in the report
			const EGameFlowResult Result = Flow->RequestEnterTurnBased(true);
			Check(State, Result == EGameFlowResult::Ok && TurnBased->IsActive() && TurnBased->GetUnitState(Marksman) != nullptr,
				TEXT("turn-based fight with the prone marksman on the grid"));
			Check(State, Marksman->IsTurnBasedHeld(), TEXT("the grid holds the marksman (SetTurnBasedHeld)"));
			State.Start = Marksman->GetActorLocation();
			const float Gap = FVector::Dist2D(State.Start, Leader->GetActorLocation());
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: marksman %.1f m from the commander, stance %d"), Gap / 100.f, static_cast<int32>(Marksman->GetStance()));
			// User request 2026-10-06: Ctrl + click is the attack order on the grid too — on an empty cell it never walks.
			UGorkyGridManager* Grid = TurnBased->GetGrid();
			const AOperativeCharacter* Unit = TurnBased->GetActiveUnit();
			const FTurnUnitState* UnitState = TurnBased->GetUnitState(Unit);
			if (Grid && UnitState)
			{
				const FIntPoint From = UnitState->GridPos;
				for (const FIntPoint Offset : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
				{
					const FIntPoint Empty = From + Offset;
					if (Grid->IsValidCell(Empty) && Grid->IsCellWalkable(Empty) && Grid->GetOccupantType(Empty) == EGorkyOccupantType::None)
					{
						TurnBased->HandleWorldClick(Grid->GridToWorld(Empty), nullptr, /*bAttackOrder (Ctrl)*/ true);
						Check(State, !TurnBased->IsUnitMoving() && TurnBased->GetUnitState(Unit)->GridPos == From,
							TEXT("Ctrl + click on an empty cell: no walk (attack order only)"));
						break;
					}
				}
			}
			// The shot: Ctrl + click on the marksman (the grid attack when the active operative has a lane), else the same
			// hit straight on the health (both run HandleMarksmanDamaged, the path of the bug).
			TurnBased->bGuaranteeAllHits = true;
			const float HealthBefore = Marksman->GetHealthComponent()->GetCurrentHealth();
			TurnBased->HandleWorldClick(Marksman->GetActorLocation(), Marksman, /*bAttackOrder (Ctrl)*/ true);
			const bool bShot = Marksman->GetHealthComponent()->GetCurrentHealth() < HealthBefore;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: Ctrl + click grid shot %s"), bShot ? TEXT("hit") : TEXT("had no lane - direct hit instead"));
			if (!bShot)
			{
				FDamageSpec Spec;
				Spec.Amount = 30.f;
				Spec.AttackerSource = Leader->DisplayName.ToString();
				Marksman->GetHealthComponent()->ApplyDamage(Spec);
			}
			Check(State, Marksman->GetHealthComponent()->GetCurrentHealth() < Marksman->GetHealthComponent()->GetMaxHealth(),
				TEXT("the marksman was hit point-blank"));
			Next(State);
			return true;
		}
		case 1:
		{
			// 3 s: the get-up run would have started at 0.45 s and covered ~13 m.
			const FVector Now = Marksman->GetActorLocation();
			State.MaxDisplacement = FMath::Max(State.MaxDisplacement, static_cast<float>(FVector::Dist2D(Now, State.Start)));
			const UGorkyGridManager* Grid = TurnBased->GetGrid();
			State.bLeftGrid |= Grid && !TurnClickRules::IsInsideGrid(Now, Grid->OriginWorld, Grid->GridSize.X, Grid->CellSize);
			State.bStoodUp |= Marksman->GetStance() == EOperativeStance::Standing;
			if (State.StageTime < 3.f)
			{
				return true;
			}
			Check(State, State.MaxDisplacement < 75.f, FString::Printf(TEXT("the shot marksman stays on his cell (max %.0f cm moved)"), State.MaxDisplacement));
			Check(State, !State.bLeftGrid, TEXT("he never leaves the grid"));
			Check(State, !State.bStoodUp, TEXT("no get-up for a kiting run while the grid holds him"));
			Check(State, Marksman->GetVelocity().Size2D() < 5.f, FString::Printf(TEXT("no velocity (%.0f cm/s)"), Marksman->GetVelocity().Size2D()));
			Check(State, TurnBased->GetGuardCorrections() == 0, TEXT("root cause fixed: the guard never had to put him back"));
			if (AAIController* AIC = Cast<AAIController>(Marksman->GetController()))
			{
				const EPathFollowingRequestResult::Type Move = AIC->MoveToLocation(State.Start + FVector(2500.f, 0.f, 0.f), 50.f, false, true);
				Check(State, Move == EPathFollowingRequestResult::Failed, TEXT("an AI move order is refused while held"));
			}
			// Guard: whatever throws him (a launch here), he is put back on his cell.
			Marksman->LaunchCharacter(FVector(-2500.f, 0.f, 300.f), true, true);
			Next(State);
			return true;
		}
		case 2:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			const UGorkyGridManager* Grid = TurnBased->GetGrid();
			const FVector Now = Marksman->GetActorLocation();
			Check(State, TurnBased->GetGuardCorrections() > 0, FString::Printf(TEXT("the guard caught the launch (%d corrections)"), TurnBased->GetGuardCorrections()));
			Check(State, FVector::Dist2D(Now, State.Start) < 75.f && Grid && TurnClickRules::IsInsideGrid(Now, Grid->OriginWorld, Grid->GridSize.X, Grid->CellSize),
				FString::Printf(TEXT("launched enemy back on his cell (%.0f cm off)"), FVector::Dist2D(Now, State.Start)));
			Flow->ExitTurnBasedToRealTime();
			Check(State, !Marksman->IsTurnBasedHeld(), TEXT("released when the fight returns to real time"));
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
		TEXT("CodexTactics.MarksmanCloseShotSmoke"),
		TEXT("Dev check (bug 2026-10-06): a prone marksman shot point-blank in turn-based combat stays on his cell / the grid; the guard puts a launched enemy back; PASS / FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
