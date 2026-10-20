// Wall-edge cover switch rules (game designer 2026-10-20; UE-only, no Godot reference). See Tactics/CoverSwitchRules.h.

#include "Tactics/CoverSwitchRules.h"

#include "Tactics/CoverFacingRules.h"

namespace CoverSwitchRules
{
	bool FindSwitchEdge(const FCoverSlot& Current, const FVector& Toward, float ReachCm, ECoverFacing& OutEdge)
	{
		if (!Current.IsValid())
		{
			return false;
		}
		bool bFound = false;
		float BestScore = -TNumericLimits<float>::Max();
		const FVector ToTarget = (Toward - Current.WorldLocation).GetSafeNormal2D();
		for (const ECoverFacing Side : { ECoverFacing::Left, ECoverFacing::Right })
		{
			const float Distance = CoverFacingRules::EdgeDistance(Current, Side);
			if (Distance < 0.f || Distance > ReachCm)
			{
				continue;
			}
			const float Score = static_cast<float>(FVector::DotProduct(CoverFacingRules::AlongWallDirection(Current, Side), ToTarget));
			if (!bFound || Score > BestScore)
			{
				bFound = true;
				BestScore = Score;
				OutEdge = Side;
			}
		}
		return bFound;
	}

	ECoverSwitchVerdict Evaluate(bool bInCover, bool bShimmying, const FCoverSlot& Current, const FCoverSlot& Target, bool bSameWall,
		const FCoverSwitchConfig& Config)
	{
		if (!Config.bEnabled)
		{
			return ECoverSwitchVerdict::Disabled;
		}
		if (!bInCover || !Current.IsValid())
		{
			return ECoverSwitchVerdict::NotInCover;
		}
		if (bShimmying)
		{
			return ECoverSwitchVerdict::Shimmying;
		}
		if (!Target.IsValid())
		{
			return ECoverSwitchVerdict::NoTarget;
		}
		ECoverFacing Edge = ECoverFacing::Right;
		if (!FindSwitchEdge(Current, Target.WorldLocation, Config.EdgeReachCm, Edge))
		{
			return ECoverSwitchVerdict::NotAtEdge;
		}
		if (bSameWall)
		{
			return ECoverSwitchVerdict::SameWall;
		}
		const float Distance = static_cast<float>(FVector::Dist2D(Current.WorldLocation, Target.WorldLocation));
		if (Distance < Config.MinRangeCm)
		{
			return ECoverSwitchVerdict::TooClose;
		}
		if (Distance > Config.MaxRangeCm)
		{
			return ECoverSwitchVerdict::OutOfRange;
		}
		return ECoverSwitchVerdict::Switch;
	}

	const TCHAR* VerdictName(ECoverSwitchVerdict Verdict)
	{
		switch (Verdict)
		{
		case ECoverSwitchVerdict::Switch: return TEXT("switch");
		case ECoverSwitchVerdict::Disabled: return TEXT("disabled");
		case ECoverSwitchVerdict::NotInCover: return TEXT("not in cover");
		case ECoverSwitchVerdict::Shimmying: return TEXT("shimmying");
		case ECoverSwitchVerdict::NotAtEdge: return TEXT("not at a wall edge");
		case ECoverSwitchVerdict::SameWall: return TEXT("his own wall");
		case ECoverSwitchVerdict::TooClose: return TEXT("too close");
		case ECoverSwitchVerdict::OutOfRange: return TEXT("out of range");
		case ECoverSwitchVerdict::NoTarget: return TEXT("no target");
		default: return TEXT("?");
		}
	}

	bool IsSwitchConfirm(bool bDoubleClick, ECoverSwitchVerdict Verdict, bool bLocked)
	{
		return bDoubleClick && Verdict == ECoverSwitchVerdict::Switch && !bLocked;
	}

