// Dev-only check of the grenade-throw clips (2026-10-07; UE-only, the clips are the user's Grenade_01root_root_*_UE set):
//   Scripts/smoke.ps1 -Command CodexTactics.GrenadeAnimSmoke -Log Smoke-GrenadeAnim.log
// For each case (walking, sprinting, crouched walking, crawling) the leader moves, a grenade is thrown at a point to the
// side (tiny blast) and, sampled every 0.05 s: the right clip plays on UpperBodySlot (IsPlayingSlotAnimation, slot
// weight > 0.9), the full-body slot stays out of it, the left-hand IK and aim-offset alphas are 0 mid-throw, the legs keep
// the locomotion (the operative keeps moving, the feet keep swinging), exactly one grenade exists and it leaves the hand
// (stops being hidden) at the clip release time +- 0.1 s. Needs the user ABP_Operative with the "Fire" upper-body slot
// in a layered blend from spine_01 and the clips from Scripts/Editor/setup_operative_grenade_animation.py.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Animation/AnimMontage.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/GrenadeActor.h"
#include "Combat/GrenadeRules.h"
#include "Combat/GrenadeSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

namespace GrenadeAnimSmoke
{
	constexpr float StepSeconds = 0.05f;

	struct FCase
	{
		const TCHAR* Name;
		EOperativeStance Stance;
		bool bSprint;
		float MinSpeed;
	};

	const FCase Cases[] = {
		{ TEXT("walk"), EOperativeStance::Standing, false, 150.f },
		{ TEXT("run"), EOperativeStance::Standing, true, 350.f },
		{ TEXT("crouch"), EOperativeStance::Crouching, false, 80.f },
		{ TEXT("prone"), EOperativeStance::Prone, false, 25.f },
	};

	struct FState
	{
		int32 CaseIndex = 0;
		int32 Phase = 0; // 0 setup, 1 stance settles, 2 moving, 3 throw sampled, 4 wait for the blast
		float Time = 0.f;
		float PhaseTime = 0.f;
		int32 Failures = 0;
		TWeakObjectPtr<AGrenadeActor> Grenade;
		TWeakObjectPtr<const UAnimSequenceBase> Clip;
		float ExpectedRelease = 0.f;
		float ReleaseTime = -1.f;
		float MaxUpperWeight = 0.f;
		float MaxFullBodyWeight = 0.f;
		bool bClipOnSlot = false;
		int32 MaxGrenades = 0;
		float MinSpeed = 1.0e9f;
		float MaxIK = 0.f;
		float MaxAim = 0.f;
		bool bMidThrowSeen = false;
		float FootMin = 1.0e9f;
		float FootMax = -1.0e9f;
		FVector Start = FVector::ZeroVector;

