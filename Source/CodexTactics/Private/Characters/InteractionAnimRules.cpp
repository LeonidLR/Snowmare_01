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

float InteractionAnimRules::GetLootWindowDelay(float ClipSeconds, float BlendOutSeconds, float FallbackSeconds, bool bTimeFrozen)
{
	if (bTimeFrozen)
	{
		return 0.f;
	}
	if (ClipSeconds > 0.f)
	{
		return FMath::Max(0.f, ClipSeconds - FMath::Max(0.f, BlendOutSeconds));
	}
	return FMath::Max(0.f, FallbackSeconds);
}

bool InteractionAnimRules::ShouldPlayClip(bool bTimeFrozen, EInteractionAnimLayer Layer)
{
	return !bTimeFrozen && Layer != EInteractionAnimLayer::None;
}