	ECoverSwitchApproach ClassifyApproach(const FCoverSlot& Target, const FVector& From)
	{
		const FVector Delta = FVector(From.X - Target.WorldLocation.X, From.Y - Target.WorldLocation.Y, 0.f);
		const float Out = static_cast<float>(FVector::DotProduct(Delta, Target.WallNormal.GetSafeNormal2D()));
		const float Along = static_cast<float>(FVector::DotProduct(Delta, Target.RightTangent()));
		if (Out >= FMath::Abs(Along))
		{
			return ECoverSwitchApproach::Front;
		}
		return Along >= 0.f ? ECoverSwitchApproach::FromPackL : ECoverSwitchApproach::FromPackR;
	}

	int32 ClipArrayIndex(ECoverSwitchApproach Approach, ECoverFacing ArrivalFacing)
	{
		return static_cast<int32>(Approach) * 2 + CoverFacingRules::ClipIndex(ArrivalFacing);
	}

	FString ClipName(ECoverSwitchApproach Approach, ECoverFacing ArrivalFacing, bool bCrouch)
	{
		const TCHAR* From = Approach == ECoverSwitchApproach::FromPackL ? TEXT("_L") : Approach == ECoverSwitchApproach::FromPackR ? TEXT("_R") : TEXT("");
		const TCHAR* End = CoverFacingRules::ClipIndex(ArrivalFacing) == 0 ? TEXT("L") : TEXT("R");
		return FString::Printf(TEXT("anim_M4_std_jog_fwd%s_to_cvr_%s_idle_%s"), From, bCrouch ? TEXT("crch") : TEXT("std"), End);
	}

	FVector2D OffsetAt(const FCoverSwitchTrack& Track, float Time)
	{
		if (!Track.IsValid())
		{
			return FVector2D::ZeroVector;
		}
		if (Time <= Track.Times[0])
		{
			return Track.Offsets[0];
		}
		for (int32 Index = 1; Index < Track.Times.Num(); ++Index)
		{
			if (Time <= Track.Times[Index])
			{
				const float Span = FMath::Max(Track.Times[Index] - Track.Times[Index - 1], UE_KINDA_SMALL_NUMBER);
				const float Alpha = FMath::Clamp((Time - Track.Times[Index - 1]) / Span, 0.f, 1.f);
				return FMath::Lerp(Track.Offsets[Index - 1], Track.Offsets[Index], Alpha);
			}
		}
		return Track.Offsets.Last();
	}

	FCoverSwitchClipStart ChooseClipStart(const TArray<FCoverSwitchTrack>& TracksByApproach, const FVector2D& RunnerOffset, float MinClipLeftSeconds,
		const FVector2D& RunnerDirection, float MinHeadingDot)
	{
		FCoverSwitchClipStart Best;
		float BestMismatch = TNumericLimits<float>::Max();
		const FVector2D Heading = RunnerDirection.GetSafeNormal();
		for (int32 Approach = 0; Approach < TracksByApproach.Num(); ++Approach)
		{
			const FCoverSwitchTrack& Track = TracksByApproach[Approach];
			if (!Track.IsValid())
			{
				continue;
			}
			const float Latest = FMath::Max(Track.Length() - MinClipLeftSeconds, 0.f);
			for (int32 Index = 0; Index < Track.Times.Num() && Track.Times[Index] <= Latest + UE_KINDA_SMALL_NUMBER; ++Index)
			{
				if (!Heading.IsNearlyZero())
				{
					// The clip body's direction of travel there (towards the next sample; the last one looks back).
					const int32 From = FMath::Min(Index, Track.Offsets.Num() - 2);
					const FVector2D Motion = (Track.Offsets[From + 1] - Track.Offsets[From]).GetSafeNormal();
					if (Motion.IsNearlyZero() || FVector2D::DotProduct(Motion, Heading) < MinHeadingDot)
					{
						continue;
					}
				}
				const float Mismatch = static_cast<float>(FVector2D::Distance(RunnerOffset, Track.Offsets[Index]));
				if (Mismatch < BestMismatch)
				{
					BestMismatch = Mismatch;
					Best.Approach = Approach;
					Best.StartSeconds = Track.Times[Index];
					Best.ClipOffset = Track.Offsets[Index];
					Best.MismatchCm = Mismatch;
				}
			}
		}
		return Best;
	}
}
