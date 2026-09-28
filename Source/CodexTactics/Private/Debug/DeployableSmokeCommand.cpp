// Dev-only console command for a headless deployables check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.DeployableSmoke
// 1. pick up an abandoned barricade (goes to the engineer), 2. F: the engineer hands it to the commander, who sets it
// up (two clicks), 3. a hidden mine next to the squad is spotted, 4. the medic-sapper defuses it lying down and
// takes it, 5. a barrel is trapped with a grenade, 6. a mine blast hurts an operative nearby.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/BarricadeActor.h"
#include "Interactables/InteractionSubsystem.h"
#include "Interactables/ProximityMineActor.h"
#include "Interactables/RelocationSubsystem.h"
#include "TimerManager.h"

namespace DeployableSmoke
{
	constexpr float NavWarmupSeconds = 3.f;
	constexpr float StepSeconds = 0.25f;
	constexpr float Timeout = 70.f;

	enum class EPhase : uint8 { PickUpMenu, PickUpWait, DeployWait, MineSpot, DefuseMenu, DefuseWait, Trap, Blast, Done };

	struct FState
	{
		EPhase Phase = EPhase::PickUpMenu;
		float Time = 0.f;
		float PhaseTime = 0.f;
		TWeakObjectPtr<ABarricadeActor> Barricade;
		TWeakObjectPtr<AProximityMineActor> Mine;
		FVector DeploySpot = FVector::ZeroVector;
		bool bPickUp = false;
		bool bDeploy = false;
		bool bSpotted = false;
		bool bDefused = false;
		bool bTrap = false;
		bool bBlast = false;
		int32 DefuseTries = 0;
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

	template <typename T>
	T* SpawnAt(UWorld* World, const FVector& Ground)
	{
		const float HalfHeight = GetDefault<T>()->Box->GetUnscaledBoxExtent().Z;
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<T>(Ground + FVector(0.f, 0.f, HalfHeight), FRotator::ZeroRotator, Params);
	}

	void Enter(FState& State, EPhase Phase)
	{
		State.Phase = Phase;
		State.PhaseTime = 0.f;
	}

