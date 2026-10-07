// Dev-only check of the aim offset feed (2026-10-07; UE-only, no Godot reference):
//   Scripts/smoke.ps1 -Command CodexTactics.AimOffsetSmoke -Log Smoke-AimOffset.log
// The commander in the open, a hound on the ground (first 80 m away = no target, then 6 m ahead) (his body centre ~ 0.8 m below the muzzle), nothing else.
// Checks: no target -> AimPitch 0; the alpha blends to 1; after a shot at the low hound AimPitch is negative and within 3 deg
// of the geometric pitch (muzzle -> the hound centre), and the aim point (= the tracer end point) is the hound body
// centre, not eye level; a hound 2 m ABOVE gives a positive pitch; after AimTargetHoldSeconds with no shot the pitch eases
// back to 0; sprinting fades the alpha to 0. The ABP node itself is the user graph (HANDOFF section 6 "ABP: aim offset").

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/AimOffsetRules.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"

namespace AimOffsetSmoke
{
	struct FState
	{
		int32 Stage = 0;
		float StageTime = 0.f;
		float Time = 0.f;
		int32 Failures = 0;
		FVector P = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		float GroundZ = 0.f;
		TWeakObjectPtr<AOperativeCharacter> Op;
		TWeakObjectPtr<AEnemyCharacter> Hound;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State)
	{
		if (AOperativeCharacter* Op = State.Op.Get())
		{
			Op->bForceHitForTesting = false;
			Op->SetSprinting(false);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("AimOffsetSmoke"));
		return false;
	}

	void PlaceHound(FState& State, const FVector& Where, float ExtraZ)
	{
		if (AEnemyCharacter* Hound = State.Hound.Get())
		{
			Hound->SetActorLocation(FVector(Where.X, Where.Y, State.GroundZ + Hound->GetSimpleCollisionHalfHeight() + 2.f + ExtraZ));
		}
	}

