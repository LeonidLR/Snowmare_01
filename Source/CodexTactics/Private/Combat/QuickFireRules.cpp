#include "Combat/QuickFireRules.h"

namespace QuickFireRules
{
	const FQuickFireConfig& GetConfig()
	{
		static const FQuickFireConfig Config;
		return Config;
	}

	int32 PickTarget(const FVector& Point, const TArray<FQuickFireCandidate>& Candidates, float RadiusCm)
	{
		int32 Best = INDEX_NONE;
		float BestDistSq = FMath::Square(FMath::Max(0.f, RadiusCm));
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			const FQuickFireCandidate& Candidate = Candidates[Index];
			if (!Candidate.bVisible || !Candidate.bAlive)
			{
				continue;
			}
			const float DistSq = static_cast<float>(FVector::DistSquared2D(Point, Candidate.Location));
			if (DistSq <= BestDistSq)
			{
				Best = Index;
				BestDistSq = DistSq;
			}
		}
		return Best;
	}

	int32 GetBurstRounds(bool bUsesAmmo, bool bSniper, int32 MaxClipSize, const FQuickFireConfig& Config)
	{
		if (!bUsesAmmo)
		{
			return 0;
		}
		if (bSniper || MaxClipSize < Config.BurstMinClipSize)
		{
			return 1;
		}
		return FMath::Max(1, Config.BurstRounds);
	}

	FVector ComputeRoundEnd(const FVector& Muzzle, const FVector& Aim, float RangeCm, const FQuickFireConfig& Config)
	{
		const FVector Delta = Aim - Muzzle;
		const float Length = static_cast<float>(Delta.Size());
		if (Length <= KINDA_SMALL_NUMBER)
		{
			return Aim;
		}
		const float Reach = FMath::Min(Length + FMath::Max(0.f, Config.OvershootCm), FMath::Max(0.f, RangeCm));
		return Muzzle + Delta / Length * Reach;
	}

	int32 FindFirstCapsuleOnSegment(const FVector& Start, const FVector& End, const TArray<FQuickFireCapsule>& Capsules, float ToleranceCm,
		float& OutAlongCm)
	{
		int32 Best = INDEX_NONE;
		OutAlongCm = TNumericLimits<float>::Max();
		for (int32 Index = 0; Index < Capsules.Num(); ++Index)
		{
			const FQuickFireCapsule& Capsule = Capsules[Index];
			const float AxisHalf = FMath::Max(0.f, Capsule.HalfHeight - Capsule.Radius);
			const FVector AxisA = Capsule.Center - FVector(0.f, 0.f, AxisHalf);
			const FVector AxisB = Capsule.Center + FVector(0.f, 0.f, AxisHalf);
			FVector OnShot;
			FVector OnAxis;
			FMath::SegmentDistToSegmentSafe(Start, End, AxisA, AxisB, OnShot, OnAxis);
			if (FVector::DistSquared(OnShot, OnAxis) > FMath::Square(Capsule.Radius + FMath::Max(0.f, ToleranceCm)))
			{
				continue;
			}
			const float Along = static_cast<float>(FVector::Dist(Start, OnShot));
			if (Along < OutAlongCm)
			{
				OutAlongCm = Along;
				Best = Index;
			}
		}
		return Best;
	}

	float GetAreaHitChance(float WeaponChance, const FQuickFireConfig& Config)
	{
		return FMath::Clamp(WeaponChance * Config.AreaFireHitMultiplier, 0.f, 1.f);
	}

	float GetSpreadDeg(int32 StanceIndex, const FQuickFireConfig& Config)
	{
		return StanceIndex >= 2 ? Config.SpreadProneDeg : (StanceIndex == 1 ? Config.SpreadCrouchingDeg : Config.SpreadStandingDeg);
	}
}
