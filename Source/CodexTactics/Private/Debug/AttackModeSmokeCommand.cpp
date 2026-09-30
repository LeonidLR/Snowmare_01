// Dev-only console command for a headless check of the turn-based attack mode on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.AttackModeSmoke
// Godot turn_based_combat_manager.gd enter / exit / toggle_attack_mode, tactical_grid_overlay.gd update_attack_pattern
// (dot matrix with a distance falloff) and the hit-chance label, main.gd KEY_F / RMB / Esc / empty-cell click.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Tactics/GorkyGridManager.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "Tactics/TurnBasedRules.h"
#include "Tactics/TurnGridOverlayActor.h"
#include "TimerManager.h"
#include "UI/GameMessageSubsystem.h"

namespace AttackModeSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		bool bPrepared = false;
		TWeakObjectPtr<AEnemyCharacter> Enemy;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("AttackModeSmoke"));
		return false;
	}

	bool LastLineContains(UWorld* World, const TCHAR* Part)
	{
		const UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>();
		return Messages && !Messages->GetHistory().IsEmpty() && Messages->GetHistory().Last().Text.ToString().Contains(Part);
	}

	ATurnGridOverlayActor* FindOverlay(UWorld* World)
	{
		for (TActorIterator<ATurnGridOverlayActor> It(World); It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 40.f)
		{
			return World ? Finish(State, false) : false;
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		UTurnBasedCombatSubsystem* TurnBased = World->GetSubsystem<UTurnBasedCombatSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
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
				Flow->TriggerCombatZone();
				Flow->FinishCutscene();
				Flow->FinishPreparation();
				// Keep one wave enemy (moved in front) and drop the rest: an empty wave would be cleared at once.
				AEnemyCharacter* Kept = nullptr;
				for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
				{
					if (!Kept && !It->IsDying())
					{
						Kept = *It;
					}
					else
					{
						It->Destroy();
					}
				}
				if (Kept)
				{
					// Parked far behind the squad and frozen: outside the 15 m fight, never reaching the squad.
					Kept->SetActorLocation(Squad->GetLeader()->GetActorLocation() - Squad->GetLeader()->GetActorForwardVector() * 4000.f);
					Kept->CustomTimeDilation = 0.f;
				}
				AOperativeCharacter* Leader = Squad->GetLeader();
				State.Enemy = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound,
					Leader->GetActorLocation() + Leader->GetActorForwardVector() * 500.f + FVector(0.f, 0.f, 20.f));
				State.bPrepared = true;
				State.StageTime = 2.5f;
				return true;
			}
			Check(State, Flow->RequestEnterTurnBased(true) == EGameFlowResult::Ok && TurnBased->IsActive(), TEXT("turn-based combat started"));
			ATurnGridOverlayActor* Overlay = FindOverlay(World);
			Check(State, !TurnBased->IsAttackMode() && Overlay && Overlay->GetCellCount(ETurnOverlayLayer::Reachable) > 0
				&& Overlay->GetCellCount(ETurnOverlayLayer::Attack) == 0, TEXT("move mode: green walk cells only"));

			// F: the attack mode with the weapon's dot matrix.
			TurnBased->ToggleAttackMode(); // the F key's action
			Check(State, TurnBased->IsAttackMode() && LastLineContains(World, TEXT("Режим прицеливания")), TEXT("F enters the attack mode"));
			Check(State, Overlay->GetCellCount(ETurnOverlayLayer::Reachable) == 0 && Overlay->GetCellCount(ETurnOverlayLayer::Attack) > 0,
				FString::Printf(TEXT("attack mode: %d matrix cells, no walk cells"), Overlay->GetCellCount(ETurnOverlayLayer::Attack)));
			const FTurnUnitState* Unit = TurnBased->GetUnitState(TurnBased->GetActiveUnit());
			const TMap<FIntPoint, FTurnBasedAttackCell> Cells = TurnBasedRules::GetWeaponAttackCells(*TurnBased->GetGrid(), Unit->GridPos,
				TurnBased->GetActiveUnit()->CurrentWeapon, Unit->Stance, FTurnBasedBalance());
			float Near = -1.f;
			float Far = 2.f;
			for (const TPair<FIntPoint, FTurnBasedAttackCell>& Entry : Cells)
			{
				const float Value = Overlay->GetAttackCellFalloff(Entry.Key);
				if (Entry.Value.Distance == 1)
				{
					Near = Value;
				}
				if (Entry.Value.Distance == Entry.Value.MaxRange && Entry.Value.MaxRange > 1)
				{
					Far = FMath::Min(Far, Value);
				}
			}
			Check(State, FMath::IsNearlyEqual(Near, 1.f) && FMath::IsNearlyEqual(Far, 0.25f, 0.01f),
				FString::Printf(TEXT("falloff: 1st cell %.2f, farthest %.2f"), Near, Far));

			// Hit chance over a matrix cell.
			if (Cells.Num() > 0)
			{
				TArray<FIntPoint> Keys;
				Cells.GetKeys(Keys);
				TurnBased->SetHoveredPoint(TurnBased->GetGrid()->GridToWorld(Keys[0]));
				FVector LabelWorld;
				FString LabelText;
				Check(State, TurnBased->GetHoverHitChance(LabelWorld, LabelText) && LabelText.Contains(TEXT("%")), FString::Printf(TEXT("hit chance label «%s»"), *LabelText));
			}

			// An empty walkable cell does not walk in the attack mode.
			const FIntPoint Before = Unit->GridPos;
			for (const TPair<FIntPoint, int32>& Entry : TurnBased->GetGrid()->GetReachableCells(Unit->GridPos, 2))
			{
				if (Entry.Key != Unit->GridPos && TurnBased->GetGrid()->GetOccupantType(Entry.Key) == EGorkyOccupantType::None)
				{
					TurnBased->HandleWorldClick(TurnBased->GetGrid()->GridToWorld(Entry.Key), nullptr, false);
					break;
				}
			}
			Check(State, TurnBased->GetUnitState(TurnBased->GetActiveUnit())->GridPos == Before && !TurnBased->IsUnitMoving()
				&& LastLineContains(World, TEXT("нет цели для выстрела")), TEXT("empty cell in the attack mode: no walk, a hint"));

			// Esc / RMB leave it with the line; F toggles back.
			TurnBased->ExitAttackMode(TEXT("🟢 Прицеливание отменено (возврат в режим перемещения)."));
			Check(State, !TurnBased->IsAttackMode() && Overlay->GetCellCount(ETurnOverlayLayer::Attack) == 0
				&& Overlay->GetCellCount(ETurnOverlayLayer::Reachable) > 0 && LastLineContains(World, TEXT("Прицеливание отменено")),
				TEXT("leaving the attack mode: walk cells back"));
			TurnBased->ToggleAttackMode(); // the F key's action
			Check(State, TurnBased->IsAttackMode(), TEXT("F again: attack mode"));
			TurnBased->EndCurrentUnitTurn();
			Check(State, !TurnBased->IsAttackMode(), TEXT("next operative: move mode"));
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
		TEXT("CodexTactics.AttackModeSmoke"),
		TEXT("Dev check: turn-based attack mode (F, dot matrix falloff, hit chance, empty cell, exit); logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
