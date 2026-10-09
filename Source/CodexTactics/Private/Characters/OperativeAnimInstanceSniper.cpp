// Sniper rifle animation layer of UOperativeAnimInstance (user request 2026-10-09; UE-only, no Godot reference).
// Clips: /Game/Sniper_Animation (UE5 SK_Mannequin of the pack, compatible with the operative skeletons), assigned by
// Scripts/Editor/setup_operative_sniper_animation.py. Everything plays on FullBodySlot from C++ (the AnimGraph is the user's).

#include "Characters/OperativeAnimInstance.h"

#include "Animation/AnimMontage.h"
#include "Animation/BlendSpace.h"
#include "CodexTactics.h"
#include "Characters/OperativeCharacter.h"
#include "Combat/SniperRules.h"

namespace
{
	constexpr int32 SniperClipLogMax = 256;
}

UAnimSequenceBase* UOperativeAnimInstance::SniperClipFor(const TArray<TObjectPtr<UAnimSequenceBase>>& Clips, EOperativeStance InStance) const
{
	const int32 Index = SniperRules::ClipIndex(InStance);
	return Clips.IsValidIndex(Index) ? Clips[Index].Get() : nullptr;
}

UAnimMontage* UOperativeAnimInstance::PlaySniperOneShot(UAnimSequenceBase* Clip, float BlendIn, float BlendOut, float PlayRate)
{
	if (!Clip)
	{
		return nullptr;
	}
	UAnimMontage* Montage = PlayCoverMontage(Clip, FAlphaBlendArgs(BlendIn), BlendOut, PlayRate);
	SniperOneShotMontage = Montage;
	SniperLoopMontage.Reset(); // faded out by PlayCoverMontage; the loop restarts after the one-shot
	SniperLoopClip.Reset();
	bSniperBoltPlaying = false;
	if (Montage)
	{
		if (SniperClipLog.Num() >= SniperClipLogMax)
		{
			SniperClipLog.RemoveAt(0);
		}
		SniperClipLog.Add(Clip->GetName());
		UE_LOG(LogCodexTactics, Display, TEXT("[SniperAnim] %s (rate %.2f)"), *Clip->GetName(), PlayRate);
	}
	return Montage;
}

UAnimSequenceBase* UOperativeAnimInstance::PickSniperTransition(EOperativeStance From, EOperativeStance To) const
{
	if (!bSniperWeapon || !bUseSniperClips || bInCover || bIsDead || bKnockedDown || Speed >= SniperStillSpeed || From == To)
	{
		return nullptr;
	}
	// The pack's transitions all start or end standing: stand <-> knee, stand <-> prone. Knee <-> prone has none (the loops
	// cross-blend).
	if (From == EOperativeStance::Standing)
	{
		return SniperClipFor(SniperAimStart, To);
	}
	if (To == EOperativeStance::Standing)
	{
		return SniperClipFor(SniperAimEnd, From);
	}
	return nullptr;
}

void UOperativeAnimInstance::PlaySniperShot(bool bCycleBolt)
{
	const AOperativeCharacter* Operative = BoundOperative.Get();
	if (!bSniperWeapon || !bUseSniperClips || bInCover || bIsDead || bKnockedDown || (Operative && Operative->bInCover))
	{
		return; // cover / death / knockdown own the body
	}
	const EOperativeStance Now = Operative ? Operative->GetStance() : Stance;
	// Kneeling down / a stance change not seen by the anim yet (turn-based: kneel + shot in one click): the shot follows it.
	if (IsPlayingStanceTransition() || Now != PreviousStance.Get(Now))
	{
		bSniperFireQueued = true;
		bSniperQueuedFireBolt = bCycleBolt;
		bSniperBoltQueued = false;
		return;
	}
	if (PlaySniperOneShot(SniperClipFor(SniperFire, Now), SniperShotBlendInSeconds, SniperShotBlendOutSeconds))
	{
		bSniperBoltQueued = bCycleBolt && SniperClipFor(SniperBoltCycle, Now) != nullptr;
		SniperBoltStance = Now;
	}
}

bool UOperativeAnimInstance::StartSniperReload(const AOperativeCharacter& Operative)
{
	UAnimSequenceBase* Clip = SniperClipFor(SniperReload, Operative.GetStance());
	if (!Clip || IsPlayingStanceTransition())
	{
		return false;
	}
	bSniperFireQueued = false;
	bSniperBoltQueued = false; // the magazine change works the bolt too
	return PlaySniperOneShot(Clip, 0.15f, 0.2f, SniperRules::PlayRateToFit(Clip->GetPlayLength(), FMath::Max(0.1f, Operative.ReloadTimer))) != nullptr;
}

bool UOperativeAnimInstance::PlaySniperHitReaction(EOperativeStance InStance)
{
	UAnimSequenceBase* Clip = SniperClipFor(SniperHitReact, InStance);
	if (!Clip || IsPlayingStanceTransition())
	{
		return false;
	}
	const bool bBolt = bSniperBoltQueued || bSniperBoltPlaying; // a bolt cut short by the hit is worked after it
	if (!PlaySniperOneShot(Clip, HitReactionBlendInSeconds, HitReactionBlendOutSeconds))
	{
		return false;
	}
	bSniperBoltQueued = bBolt;
	LastHitReactionClip = Clip;
	return true;
}

