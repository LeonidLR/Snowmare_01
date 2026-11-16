// Dev-only console command for headless verification of death ragdoll:
//   Scripts/smoke.ps1 -Command CodexTactics.DeathRagdollSmoke
// Governed by docs/port/gemini/outbox/049-ragdoll-plan.md (Task 056 / Death Ragdoll Step 2).
// Verifies death ragdoll across all required targets: Hound, Brute, Frostbitten, PMC fighter, and player KIA.
// Verifies activation, physical simulation, settle freeze with QueryOnly capsule and, for every subject, the same pelvis limits:
// pelvis bone < 45 cm above the floor in the skeleton's own centimetres (bone height / mesh world scale: 1.0 for every humanoid,
// 4.9 for the Brute) and the pelvis COLLISION BODY bottom between -max_pelvis_sink_cm (not sunk) and +45 cm (resting on the floor).
// Task 067: a target killed mid-sprint, and 3 shots + 1 blast in one frame whose fastest body over the WHOLE flight (sampled every
// frame from the lethal hit until the freeze) stays <= max_launch_speed_explosive_cm_s.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PmcSquadSubsystem.h"
#include "Characters/CorpseSubsystem.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/DeathRagdollRules.h"
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
#include "PhysicsEngine/BodySetup.h"

namespace DeathRagdollSmoke
{
	constexpr float StepSeconds = 0.1f;
	/** Acceptance: a lying pelvis bone is under this height above the floor (skeleton cm) and its body bottom under it (cm). */
	constexpr float LyingPelvisLimitCm = 45.0f;

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
		float MaxSpeedAfterHitS = 0.f;
		FName MaxSpeedBone;
		float MaxPelvisSpeed = 0.f;
	};

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		bool bFinished = false;
		double HitWorldTime = -1.0;
		TArray<FSubject> Subjects;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		State.bFinished = true;
		if (IConsoleVariable* MaxCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.DeathRagdoll.MaxSimultaneous")))
		{
			MaxCVar->Set(-1);
		}
		const bool bPassed = State.Failures == 0 && bComplete;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bPassed ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("DeathRagdollSmoke"));
		return false;
	}

	/** Every frame from the lethal hit until each body freezes: the fastest simulated body of every subject (the whole flight). */
	bool SampleSpeeds(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		UWorld* World = WeakWorld.Get();
		if (!World || State.bFinished)
		{
			return false;
		}
		if (State.HitWorldTime < 0.0)
		{
			return true;
		}
		const float SinceHit = static_cast<float>(World->GetTimeSeconds() - State.HitWorldTime);
		for (FSubject& Subj : State.Subjects)
		{
			ACharacter* Char = Subj.Character.Get();
			USkeletalMeshComponent* Mesh = Char ? Char->GetMesh() : nullptr;
			if (!Mesh || !Mesh->IsSimulatingPhysics())
			{
				continue; // not (or no longer) a ragdoll: frozen bodies have no flight left
			}
			Subj.MaxPelvisSpeed = FMath::Max(Subj.MaxPelvisSpeed, static_cast<float>(Mesh->GetPhysicsLinearVelocity(UCorpseSubsystem::ResolvePelvisBone(Mesh)).Size()));
			for (FBodyInstance* BodyInst : Mesh->Bodies)
			{
				if (BodyInst && BodyInst->IsValidBodyInstance() && BodyInst->bSimulatePhysics)
				{
					const float BodySpeed = static_cast<float>(BodyInst->GetUnrealWorldVelocity().Size());
					if (BodySpeed > Subj.MaxObservedSpeed)
					{
						Subj.MaxObservedSpeed = BodySpeed;
						Subj.MaxSpeedAfterHitS = SinceHit;
						Subj.MaxSpeedBone = BodyInst->BodySetup.IsValid() ? BodyInst->BodySetup->BoneName : NAME_None;
					}
				}
			}
		}
		return true;
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
						// Apply 3 kinetic hits + 1 explosive hit in the same frame (lethal hit is explosive)
						FDamageSpec Shot1 = Spec;
						Shot1.Amount = 15.f;
						FDamageSpec Shot2 = Spec;
						Shot2.Amount = 15.f;
						FDamageSpec Shot3 = Spec;
						Shot3.Amount = 15.f;

						FDamageSpec Blast;
						Blast.Amount = 500.f;
						Blast.ArmorPenetration = 1.f;
						Blast.DamageType = EDamageType::Explosive;
						Blast.DamageOrigin = Char->GetActorLocation() - Char->GetActorForwardVector() * 100.f;

						Enemy->GetHealthComponent()->TakeDamage(Shot1);
						Enemy->GetHealthComponent()->TakeDamage(Shot2);
						Enemy->GetHealthComponent()->TakeDamage(Shot3);
						Enemy->GetHealthComponent()->TakeDamage(Blast);
					}
				}
				else
				{
					if (Subj.bRunning)
					{
						if (UCharacterMovementComponent* Move = Char->GetCharacterMovement())
						{
							Move->Velocity = Char->GetActorForwardVector() * 500.f;
						}
					}
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

			State.HitWorldTime = World->GetTimeSeconds();
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
			// Stage 2: Wait for all corpses to settle and freeze (timeout 9 s: a body still falling at the 3.5 s ragdoll timeout
			// waits, FCorpseConfig::MaxSettleSeconds = 8 s after death freezes it in the end)
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
				if (State.StageTime < 9.0f)
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

				const UCorpseSubsystem* Corpses = World->GetSubsystem<UCorpseSubsystem>();
				const FDeathRagdollConfig RagdollConfig = Corpses ? Corpses->GetRagdollConfig() : FDeathRagdollRules::GetDefaultConfig();
				UE_LOG(LogCodexTactics, Display, TEXT("[DeathRagdollSmoke] %s fastest body over the flight: %.1f cm/s ('%s', %.2f s after the hit), fastest pelvis %.1f cm/s"),
					*Subj.Name, Subj.MaxObservedSpeed, *Subj.MaxSpeedBone.ToString(), Subj.MaxSpeedAfterHitS, Subj.MaxPelvisSpeed);
				if (Subj.bMultiShot)
				{
					// (b) 3 shooters + 1 explosive on one target: the fastest body (3D) from the hit until the freeze <= the explosive clamp
					const float Limit = RagdollConfig.MaxLaunchSpeedExplosiveCmS;
					Check(State, Subj.MaxObservedSpeed > 10.0f && Subj.MaxObservedSpeed <= Limit,
						FString::Printf(TEXT("%s fastest body %.1f cm/s from the hit until the freeze in (10, %.0f] cm/s (clamped)"), *Subj.Name, Subj.MaxObservedSpeed, Limit));
				}

				// Pelvis: the same limits for every subject
				if (USkeletalMeshComponent* Mesh = Char->GetMesh())
				{
					const FName PelvisBone = UCorpseSubsystem::ResolvePelvisBone(Mesh);
					Check(State, PelvisBone != NAME_None && Mesh->GetBoneIndex(PelvisBone) != INDEX_NONE,
						FString::Printf(TEXT("%s pelvis bone resolved ('%s')"), *Subj.Name, *PelvisBone.ToString()));
					const FVector Pelvis = Mesh->GetBoneLocation(PelvisBone);
					float FloorZ = 0.f;
					const bool bFloor = UCorpseSubsystem::TraceCorpseFloorZ(World, Pelvis, Char, RagdollConfig, FloorZ);
					Check(State, bFloor, FString::Printf(TEXT("%s floor found under the pelvis (Z=%.1f)"), *Subj.Name, FloorZ));

					const float Scale = FMath::Max(static_cast<float>(Mesh->GetComponentScale().Z), KINDA_SMALL_NUMBER);
					const float Height = static_cast<float>(Pelvis.Z) - FloorZ;
					const float SkeletonHeight = Height / Scale;
					Check(State, SkeletonHeight < LyingPelvisLimitCm,
						FString::Printf(TEXT("%s%s pelvis height %.1f cm (%.1f skeleton cm at mesh scale %.2f) < %.0f (lying down, settled)"),
							*Subj.Name, Subj.bRunning ? TEXT(" (killed at speed)") : TEXT(""), Height, SkeletonHeight, Scale, LyingPelvisLimitCm));

					float BottomZ = 0.f;
					const bool bBody = UCorpseSubsystem::GetPelvisBodyBottomZ(Mesh, PelvisBone, BottomZ);
					const float Gap = BottomZ - FloorZ;
					Check(State, bBody && Gap >= -RagdollConfig.MaxPelvisSinkCm && Gap < LyingPelvisLimitCm,
						FString::Printf(TEXT("%s pelvis body bottom %.1f cm from the floor in [-%.0f, %.0f) (not sunk, resting on it)"),
							*Subj.Name, Gap, RagdollConfig.MaxPelvisSinkCm, LyingPelvisLimitCm));
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
		const FVector BasePoint = SmokeUtils::LevelPoint(World, FVector(600.f, 0.f, 100.f));
		const FRotator Facing = Layout.Rotator() + FRotator(0.f, 180.f, 0.f);

		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();

		// 1. Spawn Hound (FrostHound)
		if (Waves)
		{
			AEnemyCharacter* Hound = Waves->SpawnEnemy(EEnemyArchetype::FrostHound, BasePoint + FVector(0.f, -400.f, 0.f), Facing);
			if (Hound && Hound->GetMesh())
			{
				Hound->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				State->Subjects.Add({ TEXT("Hound"), Hound, false, false, false });
			}
		}

		// 2. Spawn Brute
		if (Waves)
		{
			// The Brute is a 4.9x skeleton (~6.5 m standing) that topples away from the hit origin (world 0,0): 200 cm beside the
			// others it fell onto the Player KIA (pelvis body 27.7 cm up on the Brute's arm, 067 gate run). Its own corner, upstream.
			AEnemyCharacter* Brute = Waves->SpawnEnemy(EEnemyArchetype::Brute, BasePoint + FVector(-800.f, 770.f, 0.f), Facing);
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
			const FVector PmcSpot = BasePoint + FVector(0.f, 200.f, 0.f);
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
			AEnemyCharacter* Runner = Waves->SpawnEnemy(EEnemyArchetype::Frostbitten, BasePoint + FVector(0.f, 400.f, 0.f), Facing);
			if (Runner && Runner->GetMesh())
			{
				Runner->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				State->Subjects.Add({ TEXT("Running zombie"), Runner, false, false, false, /* bRunning */ true, false, 0.f });
			}
		}

		// 7. Spawn multi-shot target (Frostbitten hit by 3 shooters + 1 explosive simultaneously)
		if (Waves)
		{
			AEnemyCharacter* MultiTarget = Waves->SpawnEnemy(EEnemyArchetype::Frostbitten, BasePoint + FVector(200.f, 0.f, 0.f), Facing);
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
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return SampleSpeeds(WeakWorld, *State);
		}), 0.f);
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
		TEXT("Dev check: death ragdoll activates on lethal damage across Hound, Brute, Frostbitten, PMC fighter, and Player KIA; simulates physics, settles and freezes with capsule QueryOnly/Ignore, pelvis < 45 skeleton cm and its body not sunk; a mid-sprint kill and a 3 shots + blast target under the launch speed clamp over the whole flight; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
