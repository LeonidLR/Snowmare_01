#include "Combat/CombatGroundRules.h"

namespace CombatGroundRules
{
	TArray<float> ProbeDistances(float HorizontalCm, const FGroundProbeConfig& Config, float& OutSpacingCm)
	{
		TArray<float> Distances;
		const float Spacing = FMath::Max(1.f, Config.SpacingCm);
		const int32 MaxProbes = FMath::Max(0, FMath::RoundToInt(Config.MaxProbes));
		// Segments of at most Spacing; the probes sit on the inner segment ends.
		const int32 Segments = FMath::Min(FMath::Max(1, FMath::CeilToInt(HorizontalCm / Spacing)), MaxProbes + 1);
		OutSpacingCm = HorizontalCm / static_cast<float>(Segments);
		for (int32 Index = 1; Index < Segments; ++Index)
		{
			Distances.Add(OutSpacingCm * static_cast<float>(Index));
		}
		return Distances;
	}

	bool AreNeighbours(const FFloorSample& A, const FFloorSample& B, float SpacingCm)
	{
		return A.bHit && B.bHit && FMath::Abs(B.DistanceCm - A.DistanceCm) <= SpacingCm * 1.01f + KINDA_SMALL_NUMBER;
	}

	bool HasLevelEdge(TConstArrayView<FFloorSample> Profile, float EdgeDropCm, float SpacingCm)
	{
		for (int32 Index = 1; Index < Profile.Num(); ++Index)
		{
			const FFloorSample& Near = Profile[Index - 1];
			const FFloorSample& Far = Profile[Index];
			if (AreNeighbours(Near, Far, SpacingCm) && Near.Z - Far.Z >= EdgeDropCm)
			{
				return true;
			}
		}
		return false;
	}

	bool IsSharedGround(TConstArrayView<FFloorSample> Profile, float StepMaxCm)
	{
		if (Profile.Num() < 2)
		{
			return false;
		}
		for (int32 Index = 0; Index < Profile.Num(); ++Index)
		{
			if (!Profile[Index].bHit || (Index > 0 && FMath::Abs(Profile[Index].Z - Profile[Index - 1].Z) > StepMaxCm))
			{
				return false;
			}
		}
		return true;
	}
}