	/** Checks the aim feed against the geometry (a shot at the hound was fired a moment ago). */
	void CheckAim(FState& State, const TCHAR* Label, bool bExpectDown)
	{
		AOperativeCharacter* Op = State.Op.Get();
		const AEnemyCharacter* Hound = State.Hound.Get();
		const UOperativeAnimInstance* Anim = Op && Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr;
		if (!Op || !Hound || !Anim)
		{
			Check(State, false, FString::Printf(TEXT("%s: operative / hound / anim instance present"), Label));
			return;
		}
		const FVector Muzzle = Op->GetMuzzleLocation();
		const float Expected = AimOffsetRules::PitchToTarget(Muzzle, Hound->GetActorLocation(), Anim->AimPitchClampDegrees);
		FVector AimPoint = FVector::ZeroVector;
		const bool bAim = Op->GetAimTargetPoint(AimPoint);
		UE_LOG(LogCodexTactics, Display, TEXT("AimOffset %s: muzzle z %.0f, hound centre z %.0f, expected pitch %.1f, AimPitch %.1f (target %.1f), alpha %.2f"),
			Label, Muzzle.Z, Hound->GetActorLocation().Z, Expected, Anim->AimPitch, Anim->AimPitchTarget, Anim->AimOffsetAlpha);
		Check(State, bAim && AimPoint.Equals(Hound->GetActorLocation(), 1.f),
			FString::Printf(TEXT("%s: the aim point (tracer end) is the hound body centre z %.0f, not eye level z %.0f"), Label, AimPoint.Z, Muzzle.Z));
		Check(State, Anim->bHasAimTarget && Anim->bAimOffsetActive, FString::Printf(TEXT("%s: aim target known, offset active"), Label));
		Check(State, FMath::Abs(Anim->AimPitch - Expected) <= 3.f, FString::Printf(TEXT("%s: AimPitch %.1f within 3 deg of %.1f"), Label, Anim->AimPitch, Expected));
		Check(State, bExpectDown ? Anim->AimPitch < -5.f : Anim->AimPitch > 5.f,
			FString::Printf(TEXT("%s: pitch %s"), Label, bExpectDown ? TEXT("negative (down)") : TEXT("positive (up)")));
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += 0.05f;
		State.StageTime += 0.05f;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 60.f)
		{
			return World ? Finish(State) : false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Op = State.Op.Get();
		if (Op)
		{
			Op->ColdLevel = 0.f;
			Op->bTacticalCeaseFire = true;
		}
		const UOperativeAnimInstance* Anim = Op && Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr;
		auto Next = [&State]()
		{
			++State.Stage;
			State.StageTime = 0.f;
			return true;
		};
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			Op = Squad->GetLeader();
			State.Op = Op;
			State.F = Op->GetActorForwardVector().GetSafeNormal2D();
			State.P = Op->GetActorLocation();
			State.GroundZ = State.P.Z - Op->GetSimpleCollisionHalfHeight();
			Op->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			AEnemyCharacter* Hound = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound,
				FVector(State.P.X, State.P.Y, State.GroundZ + 100.f) + State.F * 600.f, (-State.F).Rotation());
			if (Hound)
			{
				Hound->CustomTimeDilation = 0.f;
				Hound->SetActorTickEnabled(false);
				Hound->GetHealthComponent()->SetMaxHealth(100000.f);
			}
			State.Hound = Hound;
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				if (*It != Hound)
				{
					It->Destroy();
				}
			}
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->HealthComponent->SetMaxHealth(100000.f);
				if (Member != Op)
				{
					Member->StopOperative();
					Member->TeleportTo(State.P - State.F * 400.f, State.F.Rotation(), false, true);
				}
			}
			Check(State, Hound != nullptr, TEXT("hound spawned"));
			PlaceHound(State, State.P + State.F * 8000.f, 0.f); // out of weapon range: really no target yet
			return Next();
		}
		case 1:
			if (State.StageTime < 1.f || !Anim)
			{
				return true;
			}
			{
				FVector Unused;
				Check(State, !Op->GetAimTargetPoint(Unused) && !Anim->bHasAimTarget && FMath::IsNearlyZero(Anim->AimPitch, 0.01f),
					FString::Printf(TEXT("no target: AimPitch %.2f = 0"), Anim->AimPitch));
				Check(State, Anim->AimOffsetAlpha >= 0.99f && Anim->bAimOffsetActive,
					FString::Printf(TEXT("idle, standing: alpha %.2f = 1"), Anim->AimOffsetAlpha));
			}
			PlaceHound(State, State.P + State.F * 600.f, 0.f);
			Op->bForceHitForTesting = true;
			Op->ShootAtTarget(State.Hound.Get(), 0.f);
			return Next();
		case 2:
			if (State.StageTime < 0.7f)
			{
				return true;
			}
			CheckAim(State, TEXT("hound low, 6 m"), true);
			PlaceHound(State, State.P + State.F * 300.f, 200.f);
			Op->ShootAtTarget(State.Hound.Get(), 0.f);
			return Next();
		case 3:
			if (State.StageTime < 0.7f)
			{
				return true;
			}
			CheckAim(State, TEXT("hound 2 m above, 3 m"), false);
			PlaceHound(State, State.P + State.F * 8000.f, 0.f); // out of range again: the combat target is dropped
			return Next();
		case 4:
			// No more shots: after the hold the target is gone and the pitch eases back to 0.
			if (State.StageTime < Op->AimTargetHoldSeconds + 1.5f || !Anim)
			{
				return true;
			}
			Check(State, !Anim->bHasAimTarget && FMath::Abs(Anim->AimPitch) < 1.f,
				FString::Printf(TEXT("hold over, no target: AimPitch %.2f -> 0"), Anim->AimPitch));
			Op->SetSprinting(true);
			return Next();
		case 5:
			if (State.StageTime < 0.7f || !Anim)
			{
				return true;
			}
			Check(State, Anim->bIsSprinting && Anim->AimOffsetAlpha <= 0.01f && !Anim->bAimOffsetActive,
				FString::Printf(TEXT("sprinting: alpha %.2f = 0 (sprint flag %d)"), Anim->AimOffsetAlpha, Anim->bIsSprinting ? 1 : 0));
			Op->SetSprinting(false);
			return Finish(State);
		default:
			return Finish(State);
		}
	}

	void Run(const TArray<FString>&, UWorld* World)
	{
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), 0.05f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.AimOffsetSmoke"),
		TEXT("Dev check: AimPitch / AimOffsetAlpha toward a low and a high target; PASS / FAIL."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
