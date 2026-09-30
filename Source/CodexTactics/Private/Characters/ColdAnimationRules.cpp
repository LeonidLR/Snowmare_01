#include "Characters/ColdAnimationRules.h"

int32 ColdAnimationRules::SelectTier(int32 CurrentTier, float Cold, const TArray<float>& Thresholds, float Hysteresis)
{
	if (Thresholds.Num() != 4)
	{
		return 0;
	}
	Cold = FMath::Clamp(Cold, 0.f, 100.f);
	int32 Tier = FMath::Clamp(CurrentTier, 0, 4);
	while (Tier < 4 && Cold >= Thresholds[Tier])
	{
		++Tier;
	}
	while (Tier > 0 && Cold < Thresholds[Tier - 1] - FMath::Max(0.f, Hysteresis))
	{
		--Tier;
	}
	return Tier;
}

int32 ColdAnimationRules::ResolveClipIndex(const TArray<bool>& ClipSet, int32 Level)
{
	for (int32 Index = FMath::Min(Level, ClipSet.Num()) - 1; Index >= 0; --Index)
	{
		if (ClipSet[Index])
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

float ColdAnimationRules::StepWeight(float Weight, int32 Tier, bool bEligible, float DeltaSeconds, float FadeSeconds)
{
	if (!bEligible)
	{
		return 0.f;
	}
	return FMath::FInterpConstantTo(Weight, Tier > 0 ? 1.f : 0.f, DeltaSeconds, 1.f / FMath::Max(FadeSeconds, KINDA_SMALL_NUMBER));
}