	void Finish(FState& State)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke pickUp=%d deploy=%d spotted=%d defused=%d trap=%d blast=%d"), State.bPickUp ? 1 : 0,
			State.bDeploy ? 1 : 0, State.bSpotted ? 1 : 0, State.bDefused ? 1 : 0, State.bTrap ? 1 : 0, State.bBlast ? 1 : 0);
		const bool bPass = State.bPickUp && State.bDeploy && State.bSpotted && State.bDefused && State.bTrap && State.bBlast;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bPass ? TEXT("PASS") : TEXT("FAIL"));
		State.Phase = EPhase::Done;
		FPlatformMisc::RequestExit(false, TEXT("DeployableSmoke"));
	}

	FVector Feet(const AOperativeCharacter* Operative)
	{
		return Operative->GetActorLocation() - FVector(0.f, 0.f, Operative->GetSimpleCollisionHalfHeight());
	}

	/** Returns false when finished. */
	bool Step(UWorld* World, FState& State)
	{
		State.Time += StepSeconds;
		State.PhaseTime += StepSeconds;
		UInteractionSubsystem* Interactions = World->GetSubsystem<UInteractionSubsystem>();
		URelocationSubsystem* Relocation = World->GetSubsystem<URelocationSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Commander = FindRole(World, EOperativeRole::Commander);
		AOperativeCharacter* Engineer = FindRole(World, EOperativeRole::Engineer);
		AOperativeCharacter* Medic = FindRole(World, EOperativeRole::MedicSapper);
		if (!Commander || !Engineer || !Medic || State.Time > Timeout)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in phase %d at %.1fs"), static_cast<int32>(State.Phase), State.Time);
			Finish(State);
			return false;
		}

		switch (State.Phase)
		{
		case EPhase::PickUpMenu:
			if (Interactions->IsActionMenuOpen())
			{
				const FActionMenuSpec& Menu = Interactions->GetActionMenu();
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke menu \"%s\" / \"%s\" trap=\"%s\""), *Menu.Title.ToString(),
					*Menu.ConfirmText.ToString(), *Menu.TrapText.ToString());
				Interactions->ConfirmActionMenu();
				Enter(State, EPhase::PickUpWait);
			}
			return true;

		case EPhase::PickUpWait:
			if (State.PhaseTime >= 2.f)
			{
				State.bPickUp = !State.Barricade.IsValid() && Engineer->BarricadesCount == 1;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke picked up: barricade gone=%d engineer=%d commander=%d"),
					State.Barricade.IsValid() ? 0 : 1, Engineer->BarricadesCount, Commander->BarricadesCount);
				// F with an empty commander: the engineer hands the barricade over; two clicks set it up.
				Relocation->HandleDeployKey(Commander);
				State.DeploySpot = Feet(Commander) + Commander->GetActorForwardVector() * 500.f + Commander->GetActorRightVector() * 300.f;
				Relocation->UpdatePreview(State.DeploySpot);
				Relocation->ConfirmPlacement(State.DeploySpot);
				Relocation->RotatePreview(+2);
				Relocation->ConfirmPlacement(State.DeploySpot);
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke deploy ordered: commander=%d engineer=%d tasks=%d"), Commander->BarricadesCount,
					Engineer->BarricadesCount, Relocation->GetActiveDeployCount());
				Enter(State, EPhase::DeployWait);
			}
			return true;

		case EPhase::DeployWait:
			if (Relocation->GetActiveDeployCount() == 0)
			{
				ABarricadeActor* Placed = nullptr;
				for (TActorIterator<ABarricadeActor> It(World); It; ++It)
				{
					if (FVector::Dist2D(It->GetActorLocation(), State.DeploySpot) < 30.f)
					{
						Placed = *It;
					}
				}
				State.bDeploy = Placed && Commander->BarricadesCount == 0 && FMath::IsNearlyEqual(FMath::Abs(Placed->GetActorRotation().Yaw), 90.f, 1.f);
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke deployed at %.1fs: found=%d yaw=%.0f commander=%d"), State.Time, Placed ? 1 : 0,
					Placed ? Placed->GetActorRotation().Yaw : 0.f, Commander->BarricadesCount);
				// A hidden mine 3 m to the medic's side, the rest of the squad holds position.
				Squad->SetLeader(Medic);
				Squad->EnterSoloMode();
				State.Mine = SpawnAt<AProximityMineActor>(World, Feet(Medic) + Medic->GetActorRightVector() * 380.f);
				Enter(State, EPhase::MineSpot);
			}
			return true;

		case EPhase::MineSpot:
			if (State.Mine.IsValid() && State.Mine->IsRevealed())
			{
				State.bSpotted = true;
				Interactions->RequestInteraction(State.Mine.Get());
				Enter(State, EPhase::DefuseMenu);
			}
			else if (State.PhaseTime > 3.f)
			{
				Enter(State, EPhase::DefuseMenu);
			}
			return true;

		case EPhase::DefuseMenu:
			if (Interactions->IsActionMenuOpen())
			{
				// Dismantling crouches the defuser (Godot): luck 100 keeps the odds at 99 %, failures are retried.
				Medic->Luck = 100.f;
				++State.DefuseTries;
				Medic->SetStance(EOperativeStance::Prone);
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke mine menu \"%s\" / \"%s\""), *Interactions->GetActionMenu().Title.ToString(),
					*Interactions->GetActionMenu().ConfirmText.ToString());
				Interactions->ConfirmActionMenu();
				Enter(State, EPhase::DefuseWait);
			}
			else if (!State.Mine.IsValid())
			{
				Enter(State, EPhase::Trap); // blew up on the way
			}
			return true;

		case EPhase::DefuseWait:
			if (State.PhaseTime >= 2.f && State.Mine.IsValid() && State.DefuseTries < 4)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke defusal attempt %d did not clear the mine, retrying"), State.DefuseTries);
				Interactions->RequestInteraction(State.Mine.Get());
				Enter(State, EPhase::DefuseMenu);
			}
			else if (State.PhaseTime >= 2.f)
			{
				State.bDefused = !State.Mine.IsValid() && Medic->MinesCount == 1 && Medic->HealthComponent->GetCurrentHealth() >= Medic->HealthComponent->GetMaxHealth();
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke defused: mine gone=%d medic mines=%d hp=%.0f"), State.Mine.IsValid() ? 0 : 1,
					Medic->MinesCount, Medic->HealthComponent->GetCurrentHealth());
				Enter(State, EPhase::Trap);
			}
			return true;

		case EPhase::Trap:
		{
			ABarrelActor* Barrel = SpawnAt<ABarrelActor>(World, Feet(Medic) - Medic->GetActorForwardVector() * 250.f);
			const int32 Grenades = Medic->GrenadesCount;
			State.bTrap = Barrel && Barrel->CanReceiveTrap() && Barrel->TrapWithGrenade(Medic) && Barrel->bTrapped
				&& Medic->GrenadesCount == Grenades - 1 && !Barrel->CanReceiveTrap();
			// A level mine going off 2.5 m from the engineer.
			const float Before = Engineer->HealthComponent->GetCurrentHealth();
			AProximityMineActor* Blast = SpawnAt<AProximityMineActor>(World, Feet(Engineer) + Engineer->GetActorRightVector() * 250.f);
			if (Blast)
			{
				Blast->Detonate();
			}
			const float Lost = Before - Engineer->HealthComponent->GetCurrentHealth();
			// 120 * 0.75 * (1 - 250 / 350 * 0.5) = 57.9
			State.bBlast = FMath::IsNearlyEqual(Lost, 57.86f, 1.5f);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke trap=%d grenades %d -> %d, blast damage %.1f"), State.bTrap ? 1 : 0, Grenades,
				Medic->GrenadesCount, Lost);
			Finish(State);
			return false;
		}

		default:
			return false;
		}
	}

	void Start(UWorld* World)
	{
		AOperativeCharacter* Commander = FindRole(World, EOperativeRole::Commander);
		if (!Commander)
		{
			FState Failed;
			Finish(Failed);
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		State->Barricade = SpawnAt<ABarricadeActor>(World, Feet(Commander) + Commander->GetActorForwardVector() * 450.f);
		World->GetSubsystem<UInteractionSubsystem>()->RequestInteraction(State->Barricade.Get());

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
		TEXT("CodexTactics.DeployableSmoke"),
		TEXT("Dev check: barricade pick-up and set-up, mine spotting and defusal, grenade trap, mine blast; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
