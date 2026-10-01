// Dev-only console command for a headless check of the enemy upper-body hit layer (TANDEM request 1) on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.EnemyHitLayerSmoke
// Enemies hit while running used to play the full-body hit clip and slide on the spot. Now (ABP_Enemy_* from
// Scripts/Editor/setup_enemy_animation.py, layered blend from spine_01 / the hound's neck) the hit plays on the UpperBody
// slot while the legs keep running. Checked for a frostbitten, a hound and a brute; the cutter has no hit clip (Godot
// cutter.tres enable_hit_reaction off) and must play nothing.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Animation/AnimInstance.h"
#include "Characters/EnemyAnimInstance.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

namespace EnemyHitLayerSmoke
{
	constexpr float StepSeconds = 0.05f;
	const EEnemyArchetype Types[] = { EEnemyArchetype::Frostbitten, EEnemyArchetype::Cutter, EEnemyArchetype::FrostHound, EEnemyArchetype::Brute };

	struct FState
	{
		float Time = 0.f;
		int32 Index = 0;
		int32 Stage = 0;
		bool bOk = true;
		bool bHit = false;
		float SpeedAtHit = 0.f;
		TWeakObjectPtr<AEnemyCharacter> Enemy;
	};

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.bOk && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("EnemyHitLayerSmoke"));
		return false;
	}

	void Hit(AEnemyCharacter* Enemy)
	{
		FDamageSpec Spec;
		Spec.Amount = 1.f;
		Spec.AttackerSource = TEXT("smoke");
		Enemy->GetHealthComponent()->ApplyDamage(Spec);
	}

	bool Step(UWorld* World, FState& State)
	{
		State.Time += StepSeconds;
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Leader)
		{
			return Finish(State, false);
		}
		AEnemyCharacter* Enemy = State.Enemy.Get();
		UEnemyAnimInstance* Anim = Enemy && Enemy->GetMesh() ? Cast<UEnemyAnimInstance>(Enemy->GetMesh()->GetAnimInstance()) : nullptr;
		switch (State.Stage)
		{
		case 0:
			if (State.Index >= UE_ARRAY_COUNT(Types))
			{
				return Finish(State, true);
			}
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->HealthComponent->SetMaxHealth(100000.f);
				Member->bTacticalCeaseFire = true;
			}
			Enemy = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(Types[State.Index],
				Leader->GetActorLocation() + Leader->GetActorForwardVector() * 1800.f + FVector(0.f, 0.f, 20.f));
			if (!Enemy)
			{
				return Finish(State, false);
			}
			Enemy->GetHealthComponent()->SetMaxHealth(100000.f);
			State.Enemy = Enemy;
			State.Stage = 1;
			State.Time = 0.f;
			State.bHit = false;
			return true;
		case 1:
			if (!Enemy || !Anim)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke FAIL: no enemy / UEnemyAnimInstance"));
				State.bOk = false;
				return Finish(State, false);
			}
			// Running at the squad: hit it once it is well under way.
			if (!State.bHit)
			{
				if (Anim->bIsMoving && Enemy->GetVelocity().Size2D() > 150.f && State.Time > 0.6f)
				{
					Hit(Enemy);
					State.bHit = true;
					State.SpeedAtHit = Enemy->GetVelocity().Size2D();
					State.Time = 0.f;
				}
				else if (State.Time > 6.f)
				{
					UE_LOG(LogCodexTactics, Display, TEXT("Smoke FAIL: %s never ran"), *Enemy->GetEnemyDisplayName());
					State.bOk = false;
					State.Stage = 2;
				}
				return true;
			}
			if (State.Time < 0.15f)
			{
				return true;
			}
			{
				const bool bUpper = Anim->IsSlotActive(Anim->UpperBodySlot);
				const bool bFull = Anim->IsSlotActive(Anim->OneShotSlot);
				const float Speed = Enemy->GetVelocity().Size2D();
				const bool bHasClip = !Anim->HitAnimations.IsEmpty();
				const bool bGood = (bHasClip ? bUpper : !bUpper) && !bFull && Speed > 100.f;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s hit while running (%.0f cm/s, hit clips %d): upper-body slot %d, full-body slot %d, still running %.0f cm/s"),
					bGood ? TEXT("ok  ") : TEXT("FAIL"), *Enemy->GetEnemyDisplayName(), State.SpeedAtHit, Anim->HitAnimations.Num(), bUpper ? 1 : 0, bFull ? 1 : 0, Speed);
				State.bOk &= bGood;
			}
			State.Stage = 2;
			return true;
		case 2:
			if (Enemy)
			{
				Enemy->Destroy();
			}
			++State.Index;
			State.Stage = 0;
			return true;
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
				}), StepSeconds, true);
			}
		}), 3.f, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.EnemyHitLayerSmoke"),
		TEXT("Dev check: enemies hit while running flinch on the upper-body slot and keep running; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