		/** Clears the per-case measurements. */
		void ResetCase()
		{
			Grenade.Reset();
			Clip.Reset();
			ExpectedRelease = 0.f;
			ReleaseTime = -1.f;
			MaxUpperWeight = 0.f;
			MaxFullBodyWeight = 0.f;
			bClipOnSlot = false;
			MaxGrenades = 0;
			MinSpeed = 1.0e9f;
			MaxIK = 0.f;
			MaxAim = 0.f;
			bMidThrowSeen = false;
			FootMin = 1.0e9f;
			FootMax = -1.0e9f;
		}
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("GrenadeAnimSmoke"));
		return false;
	}

	int32 GrenadesInWorld(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<AGrenadeActor> It(World); It; ++It)
		{
			Count += IsValid(*It) ? 1 : 0;
		}
		return Count;
	}

	const UAnimSequenceBase* ExpectedClip(const UOperativeAnimInstance& Anim, int32 CaseIndex)
	{
		switch (CaseIndex)
		{
		case 0: return Anim.GrenadeThrowWalkAnimation;
		case 1: return Anim.GrenadeThrowRunAnimation;
		case 2: return Anim.GrenadeThrowCrouchAnimation;
		default: return Anim.GrenadeThrowProneAnimation;
		}
	}

	float ExpectedReleaseSeconds(const UOperativeAnimInstance& Anim, int32 CaseIndex)
	{
		switch (CaseIndex)
		{
		case 0: return Anim.GrenadeThrowWalkReleaseSeconds;
		case 1: return Anim.GrenadeThrowRunReleaseSeconds;
		case 2: return Anim.GrenadeThrowCrouchReleaseSeconds;
		default: return Anim.GrenadeThrowProneReleaseSeconds;
		}
	}

	bool NextCase(FState& State)
	{
		++State.CaseIndex;
		State.Phase = 0;
		State.PhaseTime = 0.f;
		return true;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.PhaseTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 150.f)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in case %d phase %d"), State.CaseIndex, State.Phase);
			return World ? Finish(State, false) : false;
		}
		if (State.Time < 3.f)
		{
			return true; // the squad settles; the layout placement runs at 0.5 s
		}
		if (State.CaseIndex >= UE_ARRAY_COUNT(Cases))
		{
			return Finish(State, true);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UGrenadeSubsystem* Grenades = World->GetSubsystem<UGrenadeSubsystem>();
		AOperativeCharacter* Op = Squad ? Squad->GetLeader() : nullptr;
		UOperativeAnimInstance* Anim = Op && Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr;
		if (!Op || !Anim || !Grenades)
		{
			Check(State, false, TEXT("leader, anim instance and grenade subsystem present"));
			return Finish(State, false);
		}
		const FCase& Case = Cases[State.CaseIndex];
		Op->ColdLevel = 0.f;
		Op->bTacticalCeaseFire = true;
		Op->GrenadesCount = FMath::Max(Op->GrenadesCount, 2);
		Op->GrenadeEffectRadius = 40.f; // a tiny blast: nobody of the squad is hurt
		Op->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

		switch (State.Phase)
		{
		case 0:
			State.ResetCase();
			SmokeUtils::PlaceSquadAtTestStart(World);
			Op->SetSprinting(false);
			Op->SetStance(Case.Stance);
			State.Phase = 1;
			State.PhaseTime = 0.f;
			return true;
		case 1:
		{
			if (State.PhaseTime < 3.f)
			{
				return true; // the stance clip (lying down / crouching) plays out
			}
			State.Start = Op->GetActorLocation();
			const EOperativeOrderResult Order = Op->OrderMoveTo(SmokeUtils::ClearPoint(World, State.Start, SmokeUtils::LevelPoint(World, FVector(2600.f, 0.f, 100.f))), Case.bSprint);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s move order %s, stance %d"), Case.Name, *UEnum::GetValueAsString(Order), static_cast<int32>(Op->GetStance()));
			State.Phase = 2;
			State.PhaseTime = 0.f;
			return true;
		}
		case 2:
		{
			if (Op->GetVelocity().Size2D() < Case.MinSpeed)
			{
				if (State.PhaseTime > 6.f)
				{
					Check(State, false, FString::Printf(TEXT("%s: reached %.0f cm/s while moving (speed %.0f)"), Case.Name, Case.MinSpeed, Op->GetVelocity().Size2D()));
					return NextCase(State);
				}
				return true;
			}
			const FVector Side = FVector::CrossProduct(Op->GetActorForwardVector().GetSafeNormal2D(), FVector::UpVector);
			AGrenadeActor* Grenade = Grenades->ThrowAt(Op, Op->GetActorLocation() + Side * 700.f);
			State.Grenade = Grenade;
			State.Clip = ExpectedClip(*Anim, State.CaseIndex);
			State.ExpectedRelease = GrenadeRules::ReleaseDelay(State.Clip.IsValid() ? State.Clip->GetPlayLength() : 0.f, ExpectedReleaseSeconds(*Anim, State.CaseIndex));
			Check(State, Grenade != nullptr && State.Clip.IsValid(), FString::Printf(TEXT("%s: thrown while moving (%.0f cm/s), clip %s"), Case.Name,
				Op->GetVelocity().Size2D(), State.Clip.IsValid() ? *State.Clip->GetName() : TEXT("none")));
			Check(State, FMath::IsNearlyEqual(Op->GrenadeThrowDuration, State.Clip.IsValid() ? State.Clip->GetPlayLength() : -1.f, 0.01f),
				FString::Printf(TEXT("%s: GrenadeThrowDuration = clip length %.2f s"), Case.Name, Op->GrenadeThrowDuration));
			Check(State, FMath::IsNearlyEqual(Op->GrenadeReleaseSeconds, ExpectedReleaseSeconds(*Anim, State.CaseIndex), 0.001f),
				FString::Printf(TEXT("%s: release time %.2f s handed to the grenade"), Case.Name, Op->GrenadeReleaseSeconds));
			State.Phase = 3;
			State.PhaseTime = 0.f;
			return true;
		}
		case 3:
		{
			const float T = State.PhaseTime;
			const float ClipLength = State.Clip.IsValid() ? State.Clip->GetPlayLength() : 0.f;
			UAnimMontage* Montage = nullptr;
			if (State.Clip.IsValid() && Anim->IsPlayingSlotAnimation(State.Clip.Get(), Anim->UpperBodySlot, Montage))
			{
				State.bClipOnSlot = true;
			}
			if (T >= 0.3f && T <= ClipLength - 0.3f)
			{
				State.MaxUpperWeight = FMath::Max(State.MaxUpperWeight, Anim->GetSlotMontageGlobalWeight(Anim->UpperBodySlot));
				State.MaxFullBodyWeight = FMath::Max(State.MaxFullBodyWeight, Anim->GetSlotMontageGlobalWeight(Anim->FullBodySlot));
			}
			State.MaxGrenades = FMath::Max(State.MaxGrenades, GrenadesInWorld(World));
			if (T >= 0.5f && T <= ClipLength - 0.3f)
			{
				State.MinSpeed = FMath::Min(State.MinSpeed, static_cast<float>(Op->GetVelocity().Size2D()));
				State.MaxIK = FMath::Max(State.MaxIK, Anim->LeftHandIKAlpha);
				State.MaxAim = FMath::Max(State.MaxAim, Anim->AimOffsetAlpha);
				State.bMidThrowSeen = true;
				const FVector Foot = Op->GetActorTransform().InverseTransformPosition(Op->GetMesh()->GetBoneLocation(TEXT("foot_r")));
				State.FootMin = FMath::Min(State.FootMin, static_cast<float>(Foot.X));
				State.FootMax = FMath::Max(State.FootMax, static_cast<float>(Foot.X));
			}
			if (State.ReleaseTime < 0.f && State.Grenade.IsValid() && !State.Grenade->IsHidden())
			{
				State.ReleaseTime = T;
			}
			if (T < ClipLength + 0.1f)
			{
				return true;
			}
			Check(State, State.bClipOnSlot, FString::Printf(TEXT("%s: the throw clip played on the upper-body slot '%s'"), Case.Name, *Anim->UpperBodySlot.ToString()));
			Check(State, State.MaxUpperWeight > 0.9f, FString::Printf(TEXT("%s: upper-body slot weight %.2f > 0.9"), Case.Name, State.MaxUpperWeight));
			Check(State, State.MaxFullBodyWeight < 0.05f, FString::Printf(TEXT("%s: the full-body slot stays out (%.2f)"), Case.Name, State.MaxFullBodyWeight));
			Check(State, State.bMidThrowSeen && State.MaxIK < 0.05f && State.MaxAim < 0.05f,
				FString::Printf(TEXT("%s: left-hand IK alpha %.2f and aim offset alpha %.2f are 0 mid-throw"), Case.Name, State.MaxIK, State.MaxAim));
			Check(State, State.bMidThrowSeen && State.MinSpeed > Case.MinSpeed * 0.5f && FVector::Dist2D(State.Start, Op->GetActorLocation()) > 100.f,
				FString::Printf(TEXT("%s: the legs keep the locomotion (min speed %.0f cm/s, moved %.0f cm)"), Case.Name, State.MinSpeed,
					FVector::Dist2D(State.Start, Op->GetActorLocation())));
			Check(State, State.FootMax - State.FootMin > 6.f, FString::Printf(TEXT("%s: the feet keep swinging (foot_r range %.1f cm)"), Case.Name, State.FootMax - State.FootMin));
			Check(State, State.MaxGrenades == 1, FString::Printf(TEXT("%s: one grenade only (max %d in the world)"), Case.Name, State.MaxGrenades));
			Check(State, State.ReleaseTime >= 0.f && FMath::Abs(State.ReleaseTime - State.ExpectedRelease) <= 0.1f,
				FString::Printf(TEXT("%s: the grenade left the hand at %.2f s (expected %.2f s, clip %.2f s)"), Case.Name, State.ReleaseTime, State.ExpectedRelease, ClipLength));
			Op->StopOperative();
			State.Phase = 4;
			State.PhaseTime = 0.f;
			return true;
		}
		default:
			if (GrenadesInWorld(World) > 0 && State.PhaseTime < 8.f)
			{
				return true;
			}
			return NextCase(State);
		}
	}

	void Run(const TArray<FString>&, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		{
			FTimerHandle PlaceHandle;
			TWeakObjectPtr<UWorld> PlaceWorld(World);
			World->GetTimerManager().SetTimer(PlaceHandle, FTimerDelegate::CreateLambda([PlaceWorld]()
			{
				SmokeUtils::PlaceSquadAtTestStart(PlaceWorld.Get());
			}), 0.5f, false);
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.GrenadeAnimSmoke"),
		TEXT("Dev check: grenade-throw clips on the upper-body slot while walking / running / crouched / crawling, release time, one grenade; PASS / FAIL."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
