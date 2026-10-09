// Dev-only console command for a headless regression check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.TurnBlastDeathSmoke
// A turn-based barrel shot whose blast kills an operative (not the commander) drops him from the fight in the middle of
// the blast; the fight goes on (user decision 2026-10-08). The game must not crash (user report 2026-09-30: Shift +
// barrel shot crashed).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

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
#include "Interactables/BarrelActor.h"
#include "Tactics/GorkyGridManager.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "TimerManager.h"

namespace TurnBlastDeathSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		bool bPrepared = false;
		TWeakObjectPtr<ABarrelActor> Barrel;
		TWeakObjectPtr<AOperativeCharacter> Victim;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("TurnBlastDeathSmoke"));
		return false;
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
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			AOperativeCharacter* Leader = Squad->GetLeader();
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
				const FVector Forward = Leader->GetActorForwardVector();
				const FVector Right = Leader->GetActorRightVector();
				const FVector BarrelSpot = Leader->GetActorLocation() + Forward * 300.f;
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				State.Barrel = World->SpawnActor<ABarrelActor>(BarrelSpot - FVector(0.f, 0.f, 50.f), FRotator::ZeroRotator, Params);
				for (AOperativeCharacter* Member : Members)
				{
					if (Member != Leader && !State.Victim.IsValid())
					{
						State.Victim = Member;
						Member->TeleportTo(BarrelSpot + Right * 150.f, Member->GetActorRotation(), false, true);
					}
				}
				World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound, Leader->GetActorLocation() + Forward * 900.f);
				State.bPrepared = true;
				State.StageTime = 2.f;
				return true;
			}
			Check(State, State.Barrel.IsValid() && State.Victim.IsValid(), TEXT("barrel and a squad mate next to it"));
			Check(State, Flow->RequestEnterTurnBased(true) == EGameFlowResult::Ok && TurnBased->IsActive(), TEXT("turn-based combat started"));
			const FTurnUnitState* BarrelState = TurnBased->GetUnitState(State.Barrel.Get());
			const FTurnUnitState* VictimState = TurnBased->GetUnitState(State.Victim.Get());
			Check(State, BarrelState && VictimState && FMath::Max(FMath::Abs(BarrelState->GridPos.X - VictimState->GridPos.X),
				FMath::Abs(BarrelState->GridPos.Y - VictimState->GridPos.Y)) <= 1, TEXT("the mate stands in the blast"));
			if (!BarrelState || !VictimState)
			{
				return Finish(State, false);
			}
			// One hit point left: the blast kills the mate during the detonation. User decision 2026-10-08: only the
			// commander's death fails the mission, so the fight goes on without him (out of the turn order and the grid).
			UHealthComponent* Health = State.Victim->HealthComponent;
			Health->ApplyDirectHealthLoss(Health->GetCurrentHealth() - 1.f, TEXT("Smoke"));
			TurnBased->AttackCell(BarrelState->GridPos, true, true);
			State.Stage = 10; // the shot is shown once the shooter is on target (TurnAttackTimeline, user request 2026-10-09)
			State.StageTime = 0.f;
			return true;
		}
		case 10:
		{
			if (TurnBased->IsActive() && TurnBased->IsShotPending() && State.StageTime < 4.f)
			{
				return true;
			}
			// (The same blast may also clear the last enemy: then the fight ends in a victory, never in a defeat.)
			Check(State, !State.Victim->HealthComponent->IsAlive() && Flow->GetPhase() != ECodexGamePhase::GameOver
				&& TurnBased->GetUnitState(State.Victim.Get()) == nullptr,
				FString::Printf(TEXT("the blast killed the mate: no crash, no defeat, he is out of the turn order (alive %d, turn-based %d, phase %s, unit state %d)"),
					State.Victim->HealthComponent->IsAlive() ? 1 : 0, TurnBased->IsActive() ? 1 : 0, *UEnum::GetValueAsString(Flow->GetPhase()),
					TurnBased->GetUnitState(State.Victim.Get()) ? 1 : 0));
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
		}
		case 1:
			// A few more frames (the blast's timers, the death cinematic) on the running fight: no crash.
			if (State.StageTime < 3.f)
			{
				return true;
			}
			Check(State, TurnBased->IsActive() || Flow->GetPhase() != ECodexGamePhase::GameOver, TEXT("still no game over after the blast"));
			return Finish(State, true);
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
		TEXT("CodexTactics.TurnBlastDeathSmoke"),
		TEXT("Dev check: a turn-based barrel blast that kills an operative ends the mission without a crash; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
