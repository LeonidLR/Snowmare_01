// Dev-only console command for a headless check of the Marksman enemy (TANDEM request 3) on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.MarksmanSmoke
// A marksman spawned 25 m ahead of the (unkillable) squad settles into a firing stance, aims with the beam and fires;
// hit in the fight while turned away (Sprint 06-G) he faces the shooter, takes a firing stance and fires back; put 8 m
// from the squad he retreats at a sprint.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Characters/MarksmanAnimInstance.h"
#include "Characters/MarksmanEnemyCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

namespace MarksmanSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		float Time = 0.f;
		int32 Stage = 0;
		bool bOk = true;
		bool bSawAim = false;
		bool bSawFiringStance = false;
		float RetreatStartDistance = 0.f;
		bool bSawSprint = false;
		int32 ShotsBeforeHit = 0;
		TWeakObjectPtr<AMarksmanEnemyCharacter> Marksman;
	};

	void Check(FState& State, bool bCondition, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bCondition ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.bOk &= bCondition;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.bOk && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("MarksmanSmoke"));
		return false;
	}

	const TCHAR* StateName(EMarksmanAIState S)
	{
		switch (S)
		{
		case EMarksmanAIState::Patrol: return TEXT("Patrol");
		case EMarksmanAIState::Engage: return TEXT("Engage");
		case EMarksmanAIState::Aim: return TEXT("Aim");
		case EMarksmanAIState::Retreat: return TEXT("Retreat");
		case EMarksmanAIState::Flank: return TEXT("Flank");
		default: return TEXT("Ambushed");
		}
	}

	bool Step(UWorld* World, FState& State)
	{
		State.Time += StepSeconds;
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		AMarksmanEnemyCharacter* Marksman = State.Marksman.Get();
		if (!Leader || (State.Stage > 0 && !Marksman))
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke FAIL: leader or marksman gone"));
			return Finish(State, false);
		}
		switch (State.Stage)
		{
		case 0:
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->HealthComponent->SetMaxHealth(100000.f);
				Member->bTacticalCeaseFire = true; // their hits would trigger the ambush reaction early
			}
			Marksman = Cast<AMarksmanEnemyCharacter>(World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Marksman,
				Leader->GetActorLocation() + Leader->GetActorForwardVector() * 2500.f + FVector(0.f, 0.f, 20.f)));
			Check(State, Marksman != nullptr, TEXT("spawned a Marksman (AMarksmanEnemyCharacter)"));
			if (!Marksman)
			{
				return Finish(State, false);
			}
			State.Marksman = Marksman;
			// Headless: refresh the bones so the pelvis height can be measured.
			Marksman->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			{
				const UMarksmanAnimInstance* Anim = Cast<UMarksmanAnimInstance>(Marksman->GetMesh()->GetAnimInstance());
				Check(State, Anim != nullptr && Marksman->GetMesh()->GetSkeletalMeshAsset() != nullptr,
					FString::Printf(TEXT("art Blueprint %s with UMarksmanAnimInstance"), *Marksman->GetClass()->GetName()));
			}
			State.Stage = 1;
			State.Time = 0.f;
			return true;
		case 1:
			// Hold at 25 m: firing stance, 2 s aim with the beam, the shot.
			State.bSawAim |= Marksman->IsAimingAtTarget();
			State.bSawFiringStance |= Marksman->IsAimingAtTarget() && Marksman->GetStance() != EOperativeStance::Standing
				&& Marksman->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()
					== MarksmanAIRules::GetHalfHeight(Marksman->MarksmanConfig, Marksman->GetStance());
			if (Marksman->GetShotsFired() == 0 && State.Time < 10.f)
			{
				return true;
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke marksman at %.0f m: state %s, stance %d, half height %.0f"),
				FVector::Dist(Marksman->GetActorLocation(), Leader->GetActorLocation()) / 100.f, StateName(Marksman->GetAIState()),
				static_cast<int32>(Marksman->GetStance()), Marksman->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight());
			Check(State, State.bSawAim, TEXT("aimed (telegraph) before the shot"));
			Check(State, State.bSawFiringStance, TEXT("aimed crouched / prone with the lowered capsule"));
			Check(State, Marksman->GetShotsFired() >= 1, FString::Printf(TEXT("fired (%.1f s)"), State.Time));
			if (const UMarksmanAnimInstance* Anim = Cast<UMarksmanAnimInstance>(Marksman->GetMesh()->GetAnimInstance()))
			{
				// The stance's look: the transition clip into it, the fire clip, the pelvis height of a lying body.
				const float Pelvis = Marksman->GetMesh()->GetBoneLocation(TEXT("pelvis")).Z
					- (Marksman->GetActorLocation().Z - Marksman->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
				const bool bProne = Marksman->GetStance() == EOperativeStance::Prone;
				// The shot's montage (fire clip) is the active one on the stance's slot.
				const UAnimMontage* Montage = Anim->GetCurrentActiveMontage();
				const bool bFireClip = Montage && Montage->SlotAnimTracks.Num() > 0 && !Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.IsEmpty()
					&& (Anim->AttackAnimations.Contains(Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference())
						|| Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference() == Anim->CrouchFireAnimation);
				Check(State, Anim->bIsProne == bProne && (!bProne || (Anim->GetLastTransition() != nullptr && Pelvis < 45.f)) && bFireClip,
					FString::Printf(TEXT("animation: stance %d, transition %s, fire clip playing %d, pelvis %.0f cm above the feet"),
						static_cast<int32>(Marksman->GetStance()), Anim->GetLastTransition() ? *Anim->GetLastTransition()->GetName() : TEXT("-"),
						bFireClip ? 1 : 0, Pelvis));
			}
			{
				// A hit in the fight while turned away: he answers (no blind ambush).
				Marksman->SetActorRotation(Marksman->GetActorRotation() + FRotator(0.f, 150.f, 0.f));
				State.ShotsBeforeHit = Marksman->GetShotsFired();
				FDamageSpec Spec;
				Spec.Amount = 5.f;
				Spec.AttackerSource = TEXT("smoke");
				Marksman->GetHealthComponent()->ApplyDamage(Spec);
			}
			{
				const float Yaw = (Leader->GetActorLocation() - Marksman->GetActorLocation()).Rotation().Yaw;
				const float Off = FMath::Abs(FRotator::NormalizeAxis(Marksman->GetActorRotation().Yaw - Yaw));
				Check(State, Off < 15.f && Marksman->IsAimingAtTarget() && Marksman->GetStance() != EOperativeStance::Standing,
					FString::Printf(TEXT("hit in the fight: faces the shooter (%.0f deg off), aims back (%s), stance %d"), Off,
						StateName(Marksman->GetAIState()), static_cast<int32>(Marksman->GetStance())));
			}
			State.Stage = 2;
			State.Time = 0.f;
			return true;
		case 2:
			if (Marksman->GetShotsFired() == State.ShotsBeforeHit && State.Time < 4.f)
			{
				return true;
			}
			Check(State, Marksman->GetShotsFired() > State.ShotsBeforeHit,
				FString::Printf(TEXT("returned fire %.1f s after the hit (%s)"), State.Time, StateName(Marksman->GetAIState())));
			State.Stage = 3;
			State.Time = 0.f;
			return true;
		case 3:
			if (Marksman->GetAIState() == EMarksmanAIState::Flank && State.Time < 9.f)
			{
				return true;
			}
			// Too close: 8 m from the leader.
			Marksman->TeleportTo(Leader->GetActorLocation() + Leader->GetActorForwardVector() * 800.f + FVector(0.f, 0.f, 20.f),
				Marksman->GetActorRotation());
			State.Stage = 4;
			State.Time = 0.f;
			return true;
		case 4:
			if (State.Time < 0.3f)
			{
				State.RetreatStartDistance = FVector::Dist2D(Marksman->GetActorLocation(), Leader->GetActorLocation());
				return true;
			}
			State.bSawSprint |= Marksman->GetAIState() == EMarksmanAIState::Retreat && Marksman->GetVelocity().Size2D() > 400.f;
			if (State.Time < 3.5f)
			{
				return true;
			}
			{
				const float Now = FVector::Dist2D(Marksman->GetActorLocation(), Leader->GetActorLocation());
				Check(State, State.bSawSprint && Now > State.RetreatStartDistance + 300.f,
					FString::Printf(TEXT("8 m away: retreats at a sprint (%.0f -> %.0f m, now %s)"), State.RetreatStartDistance / 100.f,
						Now / 100.f, StateName(Marksman->GetAIState())));
			}
			return Finish(State, true);
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
		TEXT("CodexTactics.MarksmanSmoke"),
		TEXT("Dev check: Marksman stance / aim / shot, ambush prone + flank, retreat under 12 m; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
