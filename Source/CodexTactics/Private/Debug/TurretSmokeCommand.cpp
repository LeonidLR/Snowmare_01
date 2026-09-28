// Dev-only console command for a headless turret / generator check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.TurretSmoke
// 1. a turret shoots an enemy in range; 2. a generator breakdown unpowers it («[ОБЕСТОЧЕНА]»), a generator repair
// powers it again; 3. a broken turret offers «🔧 Починить» and the engineer repairs it in 2 s; 4. the commander picks
// the turret up; 5. F sets it up again from the supply.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/InteractableActor.h"
#include "Interactables/RelocationSubsystem.h"
#include "Interactables/TurretActor.h"
#include "TimerManager.h"

namespace TurretSmoke
{
	constexpr float NavWarmupSeconds = 3.f;
	constexpr float StepSeconds = 0.25f;
	constexpr float Timeout = 50.f;

	enum class EPhase : uint8 { Shooting, Power, Repair, PickUp, Deploy, Done };

	struct FState
	{
		EPhase Phase = EPhase::Shooting;
		float Time = 0.f;
		float PhaseTime = 0.f;
		TWeakObjectPtr<ATurretActor> Turret;
		TWeakObjectPtr<AActor> Enemy;
		float EnemyHealthBefore = 0.f;
		FVector DeploySpot = FVector::ZeroVector;
		bool bShoots = false;
		bool bPower = false;
		bool bRepair = false;
		bool bPickUp = false;
		bool bDeploy = false;
	};

	AOperativeCharacter* FindRole(UWorld* World, EOperativeRole Role)
	{
		for (AOperativeCharacter* Member : World->GetSubsystem<USquadSubsystem>()->GetMembers())
		{
			if (Member->SquadRole == Role)
			{
				return Member;
			}
		}
		return nullptr;
	}

	FVector Feet(const AOperativeCharacter* Operative)
	{
		return Operative->GetActorLocation() - FVector(0.f, 0.f, Operative->GetSimpleCollisionHalfHeight());
	}

	void Enter(FState& State, EPhase Phase)
	{
		State.Phase = Phase;
		State.PhaseTime = 0.f;
	}