void UOperativeAnimInstance::UpdateSniperLayer(const AOperativeCharacter& Operative)
{
	bSniperWeapon = Operative.IsSniperWeaponEquipped();
	const bool bStill = Speed < SniperStillSpeed;
	const bool bEligible = bSniperWeapon && bUseSniperClips && !bInCover && !bIsDead && !bKnockedDown && !bIsVaulting && !bIsSprinting
		&& !Operative.bCarrying && bStill && !IsThrowingGrenade();
	const UAnimMontage* OneShot = SniperOneShotMontage.Get();
	const bool bOneShotPlaying = OneShot && Montage_IsPlaying(OneShot);
	if (bSniperTransitionPlaying && !IsPlayingStanceTransition())
	{
		bSniperTransitionPlaying = false;
	}
	if (!bOneShotPlaying)
	{
		bSniperBoltPlaying = false;
	}
	bSniperPose = bEligible;
	const int32 AoIndex = SniperRules::ClipIndex(Stance);
	ActiveAimOffset = bEligible && SniperAimOffsets.IsValidIndex(AoIndex) && SniperAimOffsets[AoIndex] ? SniperAimOffsets[AoIndex].Get() : RifleAimOffset.Get();

	if (!bEligible)
	{
		// Dead / knocked down / in cover: those layers own the FullBody slot. Walking / sprinting / carrying: our clips fade.
		if (!bIsDead && !bKnockedDown)
		{
			if (UAnimMontage* Loop = SniperLoopMontage.Get(); Loop && Montage_IsPlaying(Loop))
			{
				Montage_Stop(0.2f, Loop);
			}
			if (!bStill || bInCover || !bSniperWeapon)
			{
				if (UAnimMontage* Shot = SniperOneShotMontage.Get(); Shot && Montage_IsPlaying(Shot))
				{
					Montage_Stop(0.2f, Shot);
				}
				if (bSniperTransitionPlaying && !bStill)
				{
					// A move ordered during the kneel / rise clip (a sprint order from prone): the legs walk, no clip slides along.
					StopSlotAnimation(0.2f, FullBodySlot);
					StanceTransitionMontage.Reset();
					bSniperTransitionPlaying = false;
				}
				bSniperFireQueued = false;
				bSniperBoltQueued = false;
			}
		}
		SniperLoopMontage.Reset();
		SniperLoopClip.Reset();
		bSniperWasPosed = false;
		return;
	}

	// A stance change this frame: UpdateStanceTransition (after this layer) decides the kneel / rise clip first.
	if (IsPlayingStanceTransition() || bOneShotPlaying || (PreviousStance.IsSet() && PreviousStance.GetValue() != Stance))
	{
		bSniperWasPosed = true;
		return; // the kneel / rise clip or a shot / bolt / reload / hit plays; the loop comes after it
	}
	if (bSniperFireQueued)
	{
		bSniperFireQueued = false;
		if (PlaySniperOneShot(SniperClipFor(SniperFire, Stance), SniperShotBlendInSeconds, SniperShotBlendOutSeconds))
		{
			bSniperBoltQueued = bSniperQueuedFireBolt && SniperClipFor(SniperBoltCycle, Stance) != nullptr;
			SniperBoltStance = Stance;
			bSniperWasPosed = true;
			return;
		}
	}
	if (bSniperBoltQueued && SniperBoltStance != Stance)
	{
		bSniperBoltQueued = false; // she changed stance since the shot: the next stance's idle, no bolt clip of another pose
	}
	if (bSniperBoltQueued)
	{
		bSniperBoltQueued = false;
		if (PlaySniperOneShot(SniperClipFor(SniperBoltCycle, Stance), 0.1f, 0.2f))
		{
			bSniperBoltPlaying = true;
			bSniperWasPosed = true;
			return;
		}
	}
	UAnimSequenceBase* Idle = SniperClipFor(SniperIdle, Stance);
	if (!Idle)
	{
		return;
	}
	const UAnimMontage* Loop = SniperLoopMontage.Get();
	if (SniperLoopClip.Get() == Idle && Loop && Montage_IsPlaying(Loop))
	{
		bSniperWasPosed = true;
		return;
	}
	// Coming to rest standing (after a walk): raise the rifle first (AS_Stand_Aim_Start), then the aimed idle.
	if (!bSniperWasPosed && Stance == EOperativeStance::Standing)
	{
		bSniperWasPosed = true;
		if (PlaySniperOneShot(SniperClipFor(SniperAimStart, EOperativeStance::Standing), 0.2f, SniperLoopBlendSeconds))
		{
			return;
		}
	}
	bSniperWasPosed = true;
	SniperLoopMontage = PlayCoverMontage(Idle, FAlphaBlendArgs(SniperLoopBlendSeconds), 0.25f, 1.f, /*LoopCount*/ 1000);
	SniperLoopClip = Idle;
	if (SniperLoopMontage.IsValid())
	{
		if (SniperClipLog.Num() >= SniperClipLogMax)
		{
			SniperClipLog.RemoveAt(0);
		}
		SniperClipLog.Add(Idle->GetName());
		UE_LOG(LogCodexTactics, Display, TEXT("[SniperAnim] loop %s"), *Idle->GetName());
	}
}
