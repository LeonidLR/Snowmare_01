// Dev-only console command for headless stance checks on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.StanceSmoke
// The squad must be spawned from BP_Operative. The leader goes crouch -> prone -> standing: the capsule takes the
// stance height (Godot 1.3 / 0.7 / 2.0 m ratios) while the feet stay on the ground. With prone clips set in
// ABP_Operative, crouch -> prone and prone -> standing play their transition clips and a prone shot plays the
// full-body prone fire clip. Then (user decision 2026-10-01): a crawling operative told to crouch stops first and
// rises in place; a sprint order (double click) to a prone operative stands him up in place, then he runs.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

namespace StanceSmoke
{
	constexpr float NavWarmupSeconds = 3.f;
	constexpr float FeetTolerance = 3.f;

	struct FState
	{
		int32 Second = 0;
		float StartFeetZ = 0.f;
		bool bOk = true;
		FVector CrawlStart = FVector::ZeroVector;
		bool bRiseOk = false;
		/** Pelvis height range while prone and firing / aiming (a jump to a standing pose shows as > ProneMaxPelvis). */
		float FireMinPelvis = TNumericLimits<float>::Max();
		float FireMaxPelvis = 0.f;
		FString FireSamples;
	};

	/** Pelvis above the feet stays below this while lying (lying ~15 cm, crouched ~50, standing ~90). */
	constexpr float ProneMaxPelvis = 40.f;

	float FeetZ(const AOperativeCharacter* Operative)
	{
		return Operative->GetActorLocation().Z - Operative->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}

	/** Pelvis bone above the feet, cm: the pose the graph really outputs (prone ~15, crouched ~50, standing ~90). */
	float PelvisHeight(const AOperativeCharacter* Operative)
	{
		return Operative->GetMesh()->GetBoneLocation(TEXT("pelvis")).Z - FeetZ(Operative);
	}

	bool Check(FState& State, AOperativeCharacter* Leader, EOperativeStance Expected)
	{
		const float Half = Leader->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
		const float ExpectedHalf = FMath::Max(Leader->GetStanceShape(Expected).CapsuleHalfHeight,
			Leader->GetCapsuleComponent()->GetUnscaledCapsuleRadius());
		const bool bOk = Leader->GetStance() == Expected && FMath::IsNearlyEqual(Half, ExpectedHalf, 0.5f)
			&& FMath::IsNearlyEqual(FeetZ(Leader), State.StartFeetZ, FeetTolerance);
		FString Clips;
		if (const UOperativeAnimInstance* Anim = Cast<UOperativeAnimInstance>(Leader->GetMesh()->GetAnimInstance()))
		{
			Clips = FString::Printf(TEXT(" clips idle=%.2f crouch=%.2f prone=%.2f pelvis=%.0fcm transition=%d"),
				Anim->GetClipWeight(EOperativeClip::Idle), Anim->GetClipWeight(EOperativeClip::CrouchIdle),
				Anim->GetClipWeight(EOperativeClip::ProneIdle), PelvisHeight(Leader), Anim->IsPlayingStanceTransition() ? 1 : 0);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke t=%ds stance=%s halfHeight=%.1f (expected %.1f) feetZ=%.1f (start %.1f)%s -> %s"),
			State.Second, *AOperativeCharacter::GetStanceDisplayName(Leader->GetStance()).ToString(), Half, ExpectedHalf,
			FeetZ(Leader), State.StartFeetZ, *Clips, bOk ? TEXT("ok") : TEXT("BAD"));
		State.bOk &= bOk;
		return bOk;
	}

