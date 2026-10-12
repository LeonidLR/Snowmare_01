// Weather idle layer of UOperativeAnimInstance (user request 2026-10-12; UE-only, no Godot reference).
// Clips: /Game/90+_Weather_Animations NEW_version/Cold, IK-retargeted onto the operative / Female Soldier skeletons by
// Scripts/Editor/setup_operative_weather_animation.py. UWarmIdleSubsystem decides the pose (Survival/WarmIdleRules); this layer
// plays its set on FullBodySlot from C++ (the AnimGraph is the user's): start -> loop A (x n) -> transit -> loop B (x n) -> transit
// -> loop A ... and the End clip / a quick blend-out when it ends. Any movement, knockdown, death, cover or vault ends it at once.

#include "Characters/OperativeAnimInstance.h"

#include "Animation/AnimMontage.h"
#include "CodexTactics.h"
#include "Characters/OperativeCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"

namespace
{
	constexpr int32 WarmClipLogMax = 256;
	/** A clip hands over to the next this long before its blend-out would start, s. */
	constexpr float WarmHandoffMargin = 0.05f;
}

bool UOperativeAnimInstance::UsesFemaleWarmClips() const
{
	const USkeletalMeshComponent* Body = GetSkelMeshComponent();
	const USkeletalMesh* Mesh = Body ? Body->GetSkeletalMeshAsset() : nullptr;
	const USkeleton* Skeleton = Mesh ? Mesh->GetSkeleton() : nullptr;
	return WarmIdleRules::UsesFemaleClips(Mesh ? Mesh->GetName() : FString(), Skeleton ? Skeleton->GetName() : FString());
}

const FWarmIdleClipSet* UOperativeAnimInstance::GetWarmClipSet(EWarmIdlePose Pose) const
{
	const bool bFemale = UsesFemaleWarmClips();
	const FWarmIdleClipSet* Male = nullptr;
	const FWarmIdleClipSet* Female = nullptr;
	switch (Pose)
	{
	case EWarmIdlePose::ColdIdle:
		Male = &ColdIdleMale;
		Female = &ColdIdleFemale;
		break;
	case EWarmIdlePose::HandWarm:
		Male = &HandWarmMale;
		Female = &HandWarmFemale;
		break;
	case EWarmIdlePose::SitWarm:
		Male = &SitWarmMale;
		Female = &SitWarmFemale;
		break;
	default:
		return nullptr;
	}
	// The female body without her own set falls back on the male one (and the reverse) rather than showing nothing.
	const FWarmIdleClipSet* Preferred = bFemale ? Female : Male;
	const FWarmIdleClipSet* Other = bFemale ? Male : Female;
	return Preferred->IsValid() ? Preferred : Other->IsValid() ? Other : nullptr;
}

void UOperativeAnimInstance::SetWarmIdleRequest(EWarmIdlePose Pose, EWarmExitReason ExitReason)
{
	WarmIdleRequest = Pose;
	if (Pose == EWarmIdlePose::None || Pose != WarmActivePose)
	{
		WarmExitReason = ExitReason;
	}
}

bool UOperativeAnimInstance::PlayWarmClip(EWarmClipStep Step, float BlendIn)
{
	const FWarmIdleClipSet* Set = GetWarmClipSet(WarmActivePose);
	if (!Set)
	{
		return false;
	}
	UAnimSequenceBase* Clip = nullptr;
	switch (Step)
	{
	case EWarmClipStep::Start: Clip = Set->Start; break;
	case EWarmClipStep::LoopA: Clip = Set->LoopA; break;
	case EWarmClipStep::TransitAB: Clip = Set->TransitAB; break;
	case EWarmClipStep::LoopB: Clip = Set->LoopB; break;
	case EWarmClipStep::TransitBA: Clip = Set->TransitBA; break;
	case EWarmClipStep::End: Clip = Set->End; break;
	default: break;
	}
	if (!Clip)
	{
		return false;
	}
	UAnimMontage* Montage = PlayCoverMontage(Clip, FAlphaBlendArgs(BlendIn), WarmIdleBlendSeconds, 1.f, 1);
	if (!Montage)
	{
		return false;
	}
	WarmMontage = Montage;
	WarmClip = Clip;
	WarmStep = Step;
	if (WarmClipLog.Num() >= WarmClipLogMax)
	{
		WarmClipLog.RemoveAt(0);
	}
	WarmClipLog.Add(Clip->GetName());
	UE_LOG(LogCodexTactics, Verbose, TEXT("[WarmIdle] %s"), *Clip->GetName());
	return true;
}

void UOperativeAnimInstance::ResetWarmState()
{
	WarmActivePose = EWarmIdlePose::None;
	WarmStep = EWarmClipStep::None;
	WarmLoopsDone = 0;
	WarmMontage.Reset();
	WarmClip.Reset();
}

void UOperativeAnimInstance::EndWarmPose(EWarmExitStyle Style)
{
	const FWarmIdleClipSet* Set = GetWarmClipSet(WarmActivePose);
	if (Style == EWarmExitStyle::PlayEnd && Set && Set->End && PlayWarmClip(EWarmClipStep::End, WarmIdleBlendSeconds))
	{
		return; // finishes on its own (UpdateWarmIdleLayer resets the state when the End clip is through)
	}
	if (UAnimMontage* Montage = WarmMontage.Get(); Montage && Montage_IsPlaying(Montage))
	{
		Montage_Stop(Style == EWarmExitStyle::Cut ? 0.05f : WarmIdleInterruptBlendSeconds, Montage);
	}
	ResetWarmState();
}

