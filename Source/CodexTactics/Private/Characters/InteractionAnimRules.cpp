#include "Characters/InteractionAnimRules.h"

EInteractionAnimLayer InteractionAnimRules::ChooseLayer(EOperativeStance Stance)
{
	switch (Stance)
	{
	case EOperativeStance::Standing: return EInteractionAnimLayer::FullBody;
	case EOperativeStance::Crouching: return EInteractionAnimLayer::UpperBody;
	default: return EInteractionAnimLayer::None;
	}
}

float InteractionAnimRules::GetEffectDelay(float ClipLengthSeconds, float StartSeconds, float PlayRate, float EffectFraction)
{
	if (ClipLengthSeconds <= 0.f)
	{
		return 0.f;
	}
	const float Moment = ClipLengthSeconds * FMath::Clamp(EffectFraction, 0.f, 1.f);
	return FMath::Max(0.f, Moment - FMath::Max(0.f, StartSeconds)) / FMath::Max(0.1f, PlayRate);
}

float InteractionAnimRules::GetLootWindowDelay(float EffectDelaySeconds, bool bClipPlayed, float FallbackSeconds)
{
	return bClipPlayed ? FMath::Max(0.f, EffectDelaySeconds) : FMath::Max(0.f, FallbackSeconds);
}

EInteractionClipDispatch InteractionAnimRules::GetDispatch(bool bTimeFrozen, EInteractionAnimLayer Layer)
{
	if (bTimeFrozen)
	{
		return EInteractionClipDispatch::WaitForRelease;
	}
	return Layer == EInteractionAnimLayer::None ? EInteractionClipDispatch::NoClip : EInteractionClipDispatch::PlayClip;
}

bool InteractionAnimRules::ShouldCompleteSearch(bool bCollectorAlive, bool bKnockedDown, float DistanceToCrateCm, float ReachCm)
{
	return bCollectorAlive && !bKnockedDown && DistanceToCrateCm <= FMath::Max(0.f, ReachCm);
}

bool InteractionAnimRules::ShouldPlayClip(bool bTimeFrozen, EInteractionAnimLayer Layer)
{
	return !bTimeFrozen && Layer != EInteractionAnimLayer::None;
}
