// Dev-only console command for a headless check of moving while shooting in the real-time fight on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.CombatMoveSmoke
// Godot player.gd combat_facing_direction / _face_movement_target / _process_combat_shooting: with a target in reach the
// commander shoots; a plain move order walks him sideways while he keeps facing the enemy with the barrel on it
// (aim pose on the upper body, standing legs); a sprint order turns him to the movement, lowers the rifle and stops the
// fire; once he stops he faces the enemy and shoots again. Tracers leave from the weapon's muzzle.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

namespace CombatMoveSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		int32 Shots = 0;
		int32 ShotsAtStageStart = 0;
		TWeakObjectPtr<AEnemyCharacter> Brute;
		// Per-stage samples.
		int32 Samples = 0;
		int32 FacingSamples = 0;
		int32 AimingSamples = 0;
		int32 SidewaysSamples = 0;
		float MaxBarrelError = 0.f;
		float MaxPlayRate = 0.f;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("CombatMoveSmoke"));
		return false;
	}

	void NextStage(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
		State.ShotsAtStageStart = State.Shots;
		State.Samples = State.FacingSamples = State.AimingSamples = State.SidewaysSamples = 0;
		State.MaxBarrelError = State.MaxPlayRate = 0.f;
	}

	/** Barrel direction vs the direction to the target, degrees (yaw only). */
	float BarrelError(const AOperativeCharacter* Commander, const AActor* Target)
	{
		const FVector Barrel = Commander->WeaponMesh->GetComponentTransform().TransformVectorNoScale(Commander->MuzzleOffset.GetSafeNormal());
		const float ToTarget = (Target->GetActorLocation() - Commander->GetActorLocation()).Rotation().Yaw;
		return FMath::Abs(FRotator::NormalizeAxis(Barrel.Rotation().Yaw - ToTarget));
	}

	void Sample(FState& State, const AOperativeCharacter* Commander)
	{
		const UOperativeAnimInstance* Anim = Cast<UOperativeAnimInstance>(Commander->GetMesh()->GetAnimInstance());
		const FVector Velocity = Commander->GetVelocity();
		++State.Samples;
		State.FacingSamples += Commander->IsFacingCombatTarget() ? 1 : 0;
		State.AimingSamples += Anim && Anim->bIsAiming ? 1 : 0;
		if (Velocity.Size2D() > 50.f)
		{
			const float Angle = FMath::Abs(FRotator::NormalizeAxis(Velocity.Rotation().Yaw - Commander->GetActorRotation().Yaw));
			State.SidewaysSamples += Angle > 50.f ? 1 : 0;
			// The legs' play rate: the standing blend space (StandPlayRate) carries the aimed walk too.
			if (Anim)
			{
				State.MaxPlayRate = FMath::Max(State.MaxPlayRate, Anim->bIsAiming ? Anim->StandPlayRate : FMath::Max(Anim->StandPlayRate, 1.f));
			}
		}
		if (State.Brute.IsValid() && Anim && Anim->bIsAiming)
		{
			State.MaxBarrelError = FMath::Max(State.MaxBarrelError, BarrelError(Commander, State.Brute.Get()));
		}
	}

	bool Step(UWorld* World, FState& State)
	{
		State.StageTime += StepSeconds;
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		AOperativeCharacter* Commander = Squad ? Squad->GetLeader() : nullptr;
		if (!Commander || !Flow || State.StageTime > 15.f)
		{
			Check(State, false, FString::Printf(TEXT("commander / stage %d timeout"), State.Stage));
			return Finish(State, false);
		}
		const FTransform Layout = SmokeUtils::LayoutTransform(World);
		switch (State.Stage)
		{
		case 0:
		{
			// A real-time wave; its enemies frozen far away, one frozen brute (220 hp, kinetic armour) 8 m ahead.
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->CustomTimeDilation = 0.f;
				It->SetActorLocation(It->GetActorLocation() + FVector(0.f, 0.f, -5000.f), false, nullptr, ETeleportType::TeleportPhysics);
			}
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member != Commander)
				{
					Member->TeleportTo(SmokeUtils::LevelPoint(World, FVector(-900.f, 600.f * Member->SquadIndex, 100.f)), Member->GetActorRotation(), false, true);
					Member->bTacticalCeaseFire = true;
				}
			}
			Commander->TeleportTo(SmokeUtils::LevelPoint(World, FVector(0.f, 0.f, 100.f)), Layout.Rotator(), false, true);
			State.Brute = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Brute,
				SmokeUtils::LevelPoint(World, FVector(800.f, 0.f, 100.f)), Layout.Rotator() + FRotator(0.f, 180.f, 0.f));
			if (!State.Brute.IsValid())
			{
				Check(State, false, TEXT("brute spawned"));
				return Finish(State, false);
			}
			State.Brute->CustomTimeDilation = 0.f;
			// No reload pauses or a kill in the middle of the checks.
			State.Brute->GetHealthComponent()->SetMaxHealth(100000.f);
			Commander->CurrentClip = 1000;
			// Headless nothing is rendered: refresh the bones so the rifle (attached to the hand) follows the real pose.
			Commander->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			Commander->OnWeaponFiredNative.AddLambda([&State](AOperativeCharacter*, AActor*, bool) { ++State.Shots; });
			Check(State, Flow->GetPhase() == ECodexGamePhase::WaveCombat && Flow->GetCombatMode() == ECodexCombatMode::RealTime, TEXT("real-time wave"));
			NextStage(State);
			return true;
		}
		case 1:
			// Standing still: he engages the brute.
			if (State.StageTime < 2.f)
			{
				Sample(State, Commander);
				return true;
			}
			Check(State, State.Shots > State.ShotsAtStageStart && Commander->IsFacingCombatTarget(),
				FString::Printf(TEXT("standing: %d shots, facing the brute"), State.Shots - State.ShotsAtStageStart));
			Check(State, State.MaxBarrelError <= 12.f, FString::Printf(TEXT("standing: barrel on the brute (max error %.1f deg, body offset %.1f)"),
				State.MaxBarrelError, Commander->GetBarrelYawOffset()));
			{
				const FVector Muzzle = Commander->GetWeaponMuzzleLocation();
				const FBox WeaponBox = Commander->WeaponMesh->Bounds.GetBox().ExpandBy(5.f);
				Check(State, WeaponBox.IsInside(Muzzle), FString::Printf(TEXT("tracer origin on the rifle (%.0f cm from the body line-of-fire point)"),
					FVector::Dist(Muzzle, Commander->GetMuzzleLocation())));
			}
			// A plain order 5 m to the side.
			Commander->OrderMoveTo(SmokeUtils::LevelPoint(World, FVector(0.f, 500.f, 100.f)), false);
			NextStage(State);
			return true;
		case 2:
			Sample(State, Commander);
			if (State.StageTime < 2.f)
			{
				return true;
			}
			Check(State, State.SidewaysSamples >= 5 && State.FacingSamples * 10 >= State.Samples * 9, FString::Printf(
				TEXT("walk order: sideways %d of %d samples, facing the brute %d"), State.SidewaysSamples, State.Samples, State.FacingSamples));
			Check(State, State.Shots > State.ShotsAtStageStart && State.AimingSamples * 2 >= State.Samples, FString::Printf(
				TEXT("walk order: keeps shooting (%d shots), aiming %d of %d"), State.Shots - State.ShotsAtStageStart, State.AimingSamples, State.Samples));
			Check(State, State.MaxBarrelError <= 15.f && State.MaxPlayRate <= 1.5f, FString::Printf(
				TEXT("walk order: barrel on the brute (max error %.1f deg), legs play rate %.2f"), State.MaxBarrelError, State.MaxPlayRate));
			// A sprint order back past the start.
			Commander->OrderMoveTo(SmokeUtils::LevelPoint(World, FVector(0.f, -500.f, 100.f)), true);
			NextStage(State);
			return true;
		case 3:
			// Skip the first 0.3 s (the shot in flight, the turn to the run).
			if (State.StageTime > 0.3f)
			{
				Sample(State, Commander);
			}
			if (State.StageTime < 1.6f)
			{
				return true;
			}
			{
				const int32 Shots = State.Shots - State.ShotsAtStageStart;
				Check(State, Commander->IsSprinting() || Commander->GetVelocity().Size2D() > 300.f, TEXT("sprint order: running"));
				Check(State, Shots <= 1 && State.AimingSamples == 0 && State.FacingSamples == 0 && State.SidewaysSamples == 0,
					FString::Printf(TEXT("sprint: %d shots, aiming %d, facing the brute %d, sideways %d of %d samples"),
						Shots, State.AimingSamples, State.FacingSamples, State.SidewaysSamples, State.Samples));
			}
			NextStage(State);
			return true;
		case 4:
			// Arrived: back to facing the brute and firing.
			if (Commander->GetVelocity().Size2D() > 5.f || State.StageTime < 2.5f)
			{
				return true;
			}
			Check(State, Commander->IsFacingCombatTarget() && State.Shots > State.ShotsAtStageStart,
				FString::Printf(TEXT("after the sprint: facing the brute, %d shots"), State.Shots - State.ShotsAtStageStart));
			return Finish(State, true);
		default:
			return Finish(State, true);
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
		TEXT("CodexTactics.CombatMoveSmoke"),
		TEXT("Dev check: walking sideways while facing / shooting, sprint stops the fire and the aim; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
