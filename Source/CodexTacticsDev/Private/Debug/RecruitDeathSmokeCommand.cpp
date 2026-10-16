// Dev-only console command for a headless check of an expendable member's death on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.RecruitDeathSmoke
// Godot is_expendable (recruit_susanin.gd) / main.gd _handle_expendable_member_death / player.gd corpse_loot: Susanin's
// death does not fail the mission; his remains («Remains: Иван Сусанин») hold his supplies and the leader takes them.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/RecruitSubsystem.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/DeathCinematicSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/KnockdownComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
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
				// The user's bug 2026-10-08 happened with Susanin in the squad: recruit him first.
				Recruits->RestoreRecruited(true);
				Check(bOk, Squad->GetMembers().Contains(Susanin), TEXT("Susanin recruited into the squad"));
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
				Check(bOk, Remains && Remains->CrateName.ToString().Contains(TEXT("Remains")),
					FString::Printf(TEXT("remains spawned: %s"), Remains ? *Remains->CrateName.ToString() : TEXT("none")));
				if (Remains)
				{
					Remains->TakeAll(Leader);
					Check(bOk, Leader->MedkitsCount == MedkitsBefore + 2 && Leader->ChocolateCount == ChocolateBefore + 3 && Remains->IsLooted(),
						FString::Printf(TEXT("the leader took his supplies (medkits %d -> %d, chocolate %d -> %d)"), MedkitsBefore,
							Leader->MedkitsCount, ChocolateBefore, Leader->ChocolateCount));
				}
			}
			if (!Susanin)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: FAIL"));
				FPlatformMisc::RequestExit(false, TEXT("RecruitDeathSmoke"));
				return;
			}
			// User bug 2026-10-08: he kept standing. A few seconds later (after the death cinematic) he must lie as a corpse.
			TWeakObjectPtr<AOperativeCharacter> WeakSusanin(Susanin);
			FTimerHandle CorpseHandle;
			W->GetTimerManager().SetTimer(CorpseHandle, FTimerDelegate::CreateLambda([WeakWorld, WeakSusanin, bOk]() mutable
			{
				const AOperativeCharacter* Dead = WeakSusanin.Get();
				UWorld* W2 = WeakWorld.Get();
				const UDeathCinematicSubsystem* DeathCam = W2 ? W2->GetSubsystem<UDeathCinematicSubsystem>() : nullptr;
				Check(bOk, DeathCam && !DeathCam->IsActive() && DeathCam->GetCompletedFocusCount() >= 1, TEXT("death cinematic played and ended"));
				const UAnimInstance* Anim = Dead && Dead->GetMesh() ? Dead->GetMesh()->GetAnimInstance() : nullptr;
				const UOperativeAnimInstance* OperativeAnim = Cast<UOperativeAnimInstance>(Anim);
				const float FullBody = OperativeAnim ? OperativeAnim->GetSlotMontageGlobalWeight(OperativeAnim->FullBodySlot) : 0.f;
				const UKnockdownComponent* Knockdown = Dead ? Dead->KnockdownComponent.Get() : nullptr;
				// Since 2026-10-09 ABP_Operative has standing death clips (Sniper pack); the knockdown fall stays the fallback. Either
				// way the held pose must be a body on the ground (pelvis near the feet).
				// (Headless runs do not refresh bone poses, so the check reads the montage: played to its end and held.)
				const UAnimMontage* Held = OperativeAnim ? OperativeAnim->GetCurrentActiveMontage() : nullptr;
				const float HeldEnd = Held && OperativeAnim->Montage_GetPosition(Held) >= Held->GetPlayLength() - 0.05f ? 1.f : 0.f;
				Check(bOk, FullBody > 0.9f && Knockdown && (HeldEnd > 0.f || Knockdown->PlayedDeathFall() || Knockdown->DiedWhileDown()),
					FString::Printf(TEXT("Susanin lies as a corpse: death pose held on FullBody (weight %.2f, held at the end %.0f, fall clip %s)"), FullBody, HeldEnd,
						Knockdown && Knockdown->GetPlayingClip() ? *Knockdown->GetPlayingClip()->GetName() : TEXT("none")));
				Check(bOk, Dead && Dead->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Ignore
					&& Dead->GetCharacterMovement()->MovementMode == MOVE_None, TEXT("no pawn collision, no movement"));
				const USquadSubsystem* Squad2 = W2 ? W2->GetSubsystem<USquadSubsystem>() : nullptr;
				Check(bOk, Dead && Squad2 && !Squad2->GetMembers().Contains(Dead) && Dead->IsKilledInAction(), TEXT("out of the squad (HUD / key 4 / turn order)"));
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"));
				FPlatformMisc::RequestExit(false, TEXT("RecruitDeathSmoke"));
			}), 4.f, false);
		}), 3.f, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(TEXT("CodexTactics.RecruitDeathSmoke"),
		TEXT("Dev check: Susanin's death keeps the mission, his remains can be searched; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
