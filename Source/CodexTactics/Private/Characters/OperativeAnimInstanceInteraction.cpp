// Interaction clip layer of UOperativeAnimInstance (game designer 2026-10-15; UE-only, no Godot reference).
// Clips: the user's Polygonmaker pack /Game/Polygonmaker/Animations/Interactions/UE4/ (UE4 mannequin rig, played by bone name - same
// bones and reference pose as the Post_Apo skeleton), as Claude-owned montages made by
// Scripts/Editor/setup_operative_interaction_animation.py (/Game/Characters/Operatives/Anims/Interactions/AM_Interact_*).
// Gameplay asks for a clip (ALootCrateActor: chest open / search; mines and tripwires: pickup after a successful disarm); the layer
// picks the body part by stance (InteractionAnimRules::ChooseLayer) and cuts the clip as soon as anything else needs the body.

#include "Characters/OperativeAnimInstance.h"

#include "Animation/AnimMontage.h"
#include "CodexTactics.h"
#include "Characters/OperativeCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"

namespace
{
	constexpr int32 InteractionClipLogMax = 128;
	const TCHAR* const InteractionMontageDir = TEXT("/Game/Characters/Operatives/Anims/Interactions/");
	const TCHAR* const ColdIdleVariantPath = TEXT("/Game/Polygonmaker/Animations/Interactions/UE4/Anim_Regular_idle_cold.Anim_Regular_idle_cold");

	UAnimMontage* LoadInteractionMontage(const FString& Name)
	{
		return LoadObject<UAnimMontage>(nullptr, *FString::Printf(TEXT("%s%s.%s"), InteractionMontageDir, *Name, *Name), nullptr,
			LOAD_NoWarn | LOAD_Quiet);
	}

	const TCHAR* InteractionAnimName(EOperativeInteractionAnim Anim)
	{
		switch (Anim)
		{
		case EOperativeInteractionAnim::ChestOpen: return TEXT("ChestOpen");
		case EOperativeInteractionAnim::ChestSearch: return TEXT("ChestSearch");
		case EOperativeInteractionAnim::PickupGround: return TEXT("PickupGround");
		default: return TEXT("None");
		}
	}

	/** The first clip of a montage's first slot track (the FullBody montage's clip for the upper-body fallback). */
	UAnimSequenceBase* FirstMontageClip(const UAnimMontage* Montage)
	{
		if (!Montage || Montage->SlotAnimTracks.IsEmpty() || Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.IsEmpty())
		{
			return nullptr;
		}
		return Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference().Get();
	}
}

UOperativeAnimInstance* UOperativeAnimInstance::Of(const AActor* Actor)
{
	const ACharacter* Character = Cast<ACharacter>(Actor);
	return Character && Character->GetMesh() ? Cast<UOperativeAnimInstance>(Character->GetMesh()->GetAnimInstance()) : nullptr;
}