void UOperativeAnimInstance::SetWarmWeaponHidden(bool bHidden)
{
	const AOperativeCharacter* Operative = BoundOperative.Get();
	UStaticMeshComponent* Weapon = Operative ? Operative->WeaponMesh.Get() : nullptr;
	if (!Weapon || bHidden == bWarmWeaponHidden)
	{
		return;
	}
	if (bHidden)
	{
		bWarmWeaponWasVisible = Weapon->IsVisible();
		Weapon->SetVisibility(false);
	}
	else if (bWarmWeaponWasVisible && !Weapon->IsVisible())
	{
		Weapon->SetVisibility(true);
	}
	bWarmWeaponHidden = bHidden;
}

void UOperativeAnimInstance::UpdateWarmIdleLayer(const AOperativeCharacter& Operative)
{
	const bool bBodyTaken = bIsDead || bKnockedDown || bInCover || bIsVaulting || Operative.bCarrying;
	const bool bMovingNow = Speed > WarmIdleStillSpeed;
	UAnimMontage* Montage = WarmMontage.Get();
	const bool bPlaying = Montage && Montage_IsPlaying(Montage);

	if (WarmActivePose != EWarmIdlePose::None)
	{
		if (!bPlaying)
		{
			ResetWarmState(); // another layer stopped it (knockdown / death / cover clip), or the End clip is through
		}
		else if (bBodyTaken)
		{
			EndWarmPose(EWarmExitStyle::Cut);
		}
		else if (!bUseWarmIdleClips || bMovingNow || bIsProne || IsPlayingStanceTransition())
		{
			EndWarmPose(EWarmExitStyle::QuickBlend); // orders take priority: the body walks at once
		}
		else if (WarmStep == EWarmClipStep::End)
		{
			// Standing up / lowering the arms; a new pose starts after it.
		}
		else if (WarmIdleRequest != WarmActivePose)
		{
			const FWarmIdleClipSet* Set = GetWarmClipSet(WarmActivePose);
			EndWarmPose(WarmIdleRules::GetExitStyle(WarmExitReason, Set && Set->End));
		}
		else
		{
			const float Rate = FMath::Max(FMath::Abs(Montage_GetPlayRate(Montage)), 0.01f);
			const float Left = (Montage->GetPlayLength() - Montage_GetPosition(Montage)) / Rate;
			if (Left <= WarmIdleBlendSeconds + WarmHandoffMargin)
			{
				const FWarmIdleClipSet* Set = GetWarmClipSet(WarmActivePose);
				const bool bLoop = WarmStep == EWarmClipStep::LoopA || WarmStep == EWarmClipStep::LoopB;
				const int32 Done = bLoop ? WarmLoopsDone + 1 : 0;
				EWarmClipStep Next = WarmIdleRules::NextClipStep(WarmStep, Done, WarmLoopsTarget, Set && Set->LoopB);
				// A missing transit clip: straight into the other loop.
				if (Set && Next == EWarmClipStep::TransitAB && !Set->TransitAB)
				{
					Next = EWarmClipStep::LoopB;
				}
				if (Set && Next == EWarmClipStep::TransitBA && !Set->TransitBA)
				{
					Next = EWarmClipStep::LoopA;
				}
				WarmLoopsDone = Next == WarmStep ? Done : 0;
				if (Next != WarmStep && (Next == EWarmClipStep::LoopA || Next == EWarmClipStep::LoopB))
				{
					WarmLoopsTarget = FMath::RandRange(FMath::Max(1, WarmIdleLoopsMin), FMath::Max(WarmIdleLoopsMin, WarmIdleLoopsMax));
				}
				if (!PlayWarmClip(Next, WarmIdleBlendSeconds))
				{
					PlayWarmClip(EWarmClipStep::LoopA, WarmIdleBlendSeconds);
				}
			}
		}
	}

	if (WarmActivePose == EWarmIdlePose::None && bUseWarmIdleClips && WarmIdleRequest != EWarmIdlePose::None && !bBodyTaken && !bMovingNow
		&& !bIsProne && !IsPlayingStanceTransition() && GetWarmClipSet(WarmIdleRequest))
	{
		WarmActivePose = WarmIdleRequest;
		WarmLoopsDone = 0;
		WarmLoopsTarget = FMath::RandRange(FMath::Max(1, WarmIdleLoopsMin), FMath::Max(WarmIdleLoopsMin, WarmIdleLoopsMax));
		if (!PlayWarmClip(EWarmClipStep::Start, WarmIdleBlendSeconds) && !PlayWarmClip(EWarmClipStep::LoopA, WarmIdleBlendSeconds))
		{
			ResetWarmState();
		}
	}

	bWarmIdleActive = WarmActivePose != EWarmIdlePose::None;
	WarmIdlePose = WarmActivePose;
	SetWarmWeaponHidden(bHideWeaponInWarmIdle && bWarmIdleActive && !bIsDead);
}
