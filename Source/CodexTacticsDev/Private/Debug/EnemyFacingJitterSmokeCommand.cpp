// Dev-only console command for headless validation of enemy and PMC facing jitter (designer bug B17-5):
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -NoRetry -ExtraArgs -Multiprocess -Commands EnemyFacingJitterSmoke
// Tests 3 subjects facing two equidistant targets (separated laterally) to detect and measure yaw jitter (sign flips of DeltaYaw):
// 1. AMarksmanEnemyCharacter (20 m range, aiming and tracking)
// 2. AEnemyCharacter (Frostbitten melee archetype, 12 m range)
// 3. APmcCharacter / AOperativeCharacter (PMC fighter under UPmcSquadSubsystem, 18 m range)
// Passing threshold: <= 2.5 flips per second over 3.0 seconds after sighting the squad.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PmcSquadSubsystem.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/MarksmanEnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

namespace EnemyFacingJitterSmoke
{
	constexpr float StepSeconds = 0.05f;
	constexpr float SampleWindowSeconds = 3.0f;
	constexpr float MaxFlipsPerSecond = 2.5f;
	constexpr float MinFlipThresholdDeg = 0.2f;

	struct FSubjectTrack
	{
		FString Name;
		TWeakObjectPtr<AActor> Subject;
		float LastYaw = 0.f;
		float LastDeltaYaw = 0.f;
		int32 Flips = 0;
		float MaxStepDeg = 0.f;
		bool bStarted = false;
		int32 SampleCount = 0;
	};

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;

		FVector ArenaOrigin = FVector::ZeroVector;
		FVector ForwardDir = FVector::ForwardVector;
		FVector RightDir = FVector::RightVector;

		TWeakObjectPtr<AOperativeCharacter> TargetA;
		TWeakObjectPtr<AOperativeCharacter> TargetB;

