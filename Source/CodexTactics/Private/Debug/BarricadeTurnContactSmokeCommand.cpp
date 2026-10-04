// Dev-only console command for a headless check of contact barricades in turn-based combat (Sprint 06-C) on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.BarricadeTurnContactSmoke
// The real-time contact timer kept ticking in turn-based combat: an enemy next to a spiked barricade lost health every
// second of the squad's planning. Now the timer stands still and the enemy takes the contact damage once, when its turn
// starts.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarricadeActor.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "TimerManager.h"

namespace BarricadeTurnContactSmoke
{
	struct FState
	{
		float Time = 0.f;
		int32 Stage = 0;
		bool bOk = true;
		float HealthAtStart = 0.f;
		TWeakObjectPtr<AEnemyCharacter> Hound;
	};

	void Check(FState& State, bool bCondition, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bCondition ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.bOk &= bCondition;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.bOk && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("BarricadeTurnContactSmoke"));
		return false;
	}

	bool Step(UWorld* World, FState& State)
	{
		State.Time += 0.1f;
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		UTurnBasedCombatSubsystem* TurnBased = World->GetSubsystem<UTurnBasedCombatSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Flow || !TurnBased || !Leader)
		{
			return Finish(State, false);
		}
		AEnemyCharacter* Hound = State.Hound.Get();
		switch (State.Stage)
		{
		case 0:
		{
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy();
			}
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->HealthComponent->SetMaxHealth(100000.f);
			}
			const FVector Forward = Leader->GetActorForwardVector().GetSafeNormal2D();
			Hound = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound, Leader->GetActorLocation() + Forward * 900.f);
			State.Hound = Hound;
			Check(State, Hound && Flow->RequestEnterTurnBased(true) == EGameFlowResult::Ok && TurnBased->IsActive(), TEXT("turn-based combat with a hound"));
			if (!Hound)
			{
				return Finish(State, false);
			}
			Hound->GetHealthComponent()->SetMaxHealth(1000.f);
			// A spiked barricade right next to the hound (after the grid placed it).
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			const FVector HoundFeet = Hound->GetActorLocation() - FVector(0.f, 0.f, Hound->GetSimpleCollisionHalfHeight());
			if (ABarricadeActor* Barricade = World->SpawnActor<ABarricadeActor>(HoundFeet + FVector(150.f, 0.f, 50.f), FRotator::ZeroRotator, Params))
			{
				Barricade->ContactType = EBarricadeContact::Physical;
				Barricade->ContactDamage = 40.f;
				Barricade->ContactTickInterval = 1.f;
			}
			State.HealthAtStart = Hound->GetHealthComponent()->GetCurrentHealth();
			State.Stage = 1;
			State.Time = 0.f;
			return true;
		}
		case 1:
			// The squad plans for 3 s: no contact damage meanwhile.
			if (State.Time < 3.f)
			{
				return true;
			}
			Check(State, Hound && Hound->GetHealthComponent()->GetCurrentHealth() == State.HealthAtStart && TurnBased->GetContactHitsThisFight() == 0,
				FString::Printf(TEXT("3 s of the squad's turn: the hound keeps %.0f HP (was %.0f)"), Hound ? Hound->GetHealthComponent()->GetCurrentHealth() : 0.f,
					State.HealthAtStart));
			State.Stage = 2;
			State.Time = 0.f;
			return true;
		case 2:
			// End the operatives' turns until the hound has had its turn.
			if (TurnBased->GetContactHitsThisFight() == 0 && State.Time < 30.f)
			{
				if (TurnBased->GetPhase() == ETurnPhase::Squad)
				{
					TurnBased->EndCurrentUnitTurn();
				}
				return true;
			}
			{
				const float Health = Hound ? Hound->GetHealthComponent()->GetCurrentHealth() : 0.f;
				Check(State, TurnBased->GetContactHitsThisFight() == 1 && Health < State.HealthAtStart,
					FString::Printf(TEXT("the hound's turn: one contact hit (%d), %.0f -> %.0f HP"), TurnBased->GetContactHitsThisFight(),
						State.HealthAtStart, Health));
				State.HealthAtStart = Health;
			}
			State.Stage = 3;
			State.Time = 0.f;
			return true;
		case 3:
			// Back in the squad's turn: nothing more until the next enemy turn.
			if (State.Time < 3.f)
			{
				return true;
			}
			Check(State, TurnBased->GetContactHitsThisFight() == 1 && Hound && Hound->GetHealthComponent()->GetCurrentHealth() == State.HealthAtStart,
				TEXT("no more contact damage during the next squad turn"));
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
				}), 0.1f, true);
			}
		}), 3.f, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(TEXT("CodexTactics.BarricadeTurnContactSmoke"),
		TEXT("Dev check: contact barricades hurt in turn-based combat once per enemy turn, not over time; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