void UOperativeAnimInstance::LoadDefaultInteractionClips()
{
	auto Fill = [](FOperativeInteractionClip& Clip, const TCHAR* Name)
	{
		if (!Clip.FullBody)
		{
			Clip.FullBody = LoadInteractionMontage(FString::Printf(TEXT("AM_Interact_%s"), Name));
		}
		if (!Clip.UpperBody)
		{
			Clip.UpperBody = LoadInteractionMontage(FString::Printf(TEXT("AM_Interact_%s_Upper"), Name));
		}
	};
	Fill(ChestOpenClip, TEXT("ChestOpen"));
	Fill(ChestSearchClip, TEXT("ChestSearch"));
	Fill(PickupGroundClip, TEXT("PickupGround"));
	if (ColdIdleVariants.IsEmpty())
	{
		if (UAnimSequenceBase* Variant = LoadObject<UAnimSequenceBase>(nullptr, ColdIdleVariantPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			ColdIdleVariants.Add(Variant);
		}
	}
}

const FOperativeInteractionClip* UOperativeAnimInstance::GetInteractionClip(EOperativeInteractionAnim Anim) const
{
	switch (Anim)
	{
	case EOperativeInteractionAnim::ChestOpen: return &ChestOpenClip;
	case EOperativeInteractionAnim::ChestSearch: return &ChestSearchClip;
	case EOperativeInteractionAnim::PickupGround: return &PickupGroundClip;
	default: return nullptr;
	}
}

float UOperativeAnimInstance::GetInteractionBlendOutSeconds(EOperativeInteractionAnim Anim) const
{
	const FOperativeInteractionClip* Clip = GetInteractionClip(Anim);
	return Clip && Clip->FullBody ? Clip->FullBody->BlendOut.GetBlendTime() : 0.f;
}

void UOperativeAnimInstance::ResetInteractionState()
{
	InteractionAnim = EOperativeInteractionAnim::None;
	InteractionLayer = EInteractionAnimLayer::None;
	InteractionMontage.Reset();
}

FInteractionClipTiming UOperativeAnimInstance::PlayInteractionTimed(EOperativeInteractionAnim Anim, EOperativeStance ChoiceStance)
{
	FInteractionClipTiming Timing;
	const AOperativeCharacter* Operative = BoundOperative.Get();
	const FOperativeInteractionClip* Clip = GetInteractionClip(Anim);
	const UWorld* World = GetWorld();
	const UGameFlowSubsystem* Flow = World ? World->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	const bool bTimeFrozen = Flow && Flow->GetCombatMode() == ECodexCombatMode::TacticalPause;
	const EInteractionAnimLayer Layer = InteractionAnimRules::ChooseLayer(ChoiceStance);
	const FString Who = Operative ? Operative->DisplayName.ToString() : GetName();
	if (!bUseInteractionClips || !Clip || !Clip->FullBody || !Operative || bIsDead || Operative->IsKnockedDown() || Operative->bInCover
		|| !InteractionAnimRules::ShouldPlayClip(bTimeFrozen, Layer))
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[InteractionAnim] %s: %s not played (stance %d%s%s)"), *Who, InteractionAnimName(Anim),
			static_cast<int32>(ChoiceStance), bTimeFrozen ? TEXT(", tactical pause") : TEXT(""), Clip && Clip->FullBody ? TEXT("") : TEXT(", no clip"));
		return Timing;
	}
	const float Rate = FMath::Max(0.1f, Clip->PlayRate);
	if (UAnimMontage* Previous = InteractionMontage.Get(); Previous && Montage_IsPlaying(Previous))
	{
		Montage_Stop(Clip->FullBody->BlendIn.GetBlendTime(), Previous);
	}
	ResetInteractionState();

	UAnimMontage* Played = nullptr;
	float Start = 0.f;
	if (Layer == EInteractionAnimLayer::FullBody)
	{
		Start = FMath::Clamp(Clip->FullBodyStartSeconds, 0.f, FMath::Max(0.f, Clip->FullBody->GetPlayLength() - 0.1f));
		FadeOutCoverMontages(Clip->FullBody->BlendIn.GetBlendTime()); // a weather pose / sniper loop / stance clip hands over smoothly
		if (Montage_Play(Clip->FullBody, Rate, EMontagePlayReturnType::MontageLength, Start, /*bStopAllMontages*/ false) > 0.f)
		{
			Played = Clip->FullBody;
		}
	}
	else if (Clip->UpperBody && Clip->UpperBody->IsValidSlot(UpperBodySlot))
	{
		if (Montage_Play(Clip->UpperBody, Rate, EMontagePlayReturnType::MontageLength, 0.f, /*bStopAllMontages*/ false) > 0.f)
		{
			Played = Clip->UpperBody;
		}
	}
	else if (UAnimSequenceBase* Sequence = FirstMontageClip(Clip->FullBody))
	{
		// No crouched montage for the ABP's upper-body slot: the standing clip on that slot.
		const FMontageBlendSettings In(Clip->FullBody->BlendIn.GetBlendTime());
		UAnimMontage* Dynamic = UAnimMontage::CreateSlotAnimationAsDynamicMontage_WithBlendSettings(Sequence, UpperBodySlot, In,
			FMontageBlendSettings(Clip->FullBody->BlendOut.GetBlendTime()), Rate, 1);
		if (Dynamic && Montage_PlayWithBlendSettings(Dynamic, In, Rate, EMontagePlayReturnType::MontageLength, 0.f, /*bStopAllMontages*/ false) > 0.f)
		{
			Played = Dynamic;
		}
	}
	if (!Played)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[InteractionAnim] %s: %s failed to start"), *Who, InteractionAnimName(Anim));
		return Timing;
	}
	InteractionMontage = Played;
	InteractionAnim = Anim;
	InteractionLayer = Layer;
	const float Seconds = FMath::Max(0.f, (Played->GetPlayLength() - Start) / Rate);
	LeftHandIKBlockSeconds = FMath::Max(LeftHandIKBlockSeconds, Seconds); // both hands work: no left-hand IK / aim offset
	const TCHAR* LayerName = Layer == EInteractionAnimLayer::FullBody ? TEXT("FullBody") : TEXT("UpperBody");
	if (InteractionClipLog.Num() >= InteractionClipLogMax)
	{
		InteractionClipLog.RemoveAt(0);
	}
	InteractionClipLog.Add(FString::Printf(TEXT("%s@%s"), InteractionAnimName(Anim), LayerName));
	Timing.bPlayed = true;
	Timing.Seconds = Seconds;
	Timing.EffectDelaySeconds = FMath::Min(Seconds, InteractionAnimRules::GetEffectDelay(Played->GetPlayLength(), Start, Rate, Clip->EffectFraction));
	UE_LOG(LogCodexTactics, Display, TEXT("[InteractionAnim] %s: %s (%s) on %s from %.2f s, %.2f s, effect at %.2f s"), *Who, InteractionAnimName(Anim),
		*Played->GetName(), LayerName, Start, Seconds, Timing.EffectDelaySeconds);
	return Timing;
}

void UOperativeAnimInstance::UpdateInteractionLayer(const AOperativeCharacter& Operative)
{
	if (InteractionAnim == EOperativeInteractionAnim::None)
	{
		return;
	}
	UAnimMontage* Montage = InteractionMontage.Get();
	if (!Montage || !Montage_IsPlaying(Montage))
	{
		ResetInteractionState(); // played out (blending out now) or another layer stopped it
		return;
	}
	const bool bWrongStance = Stance == EOperativeStance::Prone
		|| (InteractionLayer == EInteractionAnimLayer::FullBody && Stance != EOperativeStance::Standing);
	if (bIsDead || bKnockedDown || Operative.bInCover || bIsVaulting || Operative.bCarrying || Speed > InteractionStillSpeed || bWrongStance)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[InteractionAnim] %s: %s cut (speed %.0f, stance %d)"), *Operative.DisplayName.ToString(),
			InteractionAnimName(InteractionAnim), Speed, static_cast<int32>(Stance));
		Montage_Stop(InteractionInterruptBlendSeconds, Montage);
		ResetInteractionState();
	}
}