		FSubjectTrack CurrentTrack;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("EnemyFacingJitterSmoke"));
		return false;
	}

	void SampleTrack(FSubjectTrack& Track, float StageTime)
	{
		AActor* Actor = Track.Subject.Get();
		if (!Actor)
		{
			return;
		}
		const float Yaw = Actor->GetActorRotation().Yaw;
		Track.SampleCount++;

		FString CurrentTargetName = TEXT("None");
		if (const AMarksmanEnemyCharacter* Marksman = Cast<AMarksmanEnemyCharacter>(Actor))
		{
			if (const AActor* Tgt = Marksman->GetCurrentTarget())
			{
				CurrentTargetName = Tgt->GetName();
			}
		}
		else if (const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Actor))
		{
			if (const AActor* Tgt = Enemy->GetCurrentTarget())
			{
				CurrentTargetName = Tgt->GetName();
			}
		}
		else if (const AOperativeCharacter* Op = Cast<AOperativeCharacter>(Actor))
		{
			if (const AActor* Tgt = Op->GetManualPriorityTarget())
			{
				CurrentTargetName = Tgt->GetName();
			}
		}

		float Delta = 0.f;
		if (Track.bStarted)
		{
			Delta = FRotator::NormalizeAxis(Yaw - Track.LastYaw);
			Track.MaxStepDeg = FMath::Max(Track.MaxStepDeg, FMath::Abs(Delta));

			if (FMath::Abs(Delta) >= MinFlipThresholdDeg && FMath::Abs(Track.LastDeltaYaw) >= MinFlipThresholdDeg)
			{
				if (FMath::Sign(Delta) != FMath::Sign(Track.LastDeltaYaw))
				{
					Track.Flips++;
					UE_LOG(LogCodexTactics, Display, TEXT("[JitterFlip] %s flip #%d at t=%.2f: Yaw=%.2f, Delta=%.2f, PrevDelta=%.2f, Target=%s"),
						*Track.Name, Track.Flips, StageTime, Yaw, Delta, Track.LastDeltaYaw, *CurrentTargetName);
				}
			}

			if (FMath::Abs(Delta) >= MinFlipThresholdDeg)
			{
				Track.LastDeltaYaw = Delta;
			}
		}

		Track.LastYaw = Yaw;
		Track.bStarted = true;

		UE_LOG(LogCodexTactics, Display, TEXT("[JitterTrace] %s t=%.2f: Yaw=%.2f, Delta=%.2f, Target=%s, Flips=%d"),
			*Track.Name, StageTime, Yaw, Delta, *CurrentTargetName, Track.Flips);
	}

	void NextStage(FState& State)
	{
		State.Stage++;
		State.StageTime = 0.f;
	}

	bool Step(UWorld* World, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;

		USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!World || !Squad || !Leader)
		{
			Check(State, false, TEXT("World / SquadSubsystem / Leader missing"));
			return Finish(State, false);
		}

		switch (State.Stage)
		{
		case 0:
		{
			// Setup test geometry on L_MovementTest
			State.ArenaOrigin = Leader->GetActorLocation();
			State.ForwardDir = Leader->GetActorForwardVector().GetSafeNormal2D();
			State.RightDir = Leader->GetActorRightVector().GetSafeNormal2D();

			Squad->SetFollowersHolding(true);

			TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			if (Members.Num() < 2)
			{
				Check(State, false, TEXT("Need at least 2 squad members for equidistant test"));
				return Finish(State, false);
			}

			for (int32 Idx = 0; Idx < Members.Num(); ++Idx)
			{
				AOperativeCharacter* Member = Members[Idx];
				if (Member)
				{
					Member->bTacticalCeaseFire = true;
					Member->StopOperative();
					if (Member->HealthComponent)
					{
						Member->HealthComponent->SetMaxHealth(100000.f);
					}
					if (Idx >= 2)
					{
						// Park extra members far away
						Member->TeleportTo(State.ArenaOrigin + State.RightDir * (10000.f + Idx * 500.f), State.ForwardDir.Rotation(), false, true);
						if (Member->GetCharacterMovement())
						{
							Member->GetCharacterMovement()->DisableMovement();
						}
					}
				}
			}

			State.TargetA = Members[0];
			State.TargetB = Members[1];

			// Position TargetA and TargetB symmetrically around Forward line
			// Distance = sqrt(500^2 + 150^2) = 522 cm from Origin
			const FVector SpotA = State.ArenaOrigin + State.ForwardDir * 500.f - State.RightDir * 150.f;
			const FVector SpotB = State.ArenaOrigin + State.ForwardDir * 500.f + State.RightDir * 150.f;
			State.TargetA->TeleportTo(SpotA, State.ForwardDir.Rotation(), false, true);
			State.TargetB->TeleportTo(SpotB, State.ForwardDir.Rotation(), false, true);
			if (State.TargetA->GetCharacterMovement())
			{
				State.TargetA->GetCharacterMovement()->DisableMovement();
			}
			if (State.TargetB->GetCharacterMovement())
			{
				State.TargetB->GetCharacterMovement()->DisableMovement();
			}

			// Stage 1: AMarksmanEnemyCharacter placed at Origin + Forward * 2500
			// Distance to both TargetA and TargetB is sqrt(2000^2 + 150^2) = 2005.6 cm
			const FVector MarksmanSpot = State.ArenaOrigin + State.ForwardDir * 2500.f + FVector(0.f, 0.f, 20.f);
			const FRotator MarksmanFacing = (-State.ForwardDir).Rotation();

			UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
			if (!Waves)
			{
				Check(State, false, TEXT("WaveSubsystem missing"));
				return Finish(State, false);
			}

			AMarksmanEnemyCharacter* Marksman = Cast<AMarksmanEnemyCharacter>(Waves->SpawnEnemy(EEnemyArchetype::Marksman, MarksmanSpot, MarksmanFacing));
			if (!Marksman)
			{
				Check(State, false, TEXT("Spawn Marksman failed"));
				return Finish(State, false);
			}

			State.CurrentTrack = FSubjectTrack();
			State.CurrentTrack.Name = TEXT("Marksman");
			State.CurrentTrack.Subject = Marksman;

			UE_LOG(LogCodexTactics, Display, TEXT("[EnemyFacingJitterSmoke] Stage 1: Spawned Marksman at %s facing %s"),
				*MarksmanSpot.ToString(), *MarksmanFacing.ToString());

			NextStage(State);
			return true;
		}

		case 1:
		{
			// Fluctuate equidistant targets by +/- 4 cm along forward axis (realistic combat breathing / stepping)
			if (State.TargetA.IsValid() && State.TargetB.IsValid())
			{
				const float Wobble = 4.f * FMath::Sin(State.StageTime * 20.f);
				const FVector SpotA = State.ArenaOrigin + State.ForwardDir * (500.f + Wobble) - State.RightDir * 150.f;
				const FVector SpotB = State.ArenaOrigin + State.ForwardDir * (500.f - Wobble) + State.RightDir * 150.f;
				State.TargetA->SetActorLocation(SpotA);
				State.TargetB->SetActorLocation(SpotB);
			}

			// Sample Marksman for SampleWindowSeconds
			SampleTrack(State.CurrentTrack, State.StageTime);

			if (State.StageTime >= SampleWindowSeconds)
			{
				const float FlipsPerSec = State.CurrentTrack.Flips / State.StageTime;
				const bool bPass = FlipsPerSec <= MaxFlipsPerSecond;
				Check(State, bPass, FString::Printf(TEXT("Marksman facing jitter: %d flips in %.1fs (%.2f flips/s, limit %.2f, max step %.1f deg)"),
					State.CurrentTrack.Flips, State.StageTime, FlipsPerSec, MaxFlipsPerSecond, State.CurrentTrack.MaxStepDeg));

				if (AActor* Act = State.CurrentTrack.Subject.Get())
				{
					Act->Destroy();
				}

				// Prepare Stage 2: Frostbitten at Origin + Forward * 1700
				// Distance to both TargetA and TargetB = sqrt(1200^2 + 150^2) = 1209.3 cm
				const FVector FrostbittenSpot = State.ArenaOrigin + State.ForwardDir * 1700.f + FVector(0.f, 0.f, 20.f);
				const FRotator FrostbittenFacing = (-State.ForwardDir).Rotation();

				UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
				AEnemyCharacter* Frostbitten = Waves ? Waves->SpawnEnemy(EEnemyArchetype::Frostbitten, FrostbittenSpot, FrostbittenFacing) : nullptr;
				if (!Frostbitten)
				{
					Check(State, false, TEXT("Spawn Frostbitten failed"));
					return Finish(State, false);
				}

				State.CurrentTrack = FSubjectTrack();
				State.CurrentTrack.Name = TEXT("Frostbitten");
				State.CurrentTrack.Subject = Frostbitten;

				UE_LOG(LogCodexTactics, Display, TEXT("[EnemyFacingJitterSmoke] Stage 2: Spawned Frostbitten at %s"), *FrostbittenSpot.ToString());

				NextStage(State);
			}
			return true;
		}

		case 2:
		{
			// Fluctuate equidistant targets
			if (State.TargetA.IsValid() && State.TargetB.IsValid())
			{
				const float Wobble = 4.f * FMath::Sin(State.StageTime * 20.f);
				const FVector SpotA = State.ArenaOrigin + State.ForwardDir * (500.f + Wobble) - State.RightDir * 150.f;
				const FVector SpotB = State.ArenaOrigin + State.ForwardDir * (500.f - Wobble) + State.RightDir * 150.f;
				State.TargetA->SetActorLocation(SpotA);
				State.TargetB->SetActorLocation(SpotB);
			}

			// Sample Frostbitten for SampleWindowSeconds
			SampleTrack(State.CurrentTrack, State.StageTime);

			if (State.StageTime >= SampleWindowSeconds)
			{
				const float FlipsPerSec = State.CurrentTrack.Flips / State.StageTime;
				const bool bPass = FlipsPerSec <= MaxFlipsPerSecond;
				Check(State, bPass, FString::Printf(TEXT("Frostbitten facing jitter: %d flips in %.1fs (%.2f flips/s, limit %.2f, max step %.1f deg)"),
					State.CurrentTrack.Flips, State.StageTime, FlipsPerSec, MaxFlipsPerSecond, State.CurrentTrack.MaxStepDeg));

				if (AActor* Act = State.CurrentTrack.Subject.Get())
				{
					Act->Destroy();
				}

				// Prepare Stage 3: PMC Fighter at Origin + Forward * 1800 (1300 cm from targets, well in sight)
				UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
				if (!Pmc)
				{
					Check(State, false, TEXT("PmcSquadSubsystem missing"));
					return Finish(State, false);
				}

				const FVector PmcSpot = State.ArenaOrigin + State.ForwardDir * 1800.f + FVector(0.f, 0.f, 20.f);
				const FRotator PmcFacing = (-State.ForwardDir).Rotation();

				TArray<AOperativeCharacter*> PmcFighters = Pmc->SpawnSquad(1, PmcSpot, PmcFacing, 1, 42);
				AOperativeCharacter* PmcFighter = PmcFighters.IsValidIndex(0) ? PmcFighters[0] : nullptr;
				if (!PmcFighter)
				{
					Check(State, false, TEXT("Spawn PMC Fighter failed"));
					return Finish(State, false);
				}
				PmcFighter->bTacticalCeaseFire = true;

				State.CurrentTrack = FSubjectTrack();
				State.CurrentTrack.Name = TEXT("PMC Fighter");
				State.CurrentTrack.Subject = PmcFighter;

				UE_LOG(LogCodexTactics, Display, TEXT("[EnemyFacingJitterSmoke] Stage 3: Spawned PMC Fighter at %s"), *PmcSpot.ToString());

				NextStage(State);
			}
			return true;
		}

		case 3:
		{
			// Fluctuate equidistant targets
			if (State.TargetA.IsValid() && State.TargetB.IsValid())
			{
				const float Wobble = 4.f * FMath::Sin(State.StageTime * 20.f);
				const FVector SpotA = State.ArenaOrigin + State.ForwardDir * (500.f + Wobble) - State.RightDir * 150.f;
				const FVector SpotB = State.ArenaOrigin + State.ForwardDir * (500.f - Wobble) + State.RightDir * 150.f;
				State.TargetA->SetActorLocation(SpotA);
				State.TargetB->SetActorLocation(SpotB);
			}

			// Sample PMC Fighter for SampleWindowSeconds
			SampleTrack(State.CurrentTrack, State.StageTime);

			if (State.StageTime >= SampleWindowSeconds)
			{
				const float FlipsPerSec = State.CurrentTrack.Flips / State.StageTime;
				const bool bPass = FlipsPerSec <= MaxFlipsPerSecond;
				Check(State, bPass, FString::Printf(TEXT("PMC Fighter facing jitter: %d flips in %.1fs (%.2f flips/s, limit %.2f, max step %.1f deg)"),
					State.CurrentTrack.Flips, State.StageTime, FlipsPerSec, MaxFlipsPerSecond, State.CurrentTrack.MaxStepDeg));

				if (UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World))
				{
					Pmc->DestroyTeam(1);
				}

				return Finish(State, true);
			}
			return true;
		}

		default:
			break;
		}

		return Finish(State, false);
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
		}), 2.f, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.EnemyFacingJitterSmoke"),
		TEXT("Dev check: marksman, frostbitten, and PMC fighter facing jitter (yaw sign flips per second) under limit; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
