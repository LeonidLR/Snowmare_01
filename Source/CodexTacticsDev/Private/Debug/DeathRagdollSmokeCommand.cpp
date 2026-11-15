// Dev-only console command for headless verification of death ragdoll:
//   Scripts/smoke.ps1 -Command CodexTactics.DeathRagdollSmoke
// Governed by docs/port/gemini/outbox/049-ragdoll-plan.md (Task 056 / Death Ragdoll Step 2).
// Spawns an enemy and a hostile PMC fighter, kills them with lethal damage, verifies death ragdoll
// activates, simulates physics, settles and freezes with NoCollision and pelvis < 45 cm above floor.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PmcSquadSubsystem.h"
#include "Characters/CorpseSubsystem.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"

namespace DeathRagdollSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		TWeakObjectPtr<AEnemyCharacter> Enemy;
		TWeakObjectPtr<AOperativeCharacter> PmcFighter;
		bool bEnemyRagdollActivated = false;
		bool bPmcRagdollActivated = false;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		const bool bPassed = State.Failures == 0 && bComplete;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bPassed ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("DeathRagdollSmoke"));
		return false;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}

		State.Time += StepSeconds;
		State.StageTime += StepSeconds;

		switch (State.Stage)
		{
		case 0:
		{
			// Stage 0: Wait 0.5s for spawned characters to touch ground and initialize
			if (State.StageTime < 0.5f)
			{
				return true;
			}

			AEnemyCharacter* Enemy = State.Enemy.Get();
			AOperativeCharacter* Fighter = State.PmcFighter.Get();
			Check(State, Enemy && Enemy->GetHealthComponent() && Enemy->GetHealthComponent()->IsAlive(),
				TEXT("Enemy spawned and alive"));
			Check(State, Fighter && Fighter->HealthComponent && Fighter->HealthComponent->IsAlive(),
				TEXT("PMC fighter spawned and alive"));

			if (State.Failures > 0)
			{
				return Finish(State, false);
			}

			// Deal lethal damage to both
			FDamageSpec Spec;
			Spec.Amount = 500.f;
			Spec.ArmorPenetration = 1.f;
			Spec.DamageType = EDamageType::Kinetic;

			Enemy->GetHealthComponent()->TakeDamage(Spec);
			Fighter->HealthComponent->TakeDamage(Spec);

			UE_LOG(LogCodexTactics, Display, TEXT("[DeathRagdollSmoke] Lethal damage applied to enemy and PMC fighter"));
			++State.Stage;
			State.StageTime = 0.f;
			return true;
		}
		case 1:
		{
			// Stage 1: Verify ragdoll activation after short blend-in (0.3s)
			if (State.StageTime < 0.3f)
			{
				return true;
			}

			AEnemyCharacter* Enemy = State.Enemy.Get();
			AOperativeCharacter* Fighter = State.PmcFighter.Get();
			if (!Enemy || !Fighter)
			{
				Check(State, false, TEXT("Characters valid during ragdoll check"));
				return Finish(State, false);
			}

			State.bEnemyRagdollActivated = Enemy->IsRagdollActive();
			State.bPmcRagdollActivated = Fighter->IsRagdollActive();

			Check(State, State.bEnemyRagdollActivated, TEXT("Enemy death ragdoll active"));
			Check(State, State.bPmcRagdollActivated, TEXT("PMC fighter death ragdoll active"));

			const bool bEnemyCapsuleNoColl = Enemy->GetCapsuleComponent() &&
				Enemy->GetCapsuleComponent()->GetCollisionEnabled() == ECollisionEnabled::NoCollision;
			Check(State, bEnemyCapsuleNoColl, TEXT("Enemy capsule collision disabled (NoCollision)"));

			const bool bPmcCapsuleIgnoresPawn = Fighter->GetCapsuleComponent() &&
				Fighter->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Ignore;
			Check(State, bPmcCapsuleIgnoresPawn, TEXT("PMC fighter capsule ignores pawn channel"));

			++State.Stage;
			State.StageTime = 0.f;
			return true;
		}
		case 2:
		{
			// Stage 2: Wait for both corpses to settle and freeze (timeout 5.0s)
			AEnemyCharacter* Enemy = State.Enemy.Get();
			AOperativeCharacter* Fighter = State.PmcFighter.Get();
			if (!Enemy || !Fighter)
			{
				Check(State, false, TEXT("Characters valid during settle check"));
				return Finish(State, false);
			}

			const bool bEnemyFrozen = Enemy->IsCorpseFrozen();
			const bool bPmcFrozen = Fighter->IsCorpseFrozen();

			if (!bEnemyFrozen || !bPmcFrozen)
			{
				if (State.StageTime < 5.0f)
				{
					return true;
				}
				// Timed out waiting for freeze
				Check(State, bEnemyFrozen, TEXT("Enemy frozen before timeout"));
				Check(State, bPmcFrozen, TEXT("PMC fighter frozen before timeout"));
				return Finish(State, false);
			}

			// Both are frozen! Check final state and pelvis heights
			Check(State, bEnemyFrozen, FString::Printf(TEXT("Enemy corpse frozen after %.1f s"), State.StageTime));
			Check(State, bPmcFrozen, FString::Printf(TEXT("PMC fighter corpse frozen after %.1f s"), State.StageTime));

			// Check enemy pelvis height above floor
			if (USkeletalMeshComponent* Mesh = Enemy->GetMesh())
			{
				const FVector Pelvis = Mesh->GetBoneLocation(FName(TEXT("pelvis")));
				const float CapsuleHalf = Enemy->GetCapsuleComponent() ? Enemy->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() : 90.f;
				float FloorZ = Enemy->GetActorLocation().Z - CapsuleHalf;

				FHitResult Hit;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(DeathRagdollFloor), false, Enemy);
				if (World->LineTraceSingleByChannel(Hit, Pelvis + FVector(0.f, 0.f, 100.f), Pelvis - FVector(0.f, 0.f, 500.f), ECC_WorldStatic, Params))
				{
					FloorZ = static_cast<float>(Hit.ImpactPoint.Z);
				}

				const float Height = Pelvis.Z - FloorZ;
				Check(State, Height < 45.0f, FString::Printf(TEXT("Enemy pelvis height %.1f cm < 45 cm (lying down, not standing)"), Height));
			}
			else
			{
				Check(State, false, TEXT("Enemy skeletal mesh valid"));
			}

			// Check PMC fighter pelvis height above floor
			if (USkeletalMeshComponent* Mesh = Fighter->GetMesh())
			{
				const FVector Pelvis = Mesh->GetBoneLocation(FName(TEXT("pelvis")));
				const float CapsuleHalf = Fighter->GetCapsuleComponent() ? Fighter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() : 90.f;
				float FloorZ = Fighter->GetActorLocation().Z - CapsuleHalf;

				FHitResult Hit;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(DeathRagdollFloor), false, Fighter);
				if (World->LineTraceSingleByChannel(Hit, Pelvis + FVector(0.f, 0.f, 100.f), Pelvis - FVector(0.f, 0.f, 500.f), ECC_WorldStatic, Params))
				{
					FloorZ = static_cast<float>(Hit.ImpactPoint.Z);
				}

				const float Height = Pelvis.Z - FloorZ;
				Check(State, Height < 45.0f, FString::Printf(TEXT("PMC fighter pelvis height %.1f cm < 45 cm (lying down, not standing)"), Height));
			}
			else
			{
				Check(State, false, TEXT("PMC fighter skeletal mesh valid"));
			}

			return Finish(State, State.Failures == 0);
		}
		default:
			return Finish(State, false);
		}
	}

	void Start(UWorld* World)
	{
		if (!World)
		{
			return;
		}

		TSharedRef<FState> State = MakeShared<FState>();
		const FTransform Layout = SmokeUtils::LayoutTransform(World);
		const FVector BasePoint = SmokeUtils::LevelPoint(World, FVector(1200.f, 0.f, 100.f));

		// 1. Spawn enemy
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		if (Waves)
		{
			AEnemyCharacter* Enemy = Waves->SpawnEnemy(
				EEnemyArchetype::Frostbitten,
				BasePoint + FVector(0.f, -200.f, 0.f),
				Layout.Rotator() + FRotator(0.f, 180.f, 0.f));
			if (Enemy && Enemy->GetMesh())
			{
				Enemy->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				State->Enemy = Enemy;
			}
		}

		// 2. Spawn hostile PMC operative
		UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
		if (Pmc)
		{
			const FVector PmcSpot = BasePoint + FVector(0.f, 200.f, 0.f);
			const TArray<AOperativeCharacter*> Spawned = Pmc->SpawnSquad(1, PmcSpot, Layout.Rotator() + FRotator(0.f, 180.f, 0.f), 1, 7);
			if (!Spawned.IsEmpty() && Spawned[0])
			{
				Pmc->SetTeamCommanded(1, false);
				if (USkeletalMeshComponent* Mesh = Spawned[0]->GetMesh())
				{
					Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				}
				State->PmcFighter = Spawned[0];
			}
		}

		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
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
		}), 1.5f, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.DeathRagdollSmoke"),
		TEXT("Dev check: death ragdoll activates on lethal damage, simulates physics, settles and freezes with capsule NoCollision and pelvis < 45cm; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
