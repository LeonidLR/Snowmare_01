// Dev-only console command for a headless panic check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.PanicSmoke
// Godot panic_component.gd in the real-time fight (enabled by the user decision 2026-10-01): Susanin (a recruit) is
// stressed to the edge and hit — he panics: no shooting, orders refused, runs away from the enemy, then cowers
// crouched; meanwhile the engineer reaching 100 stress holds on at 95 (one panicking member at a time); after the panic
// time Susanin recovers on his feet and can shoot again. The tuned data keeps the core squad far more resistant than
// the recruit.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/PanicComponent.h"
#include "Characters/RecruitSubsystem.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

namespace PanicSmoke
{
	constexpr float StepSeconds = 0.25f;

	struct FState
	{
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		TWeakObjectPtr<AOperativeCharacter> Susanin;
		TWeakObjectPtr<AOperativeCharacter> Engineer;
		TWeakObjectPtr<AEnemyCharacter> Hound;
		FVector PanicStart = FVector::ZeroVector;
		float StartEnemyDistance = 0.f;
		bool bSawFleeing = false;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("PanicSmoke"));
		return false;
	}

	void NextStage(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
	}

	bool Step(UWorld* World, FState& State)
	{
		State.StageTime += StepSeconds;
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Flow || !Leader || State.StageTime > 30.f)
		{
			Check(State, false, FString::Printf(TEXT("flow / leader / stage %d timeout"), State.Stage));
			return Finish(State, false);
		}
		AOperativeCharacter* Susanin = State.Susanin.Get();
		UPanicComponent* SusaninPanic = Susanin ? Susanin->PanicComponent.Get() : nullptr;
		switch (State.Stage)
		{
		case 0:
		{
			URecruitSubsystem* Recruits = World->GetSubsystem<URecruitSubsystem>();
			Susanin = Recruits ? Recruits->GetOrSpawnSusanin() : nullptr;
			if (!Susanin)
			{
				Check(State, false, TEXT("Susanin spawned"));
				return Finish(State, false);
			}
			Susanin->TeleportTo(Leader->GetActorLocation() - Leader->GetActorRightVector() * 300.f, Leader->GetActorRotation(), false, true);
			Recruits->RecruitIntoSquad(true);
			State.Susanin = Susanin;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member->SquadRole == EOperativeRole::Engineer)
				{
					State.Engineer = Member;
				}
			}
			// A real-time wave: its enemies frozen far away, one frozen, unkillable hound 6 m from Susanin.
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->CustomTimeDilation = 0.f;
				It->SetActorLocation(It->GetActorLocation() + FVector(0.f, 0.f, -5000.f), false, nullptr, ETeleportType::TeleportPhysics);
			}
			State.Hound = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound,
				Susanin->GetActorLocation() + Susanin->GetActorForwardVector() * 600.f + FVector(0.f, 0.f, 20.f));
			if (State.Hound.IsValid())
			{
				State.Hound->CustomTimeDilation = 0.f;
				State.Hound->GetHealthComponent()->SetMaxHealth(100000.f);
			}
			// The data (DA_GameBalanceConfig): one panicking member, the core squad far more resistant than the recruit.
			const UPanicComponent* LeaderPanic = Leader->PanicComponent;
			const UPanicComponent* RecruitPanic = Susanin->PanicComponent;
			Check(State, LeaderPanic && RecruitPanic && RecruitPanic->Config.MaxPanickedMembers == 1
				&& LeaderPanic->Config.StressGainMultiplier * 2.f <= RecruitPanic->Config.StressGainMultiplier,
				FString::Printf(TEXT("data: max panicked %d, stress gain commander %.2f vs recruit %.2f"),
					RecruitPanic ? RecruitPanic->Config.MaxPanickedMembers : -1, LeaderPanic ? LeaderPanic->Config.StressGainMultiplier : -1.f,
					RecruitPanic ? RecruitPanic->Config.StressGainMultiplier : -1.f));
			NextStage(State);
			return true;
		}
		case 1:
			if (State.StageTime < 1.f || !SusaninPanic || !State.Hound.IsValid())
			{
				return State.StageTime < 5.f;
			}
			Check(State, SusaninPanic->IsCombatActive(), TEXT("real-time fight"));
			// On the edge, then a hit.
			SusaninPanic->SetStressForTesting(99.f);
			SusaninPanic->OnDamageTaken(10.f);
			State.PanicStart = Susanin->GetActorLocation();
			State.StartEnemyDistance = FVector::Dist2D(State.PanicStart, State.Hound->GetActorLocation());
			Check(State, SusaninPanic->IsPanicking() && SusaninPanic->GetPhase() == EPanicPhase::Fleeing && !Susanin->CanShoot(),
				TEXT("Susanin panics: fleeing, cannot shoot"));
			Check(State, Susanin->OrderMoveTo(Susanin->GetActorLocation() + FVector(300.f, 0.f, 0.f), false) == EOperativeOrderResult::Refused,
				TEXT("orders refused while panicking"));
			// The engineer at 100 stress cannot panic too (limit 1): he holds on at 95.
			if (AOperativeCharacter* Engineer = State.Engineer.Get(); Engineer && Engineer->PanicComponent)
			{
				Engineer->PanicComponent->SetStressForTesting(99.f);
				Engineer->PanicComponent->OnDamageTaken(30.f);
				Check(State, !Engineer->PanicComponent->IsPanicking() && FMath::IsNearlyEqual(Engineer->PanicComponent->GetStress(), 95.f, 0.5f),
					FString::Printf(TEXT("one panicking member: the engineer holds on (stress %.0f)"), Engineer->PanicComponent->GetStress()));
			}
			NextStage(State);
			return true;
		case 2:
			// Flight, then cowering crouched.
			if (SusaninPanic && SusaninPanic->GetPhase() == EPanicPhase::Fleeing)
			{
				State.bSawFleeing = true;
				return true;
			}
			if (!Susanin || !SusaninPanic || !State.Hound.IsValid())
			{
				return Finish(State, false);
			}
			{
				const float Ran = FVector::Dist2D(Susanin->GetActorLocation(), State.PanicStart);
				const float EnemyDistance = FVector::Dist2D(Susanin->GetActorLocation(), State.Hound->GetActorLocation());
				Check(State, State.bSawFleeing && Ran > 200.f && EnemyDistance > State.StartEnemyDistance,
					FString::Printf(TEXT("fled %.0f cm, away from the hound (%.0f -> %.0f cm)"), Ran, State.StartEnemyDistance, EnemyDistance));
				Check(State, SusaninPanic->IsPanicking() && SusaninPanic->GetPhase() == EPanicPhase::Cowering
					&& Susanin->GetStance() == EOperativeStance::Crouching && SusaninPanic->IsAuraShown(),
					TEXT("cowering crouched, red ring under him"));
			}
			NextStage(State);
			return true;
		case 3:
			// Recovery: the panic time (4.5 - 9 s for Susanin in Godot data) runs out.
			if (SusaninPanic && SusaninPanic->IsPanicking())
			{
				return true;
			}
			Check(State, Susanin && !SusaninPanic->IsPanicking() && Susanin->GetStance() == EOperativeStance::Standing
				&& Susanin->CanShoot() && !SusaninPanic->IsAuraShown(),
				FString::Printf(TEXT("recovered after %.1f s: standing, can shoot (stress %.0f)"), State.StageTime, SusaninPanic ? SusaninPanic->GetStress() : -1.f));
			return Finish(State, true);
		default:
			return Finish(State, true);
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
				}), StepSeconds, true);
			}
		}), 3.f, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.PanicSmoke"),
		TEXT("Dev check: panic flight / cowering / recovery, one panicking member, resistant core squad; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