	/** 0.2 s after a stance change / shot: Clip (when set in the AnimBP) must be playing on the full-body slot. */
	void CheckFullBodyClipSoon(UWorld* World, FState& State, AOperativeCharacter* Leader, const TCHAR* What,
		TFunction<UAnimSequenceBase*(const UOperativeAnimInstance&)> GetClip)
	{
		TWeakObjectPtr<AOperativeCharacter> WeakLeader(Leader);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([&State, WeakLeader, What, GetClip]()
		{
			const AOperativeCharacter* L = WeakLeader.Get();
			const UOperativeAnimInstance* Anim = L ? Cast<UOperativeAnimInstance>(L->GetMesh()->GetAnimInstance()) : nullptr;
			UAnimSequenceBase* Clip = Anim ? GetClip(*Anim) : nullptr;
			const bool bOk = !Clip || Anim->IsPlayingSlotAnimation(Clip, Anim->FullBodySlot);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s clip=%s full-body playing=%d -> %s"), What, *GetNameSafe(Clip),
				Clip && Anim->IsPlayingSlotAnimation(Clip, Anim->FullBodySlot) ? 1 : 0, bOk ? TEXT("ok") : TEXT("BAD"));
			State.bOk &= bOk;
		}), 0.2f, false);
	}

	/** Returns true when finished. */
	bool Step(UWorld* World, FState& State)
	{
		AOperativeCharacter* Leader = World->GetSubsystem<USquadSubsystem>()->GetLeader();
		++State.Second;
		switch (State.Second)
		{
		case 1:
			Check(State, Leader, EOperativeStance::Standing);
			Leader->SetStance(EOperativeStance::Crouching);
			return false;
		case 2:
			Check(State, Leader, EOperativeStance::Crouching);
			Leader->SetStance(EOperativeStance::Prone);
			CheckFullBodyClipSoon(World, State, Leader, TEXT("crouch->prone"),
				[](const UOperativeAnimInstance& Anim) { return Anim.CrouchToProneAnimation.Get(); });
			return false;
		case 3:
			Check(State, Leader, EOperativeStance::Prone);
			return false;
		case 4:
		{
			// After the lying-down clip: a burst of shots plays the prone fire clip, and the body stays down through the
			// burst, the aim hold after it and the return to the idle (sampled every 0.05 s).
			Check(State, Leader, EOperativeStance::Prone);
			Leader->OnWeaponFiredNative.Broadcast(Leader, nullptr, false);
			CheckFullBodyClipSoon(World, State, Leader, TEXT("prone fire"),
				[](const UOperativeAnimInstance& Anim) { return Anim.FireProneAnimation.Get(); });
			TWeakObjectPtr<AOperativeCharacter> WeakLeader(Leader);
			TSharedRef<int32> Ticks = MakeShared<int32>(0);
			TSharedRef<FTimerHandle> Sampler = MakeShared<FTimerHandle>();
			World->GetTimerManager().SetTimer(*Sampler, FTimerDelegate::CreateLambda([&State, WeakLeader, Ticks, Sampler, World]()
			{
				AOperativeCharacter* L = WeakLeader.Get();
				if (!L || ++*Ticks > 80)
				{
					World->GetTimerManager().ClearTimer(*Sampler);
					return;
				}
				if (*Ticks % 3 == 0 && *Ticks <= 36) // a shot every 0.15 s for 1.8 s
				{
					L->OnWeaponFiredNative.Broadcast(L, nullptr, false);
				}
				const float Pelvis = PelvisHeight(L);
				State.FireMinPelvis = FMath::Min(State.FireMinPelvis, Pelvis);
				State.FireMaxPelvis = FMath::Max(State.FireMaxPelvis, Pelvis);
				if (*Ticks % 4 == 0)
				{
					const UOperativeAnimInstance* Anim = Cast<UOperativeAnimInstance>(L->GetMesh()->GetAnimInstance());
					State.FireSamples += FString::Printf(TEXT(" %.2fs:%.0f%s"), *Ticks * 0.05f, Pelvis, Anim && Anim->bIsAiming ? TEXT("a") : TEXT(""));
				}
			}), 0.05f, true);
			return false;
		}
		case 5:
		case 6:
		case 7:
			return false;
		case 8:
		{
			const bool bDown = State.FireMaxPelvis <= ProneMaxPelvis;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke prone burst pelvis %.0f..%.0f cm (a = aiming):%s -> %s"),
				State.FireMinPelvis, State.FireMaxPelvis, *State.FireSamples, bDown ? TEXT("ok") : TEXT("BAD"));
			State.bOk &= bDown;
			Check(State, Leader, EOperativeStance::Prone);
			Leader->SetStance(EOperativeStance::Standing);
			CheckFullBodyClipSoon(World, State, Leader, TEXT("prone->standing"),
				[](const UOperativeAnimInstance& Anim) { return Anim.ProneToStandAnimation.Get(); });
			return false;
		}
		case 9:
			return false;
		case 10:
			Check(State, Leader, EOperativeStance::Standing);
			Leader->SetStance(EOperativeStance::Prone);
			return false;
		case 12:
			// Crawl 6 m.
			State.CrawlStart = Leader->GetActorLocation();
			Leader->OrderMoveTo(Leader->GetActorLocation() + Leader->GetActorForwardVector() * 600.f, false);
			return false;
		case 13:
		{
			const bool bCrawling = Leader->GetStance() == EOperativeStance::Prone && FVector::Dist2D(Leader->GetActorLocation(), State.CrawlStart) > 20.f;
			Leader->SetStance(EOperativeStance::Crouching);
			const bool bStopped = Leader->GetVelocity().Size2D() < 1.f && !Leader->IsMoving();
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke crawl -> crouch: crawling %d, stopped at once %d -> %s"), bCrawling ? 1 : 0, bStopped ? 1 : 0,
				bCrawling && bStopped ? TEXT("ok") : TEXT("BAD"));
			State.bOk &= bCrawling && bStopped;
			return false;
		}
		case 14:
			Leader->SetStance(EOperativeStance::Prone);
			return false;
		case 16:
			// A sprint order from prone: up first (in place), then the run.
			State.CrawlStart = Leader->GetActorLocation();
			Leader->OrderMoveTo(Leader->GetActorLocation() - Leader->GetActorForwardVector() * 900.f, true);
			State.bRiseOk = Leader->GetStance() == EOperativeStance::Standing && Leader->GetVelocity().Size2D() < 1.f;
			return false;
		case 17:
			// 1 s in: still getting up (the clip is ~1.85 s), not moving yet.
			State.bRiseOk &= FVector::Dist2D(Leader->GetActorLocation(), State.CrawlStart) < 30.f;
			return false;
		case 19:
		{
			const float Moved = FVector::Dist2D(Leader->GetActorLocation(), State.CrawlStart);
			const bool bRunning = Leader->IsSprinting() && Moved > 150.f;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke prone sprint order: stood up in place %d, then running %d (moved %.0f cm) -> %s"),
				State.bRiseOk ? 1 : 0, bRunning ? 1 : 0, Moved, State.bRiseOk && bRunning ? TEXT("ok") : TEXT("BAD"));
			State.bOk &= State.bRiseOk && bRunning;
			Leader->StopOperative();
			return false;
		}
		case 11:
		case 15:
		case 18:
			return false;
		default:
			Check(State, Leader, EOperativeStance::Standing);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.bOk ? TEXT("PASS") : TEXT("FAIL"));
			FPlatformMisc::RequestExit(false, TEXT("StanceSmoke"));
			return true;
		}
	}

	void Start(UWorld* World)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Leader)
		{
			UE_LOG(LogCodexTactics, Error, TEXT("Smoke: no squad leader"));
			FPlatformMisc::RequestExit(false, TEXT("StanceSmoke"));
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		State->StartFeetZ = FeetZ(Leader);
		const bool bBlueprint = Leader->GetClass()->ClassGeneratedBy != nullptr;
		const UAnimInstance* Anim = Leader->GetMesh()->GetAnimInstance();
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke operative class=%s blueprint=%d placeholder=%d mesh=%s anim=%s weapon=%s"),
			*Leader->GetClass()->GetName(), bBlueprint ? 1 : 0, Leader->UsesPlaceholderBody() ? 1 : 0,
			*GetNameSafe(Leader->GetMesh()->GetSkeletalMeshAsset()), *GetNameSafe(Anim ? Anim->GetClass() : nullptr),
			*GetNameSafe(Leader->WeaponMesh ? Leader->WeaponMesh->GetStaticMesh() : nullptr));
		State->bOk = bBlueprint;
		// Headless (-nullrhi) nothing is rendered: refresh the bones anyway so the pelvis height is the real pose.
		Leader->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
		World->GetTimerManager().SetTimer(*Handle, FTimerDelegate::CreateLambda([WeakWorld, State, Handle]()
		{
			UWorld* W = WeakWorld.Get();
			if (W && Step(W, *State))
			{
				W->GetTimerManager().ClearTimer(*Handle);
			}
		}), 1.f, true);
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
		}), NavWarmupSeconds, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.StanceSmoke"),
		TEXT("Dev check: BP_Operative spawned; stance capsule heights with feet kept on the ground; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
