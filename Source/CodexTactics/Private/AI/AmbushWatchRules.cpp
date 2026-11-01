#include "AI/AmbushWatchRules.h"

float AmbushWatchRules::BearingDeg(const FVector2D& From, const FVector2D& To)
{
	const FVector2D Delta = To - From;
	return Delta.IsNearlyZero() ? 0.f : static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X)));
}

float AmbushWatchRules::AngleOffDeg(float LookYawDeg, float BearingDeg)
{
	return FMath::Abs(FMath::FindDeltaAngleDegrees(LookYawDeg, BearingDeg));
}

int32 AmbushWatchRules::FindWatcher(const TArray<FAmbushWatcher>& Watchers, const FVector2D& Ambusher, const FAmbushWatchParams& Params)
{
	for (int32 Index = 0; Index < Watchers.Num(); ++Index)
	{
		const FAmbushWatcher& Watcher = Watchers[Index];
		if (FVector2D::Distance(Watcher.Position, Ambusher) > Params.WatchRangeCm)
		{
			continue;
		}
		if (AngleOffDeg(Watcher.LookYawDeg, BearingDeg(Watcher.Position, Ambusher)) <= Params.WatchHalfAngleDeg)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

bool AmbushWatchRules::IsWatched(const TArray<FAmbushWatcher>& Watchers, const FVector2D& Ambusher, const FAmbushWatchParams& Params)
{
	return FindWatcher(Watchers, Ambusher, Params) != INDEX_NONE;
}

float AmbushWatchRules::WatchMarginDeg(const TArray<FAmbushWatcher>& Watchers, const FVector2D& Ambusher, const FAmbushWatchParams& Params)
{
	float Margin = 180.f;
	for (const FAmbushWatcher& Watcher : Watchers)
	{
		if (FVector2D::Distance(Watcher.Position, Ambusher) <= Params.WatchRangeCm)
		{
			Margin = FMath::Min(Margin, AngleOffDeg(Watcher.LookYawDeg, BearingDeg(Watcher.Position, Ambusher)) - Params.WatchHalfAngleDeg);
		}
	}
	return Margin;
}

TArray<FAmbushWatcher> AmbushWatchRules::MemberLooks(const FVector2D& Position, float BodyYawDeg, const TOptional<float>& AimYawDeg,
	const TOptional<float>& SectorYawDeg, float DedupeDeg)
{
	TArray<FAmbushWatcher> Out;
	const auto Add = [&Out, &Position, DedupeDeg](float Yaw, EAmbushLookSource Source)
	{
		for (const FAmbushWatcher& Each : Out)
		{
			if (AngleOffDeg(Each.LookYawDeg, Yaw) <= DedupeDeg)
			{
				return;
			}
		}
		Out.Add({ Position, static_cast<float>(FMath::UnwindDegrees(Yaw)), Source });
	};
	Add(BodyYawDeg, EAmbushLookSource::Body);
	if (AimYawDeg.IsSet())
	{
		Add(AimYawDeg.GetValue(), EAmbushLookSource::Aim);
	}
	if (SectorYawDeg.IsSet())
	{
		Add(SectorYawDeg.GetValue(), EAmbushLookSource::Sector);
	}
	return Out;
}

float AmbushWatchRules::NearestDistance(const TArray<FVector2D>& Positions, const FVector2D& Point)
{
	float Best = MAX_flt;
	for (const FVector2D& Each : Positions)
	{
		Best = FMath::Min(Best, static_cast<float>(FVector2D::Distance(Each, Point)));
	}
	return Best;
}

EAmbushDecision AmbushWatchRules::Decide(const TArray<FAmbushWatcher>& Watchers, const TArray<FVector2D>& Squad, const FVector2D& Ambusher,
	const FAmbushWatchParams& Params)
{
	if (Squad.IsEmpty() || NearestDistance(Squad, Ambusher) > Params.StrikeRangeCm)
	{
		return EAmbushDecision::Wait;
	}
	return IsWatched(Watchers, Ambusher, Params) ? EAmbushDecision::Hold : EAmbushDecision::Spring;
}

TOptional<float> AmbushWatchRules::WidestUnwatchedBearing(const TArray<FAmbushWatcher>& Watchers, const FAmbushWatchParams& Params)
{
	if (Watchers.IsEmpty())
	{
		return 0.f;
	}
	const float Half = FMath::Clamp(Params.WatchHalfAngleDeg, 0.f, 180.f);
	if (Half >= 180.f)
	{
		return {};
	}
	// Covered arcs as [start, end) on 0..360, a wrapping arc split in two.
	TArray<TPair<float, float>> Arcs;
	for (const FAmbushWatcher& Watcher : Watchers)
	{
		const float Start = FMath::Fmod(Watcher.LookYawDeg - Half + 720.f, 360.f);
		const float End = Start + 2.f * Half;
		if (End <= 360.f)
		{
			Arcs.Add({ Start, End });
		}
		else
		{
			Arcs.Add({ Start, 360.f });
			Arcs.Add({ 0.f, End - 360.f });
		}
	}
	Arcs.Sort([](const TPair<float, float>& A, const TPair<float, float>& B) { return A.Key < B.Key; });
	// Merge, then the gaps between the merged arcs (the last gap wraps round 360 to the first arc).
	TArray<TPair<float, float>> Merged;
	for (const TPair<float, float>& Arc : Arcs)
	{
		if (!Merged.IsEmpty() && Arc.Key <= Merged.Last().Value)
		{
			Merged.Last().Value = FMath::Max(Merged.Last().Value, Arc.Value);
		}
		else
		{
			Merged.Add(Arc);
		}
	}
	float BestGap = 0.f;
	float BestMid = 0.f;
	for (int32 Index = 0; Index < Merged.Num(); ++Index)
	{
		const float GapStart = Merged[Index].Value;
		const float GapEnd = Index + 1 < Merged.Num() ? Merged[Index + 1].Key : Merged[0].Key + 360.f;
		const float Gap = GapEnd - GapStart;
		if (Gap > BestGap)
		{
			BestGap = Gap;
			BestMid = GapStart + Gap * 0.5f;
		}
	}
	if (BestGap <= KINDA_SMALL_NUMBER)
	{
		return {};
	}
	return static_cast<float>(FMath::UnwindDegrees(BestMid));
}
