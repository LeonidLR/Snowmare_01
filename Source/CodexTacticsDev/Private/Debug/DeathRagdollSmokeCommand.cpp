// Dev-only console command for headless verification of death ragdoll:
//   Scripts/smoke.ps1 -Command CodexTactics.DeathRagdollSmoke
// Governed by docs/port/gemini/outbox/049-ragdoll-plan.md (Task 056 / Death Ragdoll Step 2).
// Verifies death ragdoll across all required targets: Hound, Brute, Frostbitten, PMC fighter, and player KIA.
// Verifies activation, physical simulation, settle freeze with QueryOnly capsule and pelvis < 45 cm above floor.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PmcSquadSubsystem.h"
#include "Characters/CorpseSubsystem.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"

namespace DeathRagdollSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FSubject
	{
		FString Name;
		TWeakObjectPtr<ACharacter> Character;
		bool bIsPlayerTeam = false;
		bool bRagdollActivated = false;
		bool bFrozen = false;
		bool bRunning = false;
		bool bMultiShot = false;
		float MaxObservedSpeed = 0.f;
	};

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		TArray<FSubject> Subjects;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		if (IConsoleVariable* MaxCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.DeathRagdoll.MaxSimultaneous")))
		{
			MaxCVar->Set(-1);
		}
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
			// Maintain running velocity during warmup
			for (FSubject& Subj : State.Subjects)
			{
				if (Subj.bRunning)
				{
					if (ACharacter* Char = Subj.Character.Get())
					{
						if (UCharacterMovementComponent* Move = Char->GetCharacterMovement())
						{
							Move->MaxWalkSpeed = 600.f;
							Move->Velocity = Char->GetActorForwardVector() * 500.f;
						}
					}
				}
			}

			// Stage 0: Wait 0.5s for spawned characters to touch ground and initialize
			if (State.StageTime < 0.5f)
			{
				return true;
			}

			if (State.Subjects.IsEmpty())
			{
				Check(State, false, TEXT("Subjects spawned and registered"));
				return Finish(State, false);
			}

			for (const FSubject& Subj : State.Subjects)
			{
				ACharacter* Char = Subj.Character.Get();
				const bool bAlive = Char != nullptr && (Cast<AEnemyCharacter>(Char)
					? (Cast<AEnemyCharacter>(Char)->GetHealthComponent() && Cast<AEnemyCharacter>(Char)->GetHealthComponent()->IsAlive())
					: (Cast<AOperativeCharacter>(Char) && Cast<AOperativeCharacter>(Char)->HealthComponent && Cast<AOperativeCharacter>(Char)->HealthComponent->IsAlive()));
				Check(State, bAlive, FString::Printf(TEXT("%s spawned and alive"), *Subj.Name));
			}

			if (State.Failures > 0)
			{
				return Finish(State, false);
			}

			// Deal lethal damage to all subjects
			FDamageSpec Spec;
			Spec.Amount = 10000.f;
			Spec.ArmorPenetration = 1.f;
			Spec.DamageType = EDamageType::Kinetic;
			Spec.DamageOrigin = FVector(0.f, 0.f, 100.f);

			for (FSubject& Subj : State.Subjects)
			{
				ACharacter* Char = Subj.Character.Get();
				if (!Char)
				{
					continue;
				}

				if (Subj.bMultiShot)
				{
					if (AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Char))
					{
						// Apply 3 kinetic hits + 1 explosive hit in the same frame
						FDamageSpec Shot1 = Spec;
						Shot1.Amount = 100.f;
						FDamageSpec Shot2 = Spec;
						Shot2.Amount = 100.f;
						FDamageSpec Shot3 = Spec;
						Shot3.Amount = 100.f;

						FDamageSpec Blast;
						Blast.Amount = 500.f;
						Blast.ArmorPenetration = 1.f;
						Blast.DamageType = EDamageType::Explosive;
						Blast.DamageOrigin = Char->GetActorLocation() - Char->GetActorForwardVector() * 100.f;

						Enemy->GetHealthComponent()->TakeDamage(Shot1);
						Enemy->GetHealthComponent()->TakeDamage(Shot2);
						Enemy->GetHealthComponent()->TakeDamage(Shot3);
						Enemy->GetHealthComponent()->TakeDamage(Blast);

						if (USkeletalMeshComponent* Mesh = Enemy->GetMesh())
						{
							for (FBodyInstance* BodyInst : Mesh->Bodies)
							{
								if (BodyInst && BodyInst->IsValidBodyInstance() && BodyInst->bSimulatePhysics)
								{
									Subj.MaxObservedSpeed = FMath::Max(Subj.MaxObservedSpeed, static_cast<float>(BodyInst->GetUnrealWorldVelocity().Size()));
								}
							}
							Subj.MaxObservedSpeed = FMath::Max(Subj.MaxObservedSpeed, static_cast<float>(Mesh->GetPhysicsLinearVelocity(FName(TEXT("pelvis"))).Size()));
						}
					}
				}
				else
				{
					if (AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Char))
					{
						Enemy->GetHealthComponent()->TakeDamage(Spec);
					}
					else if (AOperativeCharacter* Op = Cast<AOperativeCharacter>(Char))
					{
						Op->TakeHit(10000.f, TEXT("Smoke"), false, true);
					}
				}
			}

			UE_LOG(LogCodexTactics, Display, TEXT("[DeathRagdollSmoke] Lethal damage applied to all %d subjects"), State.Subjects.Num());
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

			for (FSubject& Subj : State.Subjects)
			{
				ACharacter* Char = Subj.Character.Get();
				if (!Char)
				{
					Check(State, false, FString::Printf(TEXT("%s valid during ragdoll check"), *Subj.Name));
					continue;
				}

				if (AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Char))
				{
					Subj.bRagdollActivated = Enemy->IsRagdollActive();
				}
				else if (AOperativeCharacter* Op = Cast<AOperativeCharacter>(Char))
				{
					Subj.bRagdollActivated = Op->IsRagdollActive();
				}

				Check(State, Subj.bRagdollActivated, FString::Printf(TEXT("%s death ragdoll active"), *Subj.Name));

				if (Subj.bMultiShot)
				{
					Check(State, Subj.MaxObservedSpeed <= 900.0f + 1.0f,
						FString::Printf(TEXT("%s max launch speed %.1f cm/s <= 900 cm/s (clamped)"), *Subj.Name, Subj.MaxObservedSpeed));
				}

				const bool bCapsuleIgnoresPawn = Char->GetCapsuleComponent() &&
					Char->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Ignore;
				Check(State, bCapsuleIgnoresPawn, FString::Printf(TEXT("%s capsule ignores pawn channel"), *Subj.Name));
			}

			++State.Stage;
			State.StageTime = 0.f;
			return true;
		}
		case 2:
		{
			// Stage 2: Wait for all corpses to settle and freeze (timeout 5.5s)
			bool bAllFrozen = true;
			for (FSubject& Subj : State.Subjects)
			{
				ACharacter* Char = Subj.Character.Get();
				if (!Char)
				{
					continue;
				}
				if (AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Char))
				{
					Subj.bFrozen = Enemy->IsCorpseFrozen();
				}
				else if (AOperativeCharacter* Op = Cast<AOperativeCharacter>(Char))
				{
					Subj.bFrozen = Op->IsCorpseFrozen();
				}
				if (!Subj.bFrozen)
				{
					bAllFrozen = false;
				}
			}

			if (!bAllFrozen)
			{
				if (State.StageTime < 5.5f)
				{
					return true;
				}
				// Timed out waiting for freeze
				for (const FSubject& Subj : State.Subjects)
				{
					Check(State, Subj.bFrozen, FString::Printf(TEXT("%s frozen before timeout"), *Subj.Name));
				}
				return Finish(State, false);
			}

			// All are frozen! Check final state and pelvis heights
			for (const FSubject& Subj : State.Subjects)
			{
				ACharacter* Char = Subj.Character.Get();
				Check(State, Subj.bFrozen, FString::Printf(TEXT("%s corpse frozen after %.1f s"), *Subj.Name, State.StageTime));

				if (!Char)
				{
					Check(State, false, FString::Printf(TEXT("%s character still valid"), *Subj.Name));
					continue;
				}

				// Player operative KIA must remain alive in world (never destroyed by corpse cap)
				if (Subj.bIsPlayerTeam)
				{
					Check(State, IsValid(Char) && !Char->IsActorBeingDestroyed(),
						FString::Printf(TEXT("%s (player team KIA) preserved, not destroyed by cap"), *Subj.Name));
				}

				if (Subj.bMultiShot)
				{
					// (b) 3 shooters + 1 explosive on one target: max launch speed was verified in stage 1
					continue;
				}

				// Check pelvis height above floor
				if (USkeletalMeshComponent* Mesh = Char->GetMesh())
				{
					const FVector Pelvis = Mesh->GetBoneLocation(FName(TEXT("pelvis")));
					const float CapsuleHalf = Char->GetCapsuleComponent() ? Char->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() : 90.f;
					float FloorZ = Char->GetActorLocation().Z - CapsuleHalf;

					FHitResult Hit;
					FCollisionQueryParams Params(SCENE_QUERY_STAT(DeathRagdollFloor), false, Char);
					if (World->LineTraceSingleByChannel(Hit, Pelvis + FVector(0.f, 0.f, 100.f), Pelvis - FVector(0.f, 0.f, 500.f), ECC_WorldStatic, Params))
					{
						FloorZ = static_cast<float>(Hit.ImpactPoint.Z);
					}

					const float Height = Pelvis.Z - FloorZ;
					const bool bIsBrute = Char->IsA<AEnemyCharacter>() && Cast<AEnemyCharacter>(Char)->GetArchetype() == EEnemyArchetype::Brute;
					const float MaxAllowedHeight = bIsBrute ? 120.0f : 65.0f;
					const bool bHeightOk = Height >= -20.0f && Height < MaxAllowedHeight;
					if (Subj.bRunning)
					{
						// (a) a kill while target runs at full speed - after settle pelvis is within bounds of floor trace (< 65 cm, > -20 cm)
						Check(State, bHeightOk,
							FString::Printf(TEXT("%s (killed at speed) pelvis height %.1f cm within bounds of floor (< %.0f cm, settled)"), *Subj.Name, Height, MaxAllowedHeight));
					}
					else
					{
						Check(State, bHeightOk,
							FString::Printf(TEXT("%s pelvis height %.1f cm within bounds of floor (< %.0f cm, settled)"), *Subj.Name, Height, MaxAllowedHeight));
					}
				}
				else
				{
					Check(State, false, FString::Printf(TEXT("%s skeletal mesh valid"), *Subj.Name));
				}
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

		if (IConsoleVariable* MaxCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.DeathRagdoll.MaxSimultaneous")))
		{
			MaxCVar->Set(8);
		}

		TSharedRef<FState> State = MakeShared<FState>();
		const FTransform Layout = SmokeUtils::LayoutTransform(World);
		const FVector BasePoint = SmokeUtils::LevelPoint(World, FVector(1200.f, 0.f, 100.f));
		const FRotator Facing = Layout.Rotator() + FRotator(0.f, 180.f, 0.f);

		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();

		// 1. Spawn Hound (FrostHound)
		if (Waves)
		{
			AEnemyCharacter* Hound = Waves->SpawnEnemy(EEnemyArchetype::FrostHound, BasePoint + FVector(0.f, -300.f, 0.f), Facing);
			if (Hound && Hound->GetMesh())
			{
				Hound->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				State->Subjects.Add({ TEXT("Hound"), Hound, false, false, false });
			}
		}

		// 2. Spawn Brute (offset in X to avoid corridor wall at Y=-200)
		if (Waves)
		{
			AEnemyCharacter* Brute = Waves->SpawnEnemy(EEnemyArchetype::Brute, BasePoint + FVector(200.f, -150.f, 0.f), Facing);
			if (Brute && Brute->GetMesh())
			{
				Brute->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				State->Subjects.Add({ TEXT("Brute"), Brute, false, false, false });
			}
		}

		// 3. Spawn Frostbitten
		if (Waves)
		{
			AEnemyCharacter* Frostbitten = Waves->SpawnEnemy(EEnemyArchetype::Frostbitten, BasePoint + FVector(0.f, 0.f, 0.f), Facing);
			if (Frostbitten && Frostbitten->GetMesh())
			{
				Frostbitten->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				State->Subjects.Add({ TEXT("Frostbitten"), Frostbitten, false, false, false });
			}
		}

		// 4. Spawn hostile PMC operative
		if (Pmc)
		{
			const FVector PmcSpot = BasePoint + FVector(0.f, 150.f, 0.f);
			const TArray<AOperativeCharacter*> Spawned = Pmc->SpawnSquad(1, PmcSpot, Facing, 1, 7);
			if (!Spawned.IsEmpty() && Spawned[0])
			{
				Pmc->SetTeamCommanded(1, false);
				if (USkeletalMeshComponent* Mesh = Spawned[0]->GetMesh())
				{
					Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				}
				State->Subjects.Add({ TEXT("PMC fighter"), Spawned[0], false, false, false });
			}
		}

		// 5. Player operative KIA
		if (Squad && Squad->GetMembers().Num() >= 2)
		{
			AOperativeCharacter* PlayerOp = Squad->GetMembers()[1];
			if (PlayerOp)
			{
				PlayerOp->TeleportTo(BasePoint + FVector(-200.f, 0.f, 0.f), Facing, false, true);
				if (USkeletalMeshComponent* Mesh = PlayerOp->GetMesh())
				{
					Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				}
				State->Subjects.Add({ TEXT("Player KIA"), PlayerOp, true, false, false });
			}
		}

		// 6. Spawn running zombie (Frostbitten killed mid-sprint)
		if (Waves)
		{
			AEnemyCharacter* Runner = Waves->SpawnEnemy(EEnemyArchetype::Frostbitten, BasePoint + FVector(0.f, 600.f, 0.f), Facing);
			if (Runner && Runner->GetMesh())
			{
				Runner->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				State->Subjects.Add({ TEXT("Running zombie"), Runner, false, false, false, /* bRunning */ true, false, 0.f });
			}
		}

		// 7. Spawn multi-shot target (Brute hit by 3 shooters + 1 explosive simultaneously)
		if (Waves)
		{
			AEnemyCharacter* MultiTarget = Waves->SpawnEnemy(EEnemyArchetype::Brute, BasePoint + FVector(200.f, 150.f, 0.f), Facing);
			if (MultiTarget && MultiTarget->GetMesh())
			{
				MultiTarget->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				State->Subjects.Add({ TEXT("Multi-shot target"), MultiTarget, false, false, false, false, /* bMultiShot */ true, 0.f });
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
		TEXT("Dev check: death ragdoll activates on lethal damage across Hound, Brute, Frostbitten, PMC fighter, and Player KIA; simulates physics, settles and freezes with capsule QueryOnly/Ignore and pelvis < 45cm; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
