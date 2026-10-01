// Dev-only console command for headless stance checks on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.StanceSmoke
// The squad must be spawned from BP_Operative. The leader goes crouch -> prone -> standing: the capsule takes the
// stance height (Godot 1.3 / 0.7 / 2.0 m ratios) while the feet stay on the ground. With prone clips set in
// ABP_Operative, crouch -> prone and prone -> standing play their transition clips and a prone shot plays the
// full-body prone fire clip.

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
	};

	float FeetZ(const AOperativeCharacter* Operative)
	{
		return Operative->GetActorLocation().Z - Operative->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
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
			Clips = FString::Printf(TEXT(" clips idle=%.2f crouch=%.2f prone=%.2f"), Anim->GetClipWeight(EOperativeClip::Idle),
				Anim->GetClipWeight(EOperativeClip::CrouchIdle), Anim->GetClipWeight(EOperativeClip::ProneIdle));
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
			// After the lying-down clip: a shot plays the prone fire clip.
			Leader->OnWeaponFiredNative.Broadcast(Leader, nullptr, false);
			CheckFullBodyClipSoon(World, State, Leader, TEXT("prone fire"),
				[](const UOperativeAnimInstance& Anim) { return Anim.FireProneAnimation.Get(); });
			return false;
		case 5:
			Check(State, Leader, EOperativeStance::Prone);
			Leader->SetStance(EOperativeStance::Standing);
			CheckFullBodyClipSoon(World, State, Leader, TEXT("prone->standing"),
				[](const UOperativeAnimInstance& Anim) { return Anim.ProneToStandAnimation.Get(); });
			return false;
		case 6:
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
