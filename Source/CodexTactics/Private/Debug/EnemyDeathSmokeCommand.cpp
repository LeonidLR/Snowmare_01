// Dev-only console command for a headless check of enemy deaths on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.EnemyDeathSmoke
// Godot enemy_base.gd _die: the death clip plays and the body stays on its last frame until it is removed
// (death_decay_delay); it must never return to the idle / locomotion pose. One enemy of each type with a Blueprint
// (hound, frostbitten, cutter, brute) is killed, then hit a few more times (shots landing on the body); the
// one-shot slot weight is sampled every 0.1 s until the actor is gone.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyAnimInstance.h"
#include "Characters/EnemyCharacter.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

namespace EnemyDeathSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FTracked
	{
		TWeakObjectPtr<AEnemyCharacter> Enemy;
		EEnemyArchetype Type = EEnemyArchetype::Base;
		FString Timeline;
		float MinHeldWeight = 1.f;
		float DeadFor = 0.f;
		bool bHasClip = false;
		bool bGone = false;
	};

	struct FState
	{
		float Time = 0.f;
		bool bKilled = false;
		TArray<FTracked> Enemies;
	};

	FString Name(EEnemyArchetype Type)
	{
		return UEnum::GetValueAsString(Type);
	}

	bool Step(UWorld* World, FState& State)
	{
		State.Time += StepSeconds;
		if (State.Time < 1.f)
		{
			return true;
		}
		if (!State.bKilled)
		{
			for (FTracked& Tracked : State.Enemies)
			{
				if (AEnemyCharacter* Enemy = Tracked.Enemy.Get())
				{
					const UEnemyAnimInstance* Anim = Cast<UEnemyAnimInstance>(Enemy->GetMesh()->GetAnimInstance());
					Tracked.bHasClip = Anim && Anim->HasDeathAnimation();
					FDamageSpec Spec;
					Spec.Amount = 100000.f;
					Spec.ArmorPenetration = 1.f;
					Enemy->GetHealthComponent()->TakeDamage(Spec);
				}
			}
			State.bKilled = true;
			return true;
		}
		bool bAllGone = true;
		for (FTracked& Tracked : State.Enemies)
		{
			AEnemyCharacter* Enemy = Tracked.Enemy.Get();
			if (!Enemy || Enemy->IsActorBeingDestroyed())
			{
				Tracked.bGone = true;
				continue;
			}
			bAllGone = false;
			Tracked.DeadFor += StepSeconds;
			// A few more shots land on the body during the first second.
			if (Tracked.DeadFor < 1.f)
			{
				FDamageSpec Spec;
				Spec.Amount = 20.f;
				Enemy->GetHealthComponent()->TakeDamage(Spec);
			}
			const UEnemyAnimInstance* Anim = Cast<UEnemyAnimInstance>(Enemy->GetMesh()->GetAnimInstance());
			const float Weight = Anim ? Anim->GetSlotMontageGlobalWeight(Anim->OneShotSlot) : 0.f;
			if (Tracked.DeadFor > 0.35f)
			{
				Tracked.MinHeldWeight = FMath::Min(Tracked.MinHeldWeight, Weight);
			}
			if (FMath::IsNearlyZero(FMath::Fmod(Tracked.DeadFor + 0.001f, 0.5f), 0.05f))
			{
				Tracked.Timeline += FString::Printf(TEXT(" %.1fs:%.2f"), Tracked.DeadFor, Weight);
			}
		}
		if (!bAllGone && State.Time < 20.f)
		{
			return true;
		}
		bool bOk = !State.Enemies.IsEmpty();
		for (const FTracked& Tracked : State.Enemies)
		{
			const bool bHeld = !Tracked.bHasClip || Tracked.MinHeldWeight > 0.9f;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s death clip=%d, removed=%d after %.1f s, one-shot weight%s -> %s"),
				bHeld && Tracked.bGone ? TEXT("ok  ") : TEXT("FAIL"), *Name(Tracked.Type), Tracked.bHasClip ? 1 : 0,
				Tracked.bGone ? 1 : 0, Tracked.DeadFor, *Tracked.Timeline, bHeld ? TEXT("held") : TEXT("RETURNED TO THE GRAPH"));
			bOk &= bHeld && Tracked.bGone;
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("EnemyDeathSmoke"));
		return false;
	}

	void Start(UWorld* World)
	{
		TSharedRef<FState> State = MakeShared<FState>();
		const FTransform Layout = SmokeUtils::LayoutTransform(World);
		const EEnemyArchetype Types[] = { EEnemyArchetype::FrostHound, EEnemyArchetype::Frostbitten, EEnemyArchetype::Cutter, EEnemyArchetype::Brute };
		int32 Index = 0;
		for (const EEnemyArchetype Type : Types)
		{
			AEnemyCharacter* Enemy = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(Type,
				SmokeUtils::LevelPoint(World, FVector(1500.f, -450.f + 300.f * Index++, 100.f)), Layout.Rotator() + FRotator(0.f, 180.f, 0.f));
			if (Enemy)
			{
				Enemy->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				State->Enemies.Add({Enemy, Type});
			}
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
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
		}), 3.f, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.EnemyDeathSmoke"),
		TEXT("Dev check: every enemy type stays in its death pose until removed; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
