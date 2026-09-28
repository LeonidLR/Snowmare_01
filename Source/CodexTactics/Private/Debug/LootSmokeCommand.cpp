// Dev-only console command for a headless supply crate check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.LootSmoke
// 1. an intact crate opens without a menu and shows the loot dialog; one stack, then «Забрать ВСЁ» go to the leader;
// 2. the empty crate only posts the Godot line; 3. a trapped crate (medic-sapper, prone) is defused, opened and its
// turret / barricades / mines are taken; 4. a trapped crate that blows up loses its contents.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/InteractionSubsystem.h"
#include "Interactables/LootCrateActor.h"
#include "TimerManager.h"

namespace LootSmoke
{
	constexpr float NavWarmupSeconds = 3.f;
	constexpr float StepSeconds = 0.25f;
	constexpr float Timeout = 60.f;

	enum class EPhase : uint8 { OpenCrate, EmptyClick, TrappedMenu, TrappedOpen, Blast, Done };

	struct FState
	{
		EPhase Phase = EPhase::OpenCrate;
		float Time = 0.f;
		float PhaseTime = 0.f;
		TWeakObjectPtr<ALootCrateActor> Crate;
		TWeakObjectPtr<ALootCrateActor> Trapped;
		int32 MedkitsBefore = 0;
		int32 AmmoBefore = 0;
		bool bOpen = false;
		bool bTake = false;
		bool bEmpty = false;
		bool bDefuse = false;
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

	ALootCrateActor* SpawnCrate(UWorld* World, const FVector& Ground, bool bTrapped)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ALootCrateActor* Crate = World->SpawnActor<ALootCrateActor>(Ground + FVector(0.f, 0.f, 40.f), FRotator::ZeroRotator, Params);
		if (Crate && bTrapped)
		{
			Crate->CrateName = FText::FromString(TEXT("📦 Заминированный ящик аванпоста"));
			Crate->bTrapped = true;
			Crate->Contents.Turrets = 1;
			Crate->Contents.Barricades = 2;
			Crate->Contents.Mines = 2;
		}
		return Crate;
	}

	FVector Feet(const AOperativeCharacter* Operative)
	{
		return Operative->GetActorLocation() - FVector(0.f, 0.f, Operative->GetSimpleCollisionHalfHeight());
	}

