// First-aid clip layer of UOperativeAnimInstance (the game designer 2026-11-16, feat/first-aid; UE-only - Godot's medkit was instant).
// UFirstAidSubsystem asks for a clip of the healing pack (/Game/HealingAnimations, IK-retargeted copies in /Game/Characters/Animations/FirstAid/
// {Male,Female} by Scripts/Editor/setup_first_aid_animation.py); it plays here on FullBodySlot - the ABP's existing full-body slot (knockdown,
// vault / mantle, interaction, cover), so the ABP needs no change - without the engine's "one montage per group" stop (like the cover clips).
// The native layers yield to it on their own (the injured / prone-crawl / stealth / shotgun layers fade out under a FullBody montage; the warm
// idle treats it as a body taken).

#include "Characters/OperativeAnimInstance.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "CodexTactics.h"
#include "Characters/OperativeCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Survival/FirstAidRules.h"

bool UOperativeAnimInstance::PlayFirstAidClip(UAnimSequenceBase* Clip, float Seconds, float MinRate, float MaxRate, float BlendIn, float BlendOut,
	float ElapsedSeconds, bool bHideWeapon)
{
	AOperativeCharacter* Operative = BoundOperative.Get();
	if (!Clip || bIsDead || !Operative)
	{
		return false;
	}
	if (UAnimMontage* Previous = FirstAidMontage.Get(); Previous && Montage_IsPlaying(Previous))
	{
		Montage_Stop(BlendIn, Previous);
	}
	const FMontageBlendSettings In(FMath::Max(0.f, BlendIn));
	const FMontageBlendSettings Out(FMath::Max(0.f, BlendOut));
	// The montage's own length (a clip with a RateScale of its own plays shorter / longer than its raw length): the rate and the loops are fitted
	// to it. RateScale of the montage stays 1 - the play call sets the rate (both would multiply).
	UAnimMontage* Montage = UAnimMontage::CreateSlotAnimationAsDynamicMontage_WithBlendSettings(Clip, FullBodySlot, In, Out, 1.f, 1);
	const float Length = Montage ? Montage->GetPlayLength() : 0.f;
	const float Rate = FirstAidRules::FitPlayRate(Length, Seconds, MinRate, MaxRate);
	const int32 Loops = FirstAidRules::LoopCount(Length, Seconds, Rate);
	if (Montage && Loops > 1)
	{
		Montage = UAnimMontage::CreateSlotAnimationAsDynamicMontage_WithBlendSettings(Clip, FullBodySlot, In, Out, 1.f, Loops);
	}
	if (!Montage)
	{
		return false;
	}
	FadeOutCoverMontages(In.Blend.BlendTime); // a weather pose / stance clip / interaction hands over smoothly
	const float Start = FMath::Clamp(FMath::Max(0.f, ElapsedSeconds) * Rate, 0.f, FMath::Max(0.f, Montage->GetPlayLength() - 0.05f));
	if (Montage_PlayWithBlendSettings(Montage, In, Rate, EMontagePlayReturnType::MontageLength, Start, /*bStopAllMontages*/ false) <= 0.f)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[FirstAid] %s: %s failed to start on %s"), *Operative->DisplayName.ToString(), *Clip->GetName(),
			*FullBodySlot.ToString());
		return false;
	}
	// The montage holds its last frame instead of its own blend-out: the treatment ends it (StopFirstAidClip). A long blend-out (the lying patient's,
	// R17-6) would otherwise start before the treatment's end and read as a cut clip (UFirstAidSubsystem restarts those).
	if (FAnimMontageInstance* Instance = GetActiveInstanceForMontage(Montage))
	{
		Instance->bEnableAutoBlendOut = false;
	}
	FirstAidMontage = Montage;
	FirstAidFadingMontage.Reset();
	FirstAidClip = Clip;
	FirstAidPlayRate = Rate;
	LeftHandIKBlockSeconds = FMath::Max(LeftHandIKBlockSeconds, FMath::Max(0.f, Seconds - ElapsedSeconds)); // both hands work: no rifle IK / aim
	if (bHideWeapon && !bFirstAidWeaponHidden && Operative->WeaponMesh && Operative->WeaponMesh->IsVisible())
	{
		Operative->WeaponMesh->SetVisibility(false, true);
		bFirstAidWeaponHidden = true;
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[FirstAid] %s: %s on %s, %.2f s montage (clip %.2f s) x%d at %.2f (%.1f s) from %.2f s"),
		*Operative->DisplayName.ToString(), *Clip->GetName(), *FullBodySlot.ToString(), Length, Clip->GetPlayLength(), Loops, Rate, Seconds, Start);
	return true;
}

void UOperativeAnimInstance::StopFirstAidClip(float BlendOut)
{
	if (UAnimMontage* Montage = FirstAidMontage.Get(); Montage && Montage_IsPlaying(Montage))
	{
		Montage_Stop(FMath::Max(0.f, BlendOut), Montage);
		FirstAidFadingMontage = Montage;
	}
	FirstAidMontage.Reset();
	FirstAidClip.Reset();
	LeftHandIKBlockSeconds = FMath::Min(LeftHandIKBlockSeconds, FMath::Max(0.f, BlendOut));
	AOperativeCharacter* Operative = BoundOperative.Get();
	if (bFirstAidWeaponHidden && Operative && Operative->WeaponMesh && !bIsDead)
	{
		Operative->WeaponMesh->SetVisibility(true, true);
	}
	bFirstAidWeaponHidden = false;
}

bool UOperativeAnimInstance::IsFirstAidClipActive() const
{
	const UAnimMontage* Montage = FirstAidMontage.Get();
	return Montage && Montage_IsPlaying(Montage);
}

float UOperativeAnimInstance::GetFirstAidClipWeight() const
{
	for (const UAnimMontage* Montage : { FirstAidMontage.Get(), FirstAidFadingMontage.Get() })
	{
		// GetInstanceForMontage: also an instance that blends out (GetActiveInstanceForMontage drops it at Montage_Stop).
		if (const FAnimMontageInstance* Instance = Montage ? GetInstanceForMontage(Montage) : nullptr)
		{
			return FMath::Clamp(Instance->GetWeight(), 0.f, 1.f);
		}
	}
	return 0.f;
}

FString UOperativeAnimInstance::GetFirstAidClipName() const
{
	const UAnimSequenceBase* Clip = FirstAidClip.Get();
	return Clip && IsFirstAidClipActive() ? Clip->GetName() : FString();
}
