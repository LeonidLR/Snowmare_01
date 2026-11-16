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
	float StartSeconds, bool bHideWeapon)
{
	AOperativeCharacter* Operative = BoundOperative.Get();
	if (!Clip || bIsDead || !Operative)
	{
		return false;
	}
	const float Length = Clip->GetPlayLength();
	const float Rate = FirstAidRules::FitPlayRate(Length, Seconds, MinRate, MaxRate);
	const int32 Loops = FirstAidRules::LoopCount(Length, Seconds, Rate);
	if (UAnimMontage* Previous = FirstAidMontage.Get(); Previous && Montage_IsPlaying(Previous))
	{
		Montage_Stop(BlendIn, Previous);
	}
	const FMontageBlendSettings In(FMath::Max(0.f, BlendIn));
	UAnimMontage* Montage = UAnimMontage::CreateSlotAnimationAsDynamicMontage_WithBlendSettings(Clip, FullBodySlot, In,
		FMontageBlendSettings(FMath::Max(0.f, BlendOut)), Rate, Loops);
	if (!Montage)
	{
		return false;
	}
	FadeOutCoverMontages(In.Blend.BlendTime); // a weather pose / stance clip / interaction hands over smoothly
	const float Start = FMath::Clamp(StartSeconds, 0.f, FMath::Max(0.f, Montage->GetPlayLength() - 0.05f));
	if (Montage_PlayWithBlendSettings(Montage, In, Rate, EMontagePlayReturnType::MontageLength, Start, /*bStopAllMontages*/ false) <= 0.f)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[FirstAid] %s: %s failed to start on %s"), *Operative->DisplayName.ToString(), *Clip->GetName(),
			*FullBodySlot.ToString());
		return false;
	}
	FirstAidMontage = Montage;
	FirstAidClip = Clip;
	FirstAidPlayRate = Rate;
	LeftHandIKBlockSeconds = FMath::Max(LeftHandIKBlockSeconds, FMath::Max(0.f, Seconds - Start / Rate)); // both hands work: no rifle IK / aim
	if (bHideWeapon && !bFirstAidWeaponHidden && Operative->WeaponMesh && Operative->WeaponMesh->IsVisible())
	{
		Operative->WeaponMesh->SetVisibility(false, true);
		bFirstAidWeaponHidden = true;
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[FirstAid] %s: %s on %s, %.2f s clip x%d at %.2f (%.1f s) from %.2f s"), *Operative->DisplayName.ToString(),
		*Clip->GetName(), *FullBodySlot.ToString(), Length, Loops, Rate, Seconds, Start);
	return true;
}

void UOperativeAnimInstance::StopFirstAidClip(float BlendOut)
{
	if (UAnimMontage* Montage = FirstAidMontage.Get(); Montage && Montage_IsPlaying(Montage))
	{
		Montage_Stop(FMath::Max(0.f, BlendOut), Montage);
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

FString UOperativeAnimInstance::GetFirstAidClipName() const
{
	const UAnimSequenceBase* Clip = FirstAidClip.Get();
	return Clip && IsFirstAidClipActive() ? Clip->GetName() : FString();
}
