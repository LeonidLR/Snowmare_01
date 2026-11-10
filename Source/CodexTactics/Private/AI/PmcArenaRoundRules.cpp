#include "AI/PmcArenaRoundRules.h"

bool PmcArenaRoundRules::IsLauncherContact(const FVector& Burst, const TArray<FVector>& LiveEnemies, float RadiusCm)
{
	if (RadiusCm <= 0.f)
	{
		return false;
	}
	const double RadiusSq = static_cast<double>(RadiusCm) * RadiusCm;
	for (const FVector& Enemy : LiveEnemies)
	{
		if (FVector::DistSquared(Burst, Enemy) <= RadiusSq)
		{
			return true;
		}
	}
	return false;
}

FPmcArenaFlow PmcArenaRoundRules::ComputeFlow(TArray<float> ContactTimes, float DurationS)
{
	FPmcArenaFlow Flow;
	const float Duration = FMath::Max(DurationS, 0.f);
	ContactTimes.Sort();
	Flow.Events = ContactTimes.Num();
	Flow.bContact = Flow.Events > 0;
	Flow.EventsPerMin = Flow.Events / (FMath::Max(Duration, 1.f) / 60.f);
	if (!Flow.bContact)
	{
		Flow.LongestQuietS = Duration;
		return Flow;
	}
	Flow.FirstContactS = ContactTimes[0];
	float Longest = FMath::Max(0.f, Duration - ContactTimes.Last());
	for (int32 Index = 1; Index < ContactTimes.Num(); ++Index)
	{
		Longest = FMath::Max(Longest, ContactTimes[Index] - ContactTimes[Index - 1]);
	}
	Flow.LongestQuietS = Longest;
	return Flow;
}

bool PmcArenaRoundRules::IsFullPath(bool bFound, bool bPartial)
{
	return bFound && !bPartial;
}

bool PmcArenaRoundRules::IsReachableFromBoth(const FVector& Point, const FVector& SpawnA, const FVector& SpawnB,
	TFunctionRef<bool(const FVector& From, const FVector& To)> HasFullPath)
{
	return HasFullPath(SpawnA, Point) && HasFullPath(SpawnB, Point);
}

int32 PmcArenaRoundRules::PickReachable(const TArray<FVector>& Candidates, const FVector& SpawnA, const FVector& SpawnB,
	TFunctionRef<bool(const FVector& From, const FVector& To)> HasFullPath)
{
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		if (IsReachableFromBoth(Candidates[Index], SpawnA, SpawnB, HasFullPath))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}
