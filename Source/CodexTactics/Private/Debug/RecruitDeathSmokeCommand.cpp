// Dev-only console command for a headless check of an expendable member's death on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.RecruitDeathSmoke
// Godot is_expendable (recruit_susanin.gd) / main.gd _handle_expendable_member_death / player.gd corpse_loot: Susanin's
// death does not fail the mission; his remains («Останки: Иван Сусанин») hold his supplies and the leader takes them.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/RecruitSubsystem.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Core/MissionSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/LootCrateActor.h"
#include "TimerManager.h"

namespace RecruitDeathSmoke
{
	void Check(bool& bOk, bool bCondition, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bCondition ? TEXT("ok  ") : TEXT("FAIL"), *What);
		bOk &= bCondition;
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
			UWorld* W = WeakWorld.Get();
			bool bOk = true;
			USquadSubsystem* Squad = W ? W->GetSubsystem<USquadSubsystem>() : nullptr;
			URecruitSubsystem* Recruits = W ? W->GetSubsystem<URecruitSubsystem>() : nullptr;
			AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
			AOperativeCharacter* Susanin = Recruits ? Recruits->GetOrSpawnSusanin() : nullptr;
			Check(bOk, Leader && Susanin, TEXT("leader and Susanin present"));
			if (Leader && Susanin)
			{
				Check(bOk, Susanin->IsExpendable(), TEXT("the recruit is expendable"));
				Susanin->SetActorLocation(Leader->GetActorLocation() + Leader->GetActorForwardVector() * 300.f);
				Susanin->MedkitsCount = 2;
				Susanin->ChocolateCount = 3;
				const int32 MedkitsBefore = Leader->MedkitsCount;
				const int32 ChocolateBefore = Leader->ChocolateCount;
				FDamageSpec Spec;
				Spec.Amount = 100000.f;
				Spec.AttackerSource = TEXT("smoke");
				Susanin->HealthComponent->TakeDamage(Spec);
				const UMissionSubsystem* Mission = W->GetSubsystem<UMissionSubsystem>();
				Check(bOk, Mission && !Mission->IsMissionFailed(), TEXT("his death does not fail the mission"));
				Check(bOk, Squad->GetLeader() == Leader, TEXT("the leader stays"));
				ALootCrateActor* Remains = nullptr;
				for (TActorIterator<ALootCrateActor> It(W); It; ++It)
				{
					Remains = It->ActorHasTag(TEXT("CorpseLoot")) ? *It : Remains;
				}
				Check(bOk, Remains && Remains->CrateName.ToString().Contains(TEXT("Останки")),
					FString::Printf(TEXT("remains spawned: %s"), Remains ? *Remains->CrateName.ToString() : TEXT("none")));
				if (Remains)
				{
					Remains->TakeAll(Leader);
					Check(bOk, Leader->MedkitsCount == MedkitsBefore + 2 && Leader->ChocolateCount == ChocolateBefore + 3 && Remains->IsLooted(),
						FString::Printf(TEXT("the leader took his supplies (medkits %d -> %d, chocolate %d -> %d)"), MedkitsBefore,
							Leader->MedkitsCount, ChocolateBefore, Leader->ChocolateCount));
				}
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"));
			FPlatformMisc::RequestExit(false, TEXT("RecruitDeathSmoke"));
		}), 3.f, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(TEXT("CodexTactics.RecruitDeathSmoke"),
		TEXT("Dev check: Susanin's death keeps the mission, his remains can be searched; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