	void Finish(FState& State)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke shoots=%d power=%d repair=%d pickUp=%d deploy=%d"), State.bShoots ? 1 : 0,
			State.bPower ? 1 : 0, State.bRepair ? 1 : 0, State.bPickUp ? 1 : 0, State.bDeploy ? 1 : 0);
		const bool bPass = State.bShoots && State.bPower && State.bRepair && State.bPickUp && State.bDeploy;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bPass ? TEXT("PASS") : TEXT("FAIL"));
		State.Phase = EPhase::Done;
		FPlatformMisc::RequestExit(false, TEXT("TurretSmoke"));
	}

	/** Returns false when finished. */
	bool Step(UWorld* World, FState& State)
	{
		State.Time += StepSeconds;
		State.PhaseTime += StepSeconds;
		AOperativeCharacter* Commander = FindRole(World, EOperativeRole::Commander);
		AOperativeCharacter* Engineer = FindRole(World, EOperativeRole::Engineer);
		ATurretActor* Turret = State.Turret.Get();
		if (!Commander || !Engineer || State.Time > Timeout)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in phase %d at %.1fs"), static_cast<int32>(State.Phase), State.Time);
			Finish(State);
			return false;
		}

		switch (State.Phase)
		{
		case EPhase::Shooting:
			if (State.PhaseTime >= 2.f && Turret)
			{
				const UHealthComponent* EnemyHealth = State.Enemy.IsValid() ? State.Enemy->FindComponentByClass<UHealthComponent>() : nullptr;
				const float Now = EnemyHealth ? EnemyHealth->GetCurrentHealth() : 0.f;
				State.bShoots = !State.Enemy.IsValid() || Now < State.EnemyHealthBefore;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke enemy health %.0f -> %.0f"), State.EnemyHealthBefore, Now);
				if (State.Enemy.IsValid())
				{
					State.Enemy->Destroy();
				}
				// Generator breakdown cuts the power, repair restores it.
				AInteractableActor* Generator = nullptr;
				for (TActorIterator<AInteractableActor> It(World); It; ++It)
				{
					if (It->ObjectType == EInteractableType::Generator)
					{
						Generator = *It;
					}
				}
				if (Generator)
				{
					Generator->TakeGeneratorDamage(250.f);
					const FActionMenuRequest Off = Turret->BuildActionMenu(Commander);
					const bool bOff = !Turret->IsPowered() && Generator->bGeneratorBroken && Off.Menu.bConfirmDisabled;
					UE_LOG(LogCodexTactics, Display, TEXT("Smoke unpowered menu \"%s\" / \"%s\" / \"%s\""), *Off.Menu.Title.ToString(),
						*Off.Menu.ConfirmText.ToString(), *Off.Menu.CancelText.ToString());
					const FActionMenuRequest GeneratorMenu = Generator->BuildActionMenu(Engineer);
					UE_LOG(LogCodexTactics, Display, TEXT("Smoke generator menu \"%s\" / \"%s\""), *GeneratorMenu.Menu.Title.ToString(),
						*GeneratorMenu.Menu.ConfirmText.ToString());
					Generator->RepairGenerator();
					State.bPower = bOff && Turret->IsPowered() && !Generator->bGeneratorBroken;
				}
				// Break the turret; the engineer repairs it.
				Turret->Health->ApplyDirectHealthLoss(500.f, TEXT("Smoke"));
				const FActionMenuRequest Broken = Turret->BuildActionMenu(Engineer);
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke broken menu \"%s\" / \"%s\" broken=%d"), *Broken.Menu.Title.ToString(),
					*Broken.Menu.ConfirmText.ToString(), Turret->IsBroken() ? 1 : 0);
				Turret->ExecuteAction(Engineer);
				Enter(State, EPhase::Repair);
			}
			return true;

		case EPhase::Repair:
			if (State.PhaseTime >= 2.5f && Turret)
			{
				State.bRepair = !Turret->IsBroken() && Turret->Health->GetCurrentHealth() >= Turret->Health->GetMaxHealth();
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke repaired=%d hp=%.0f"), State.bRepair ? 1 : 0, Turret->Health->GetCurrentHealth());
				const FActionMenuRequest PickUp = Turret->BuildActionMenu(Commander);
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke pick-up menu \"%s\" / \"%s\""), *PickUp.Menu.Title.ToString(), *PickUp.Menu.ConfirmText.ToString());
				Turret->ExecuteAction(Commander);
				Enter(State, EPhase::PickUp);
			}
			return true;

		case EPhase::PickUp:
			if (State.PhaseTime >= 1.5f)
			{
				State.bPickUp = !State.Turret.IsValid() && Commander->TurretsCount == 1;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke picked up: turret gone=%d commander turrets=%d"), State.Turret.IsValid() ? 0 : 1,
					Commander->TurretsCount);
				URelocationSubsystem* Relocation = World->GetSubsystem<URelocationSubsystem>();
				Commander->SelectedDeployType = EDeployableType::Turret;
				Relocation->HandleDeployKey(Commander);
				State.DeploySpot = Feet(Commander) + Commander->GetActorForwardVector() * 400.f - Commander->GetActorRightVector() * 300.f;
				Relocation->UpdatePreview(State.DeploySpot);
				Relocation->ConfirmPlacement(State.DeploySpot);
				Relocation->ConfirmPlacement(State.DeploySpot);
				Enter(State, EPhase::Deploy);
			}
			return true;

		case EPhase::Deploy:
			if (World->GetSubsystem<URelocationSubsystem>()->GetActiveDeployCount() == 0 && State.PhaseTime > 0.5f)
			{
				ATurretActor* Placed = nullptr;
				for (TActorIterator<ATurretActor> It(World); It; ++It)
				{
					if (FVector::Dist2D(It->GetActorLocation(), State.DeploySpot) < 30.f)
					{
						Placed = *It;
					}
				}
				State.bDeploy = Placed && Commander->TurretsCount == 0 && Placed->IsPowered();
				Finish(State);
				return false;
			}
			return true;

		default:
			return false;
		}
	}

	void Start(UWorld* World)
	{
		AOperativeCharacter* Commander = FindRole(World, EOperativeRole::Commander);
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		if (!Commander || !Waves)
		{
			FState Failed;
			Finish(Failed);
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector TurretSpot = Feet(Commander) + Commander->GetActorForwardVector() * 300.f + FVector(0.f, 0.f, 40.f);
		State->Turret = World->SpawnActor<ATurretActor>(TurretSpot, FRotator::ZeroRotator, Params);
		AEnemyCharacter* Enemy = Waves->SpawnEnemy(EEnemyArchetype::Brute, TurretSpot + Commander->GetActorForwardVector() * 800.f + FVector(0.f, 0.f, 60.f));
		State->Enemy = Enemy;
		const UHealthComponent* EnemyHealth = Enemy ? Enemy->FindComponentByClass<UHealthComponent>() : nullptr;
		State->EnemyHealthBefore = EnemyHealth ? EnemyHealth->GetCurrentHealth() : 0.f;

		TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
		TWeakObjectPtr<UWorld> WeakWorld(World);
		World->GetTimerManager().SetTimer(*Handle, FTimerDelegate::CreateLambda([WeakWorld, State, Handle]()
		{
			UWorld* W = WeakWorld.Get();
			if (W && !Step(W, *State))
			{
				W->GetTimerManager().ClearTimer(*Handle);
			}
		}), StepSeconds, true);
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
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld]()
		{
			if (UWorld* W = WeakWorld.Get())
			{
				Start(W);
			}
		}), NavWarmupSeconds, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.TurretSmoke"),
		TEXT("Dev check: turret fire, generator power, repair, pick-up and set-up; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