	void Finish(FState& State)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke open=%d take=%d empty=%d defuse=%d blast=%d"), State.bOpen ? 1 : 0, State.bTake ? 1 : 0,
			State.bEmpty ? 1 : 0, State.bDefuse ? 1 : 0, State.bBlast ? 1 : 0);
		const bool bPass = State.bOpen && State.bTake && State.bEmpty && State.bDefuse && State.bBlast;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bPass ? TEXT("PASS") : TEXT("FAIL"));
		State.Phase = EPhase::Done;
		FPlatformMisc::RequestExit(false, TEXT("LootSmoke"));
	}

	void Enter(FState& State, EPhase Phase)
	{
		State.Phase = Phase;
		State.PhaseTime = 0.f;
	}

	/** Returns false when finished. */
	bool Step(UWorld* World, FState& State)
	{
		State.Time += StepSeconds;
		State.PhaseTime += StepSeconds;
		UInteractionSubsystem* Interactions = World->GetSubsystem<UInteractionSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Commander = FindRole(World, EOperativeRole::Commander);
		AOperativeCharacter* Medic = FindRole(World, EOperativeRole::MedicSapper);
		if (!Commander || !Medic || State.Time > Timeout)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in phase %d at %.1fs"), static_cast<int32>(State.Phase), State.Time);
			Finish(State);
			return false;
		}

		switch (State.Phase)
		{
		case EPhase::OpenCrate:
			if (Interactions->IsActionMenuOpen())
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke unexpected menu on an intact crate"));
				Interactions->CancelActionMenu();
			}
			if (Interactions->GetLootCrate() == State.Crate.Get() && State.Crate.IsValid())
			{
				State.bOpen = true;
				Interactions->LootItem(ELootItem::Medkit);
				const bool bOne = Commander->MedkitsCount == State.MedkitsBefore + 2 && State.Crate->Contents.Medkits == 0;
				Interactions->LootAll();
				State.bTake = bOne && Commander->ReserveAmmo == State.AmmoBefore + 60 && State.Crate->IsLooted()
					&& !Interactions->GetLootCrate();
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke looted: medkits %d -> %d, reserve %d -> %d, looted=%d"), State.MedkitsBefore,
					Commander->MedkitsCount, State.AmmoBefore, Commander->ReserveAmmo, State.Crate->IsLooted() ? 1 : 0);
				Interactions->RequestInteraction(State.Crate.Get());
				Enter(State, EPhase::EmptyClick);
			}
			return true;

		case EPhase::EmptyClick:
			State.bEmpty = !Interactions->IsActionMenuOpen() && !Interactions->GetLootCrate();
			// The medic-sapper leads to the trapped crate, the others hold.
			Squad->SetLeader(Medic);
			Squad->EnterSoloMode();
			State.Trapped = SpawnCrate(World, Feet(Medic) + Medic->GetActorForwardVector() * 300.f, true);
			Interactions->RequestInteraction(State.Trapped.Get());
			Enter(State, EPhase::TrappedMenu);
			return true;

		case EPhase::TrappedMenu:
			if (Interactions->IsActionMenuOpen())
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke trapped menu \"%s\" / \"%s\""), *Interactions->GetActionMenu().Title.ToString(),
					*Interactions->GetActionMenu().ConfirmText.ToString());
				// The defuser crouches for the work (Godot): luck 100 keeps the odds at 99 %, failures are retried.
				Medic->Luck = 100.f;
				++State.DefuseTries;
				Medic->SetStance(EOperativeStance::Prone);
				Interactions->ConfirmActionMenu();
				Enter(State, EPhase::TrappedOpen);
			}
			return true;

		case EPhase::TrappedOpen:
			if (State.Trapped.IsValid() && Interactions->GetLootCrate() == State.Trapped.Get())
			{
				Interactions->LootAll();
				State.bDefuse = !State.Trapped->bTrapped && Medic->TurretsCount == 1 && Medic->BarricadesCount == 2 && Medic->MinesCount == 2;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke defused crate: turrets=%d barricades=%d mines=%d"), Medic->TurretsCount,
					Medic->BarricadesCount, Medic->MinesCount);
				Enter(State, EPhase::Blast);
			}
			else if (State.PhaseTime > 3.f && State.Trapped.IsValid() && State.Trapped->bTrapped && !State.Trapped->IsDestroyed()
				&& State.DefuseTries < 4)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke crate defusal attempt %d failed, retrying"), State.DefuseTries);
				Interactions->RequestInteraction(State.Trapped.Get());
				Enter(State, EPhase::TrappedMenu);
			}
			else if (State.PhaseTime > 8.f)
			{
				Enter(State, EPhase::Blast);
			}
			return true;

		case EPhase::Blast:
		{
			ALootCrateActor* Doomed = SpawnCrate(World, Feet(Commander) + Commander->GetActorRightVector() * 600.f, true);
			Doomed->DetonateTrap();
			State.bBlast = Doomed->IsDestroyed() && Doomed->GetItems().IsEmpty();
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
		State->Crate = SpawnCrate(World, Feet(Commander) + Commander->GetActorForwardVector() * 400.f, false);
		State->Crate->Contents.Medkits = 2;
		State->MedkitsBefore = Commander->MedkitsCount;
		State->AmmoBefore = Commander->ReserveAmmo;
		World->GetSubsystem<UInteractionSubsystem>()->RequestInteraction(State->Crate.Get());

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
		TEXT("CodexTactics.LootSmoke"),
		TEXT("Dev check: supply crate opening and loot dialog, empty crate, trapped crate defusal and loot, detonation; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
