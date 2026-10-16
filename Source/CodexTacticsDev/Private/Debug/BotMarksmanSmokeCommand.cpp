// Dev-only console command for a headless check of the playtest bot's answer to a marksman (Sprint 05-D) on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.BotMarksmanSmoke
// A marksman 25 m ahead of the (unkillable, cease-fire) squad starts its telegraphed aim; the bot's ReactToMarksman then
// crouches the standing operatives (and sends the leader to a barricade within 20 m when there is one).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Bot/PlaytestBotSubsystem.h"
#include "Characters/MarksmanEnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

namespace BotMarksmanSmoke
{
	struct FState
	{
		float Time = 0.f;
		bool bReacted = false;
		TWeakObjectPtr<AMarksmanEnemyCharacter> Marksman;
	};

	bool Finish(bool bPass)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bPass ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("BotMarksmanSmoke"));
		return false;
	}

	bool Step(UWorld* World, FState& State)
	{
		State.Time += 0.1f;
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		UPlaytestBotSubsystem* Bot = World->GetSubsystem<UPlaytestBotSubsystem>();
		if (!Leader || !Bot)
		{
			return Finish(false);
		}
		AMarksmanEnemyCharacter* Marksman = State.Marksman.Get();
		if (!Marksman)
		{
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->HealthComponent->SetMaxHealth(100000.f);
				Member->bTacticalCeaseFire = true;
				Member->SetStance(EOperativeStance::Standing);
			}
			Marksman = Cast<AMarksmanEnemyCharacter>(World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Marksman,
				Leader->GetActorLocation() + Leader->GetActorForwardVector() * 2500.f + FVector(0.f, 0.f, 20.f)));
			State.Marksman = Marksman;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: marksman spawned"), Marksman ? TEXT("ok  ") : TEXT("FAIL"));
			return Marksman ? true : Finish(false);
		}
		const bool bNoAim = Bot->ReactToMarksman();
		if (!Marksman->IsAimingAtTarget())
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: no reaction while the marksman does not aim"), bNoAim ? TEXT("FAIL") : TEXT("ok  "));
			if (bNoAim || State.Time > 12.f)
			{
				return Finish(false);
			}
			return true;
		}
		// Aiming now: the bot already reacted this tick (ReactToMarksman above).
		bool bAllCrouched = true;
		for (const AOperativeCharacter* Member : Squad->GetMembers())
		{
			bAllCrouched &= !Member->HealthComponent->IsAlive() || Member->IsMoving() || Member->GetStance() != EOperativeStance::Standing;
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: the marksman aims (%.0f%%) -> the bot reacted %d, squad crouched %d (reactions %d)"),
			bNoAim && bAllCrouched ? TEXT("ok  ") : TEXT("FAIL"), Marksman->GetAimProgress() * 100.f, bNoAim ? 1 : 0, bAllCrouched ? 1 : 0,
			Bot->GetMarksmanReactions());
		return Finish(bNoAim && bAllCrouched && Bot->GetMarksmanReactions() == 1);
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

	static FAutoConsoleCommandWithWorldAndArgs Command(TEXT("CodexTactics.BotMarksmanSmoke"),
		TEXT("Dev check: the playtest bot crouches the squad when a marksman aims at it; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
