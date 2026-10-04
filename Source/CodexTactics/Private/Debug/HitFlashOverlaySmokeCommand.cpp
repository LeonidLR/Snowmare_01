// Dev-only console command for a headless check of overlapping hit flashes on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.HitFlashOverlaySmoke
// Found by the playtest bot (GC stress runs): a second hit flash on an enemy remembered the first flash's glow as the
// mesh's overlay and put it back after the first flash actor (and its glow) was gone — the mesh then pointed at a freed
// material and the garbage collector crashed ("Invalid object in GC ... MemberId OverlayMaterial"). Two overlapping flashes,
// then a full GC: the mesh must be back on its own overlay.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Components/MeshComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "UObject/UObjectGlobals.h"

namespace HitFlashOverlaySmoke
{
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
			USquadSubsystem* Squad = W ? W->GetSubsystem<USquadSubsystem>() : nullptr;
			AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
			AEnemyCharacter* Hound = Leader ? W->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound,
				Leader->GetActorLocation() + Leader->GetActorForwardVector() * 1500.f + FVector(0.f, 0.f, 20.f)) : nullptr;
			UCombatFeedbackSubsystem* Feedback = W ? W->GetSubsystem<UCombatFeedbackSubsystem>() : nullptr;
			if (!Hound || !Feedback)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke FAIL: no hound / feedback"));
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: FAIL"));
				FPlatformMisc::RequestExit(false, TEXT("HitFlashOverlaySmoke"));
				return;
			}
			Hound->SetActorTickEnabled(false);
			TArray<UMeshComponent*> Meshes;
			Hound->GetComponents(Meshes);
			TArray<TWeakObjectPtr<UMeshComponent>> Visible;
			TArray<TWeakObjectPtr<UMaterialInterface>> Original;
			for (UMeshComponent* Mesh : Meshes)
			{
				if (Mesh->IsVisible())
				{
					Visible.Add(Mesh);
					Original.Add(Mesh->GetOverlayMaterial());
				}
			}
			Feedback->FlashEnemyHit(Hound);
			FTimerHandle Second;
			W->GetTimerManager().SetTimer(Second, FTimerDelegate::CreateLambda([WeakWorld, Hound = TWeakObjectPtr<AEnemyCharacter>(Hound), Visible, Original]()
			{
				UWorld* W2 = WeakWorld.Get();
				if (UCombatFeedbackSubsystem* Feedback2 = W2 ? W2->GetSubsystem<UCombatFeedbackSubsystem>() : nullptr)
				{
					Feedback2->FlashEnemyHit(Hound.Get()); // overlaps the first flash
				}
				FTimerHandle Check;
				W2->GetTimerManager().SetTimer(Check, FTimerDelegate::CreateLambda([Visible, Original]()
				{
					// Both flashes are over and destroyed; a full purge frees their glows.
					CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, true);
					bool bOk = !Visible.IsEmpty();
					for (int32 Index = 0; Index < Visible.Num(); ++Index)
					{
						const UMeshComponent* Mesh = Visible[Index].Get();
						const bool bRestored = Mesh && Mesh->GetOverlayMaterial() == Original[Index].Get();
						UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s overlay back on its own (%s)"), bRestored ? TEXT("ok  ") : TEXT("FAIL"),
							Mesh ? *Mesh->GetName() : TEXT("?"), Mesh && Mesh->GetOverlayMaterial() ? *Mesh->GetOverlayMaterial()->GetName() : TEXT("none"));
						bOk &= bRestored;
					}
					UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"));
					FPlatformMisc::RequestExit(false, TEXT("HitFlashOverlaySmoke"));
				}), 2.f, false);
			}), 0.05f, false);
		}), 3.f, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(TEXT("CodexTactics.HitFlashOverlaySmoke"),
		TEXT("Dev check: overlapping hit flashes restore the enemy mesh overlay (no freed glow left); logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
