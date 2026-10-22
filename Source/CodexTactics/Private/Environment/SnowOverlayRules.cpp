#include "Environment/SnowOverlayRules.h"
#include "Algo/StableSort.h"

namespace SnowOverlayRules
{
	FName OptOutTag()
	{
		static const FName Tag(TEXT("NoSnow"));
		return Tag;
	}

	bool PassesCheapChecks(const FSnowTargetTraits& Traits, const FSnowTargetConfig& Config)
	{
		const ESnowTargetVerdict Verdict = Classify(Traits, Config);
		return Verdict == ESnowTargetVerdict::Accept || Verdict == ESnowTargetVerdict::Interior || Verdict == ESnowTargetVerdict::Covered;
	}

	ESnowTargetVerdict Classify(const FSnowTargetTraits& Traits, const FSnowTargetConfig& Config)
	{
		if (!Traits.bStaticMesh)
		{
			return ESnowTargetVerdict::NotStaticMesh;
		}
		if (Traits.bInstanced)
		{
			return ESnowTargetVerdict::Instanced;
		}
		if (Traits.bPawn)
		{
			return ESnowTargetVerdict::Pawn;
		}
		if (Traits.bLandscape)
		{
			return ESnowTargetVerdict::Landscape;
		}
		if (!Traits.bVisible)
		{
			return ESnowTargetVerdict::Hidden;
		}
		if (Traits.bHasOverlay)
		{
			return ESnowTargetVerdict::HasOverlay;
		}
		if (Traits.bOptOut)
		{
			return ESnowTargetVerdict::OptOut;
		}
		if (Traits.bTranslucentOnly)
		{
			return ESnowTargetVerdict::TranslucentOnly;
		}
		if (Traits.bNanite && !Config.bAllowNaniteFallback)
		{
			return ESnowTargetVerdict::Nanite;
		}
		if (Traits.BoundsRadiusCm < Config.MinBoundsRadiusCm)
		{
			return ESnowTargetVerdict::TooSmall;
		}
		if (Config.MaxFootprintCm > 0.f && Traits.FootprintCm > Config.MaxFootprintCm)
		{
			return ESnowTargetVerdict::TooLarge;
		}
		if (Traits.FootprintCm >= Config.GroundMinFootprintCm && Traits.HeightCm < Traits.FootprintCm * Config.GroundMaxHeightShare)
		{
			return ESnowTargetVerdict::Ground;
		}
		if (Traits.bInterior)
		{
			return ESnowTargetVerdict::Interior;
		}
		if (Traits.bCovered)
		{
			return ESnowTargetVerdict::Covered;
		}
		return ESnowTargetVerdict::Accept;
	}

	bool IsTarget(const FSnowTargetTraits& Traits, const FSnowTargetConfig& Config)
	{
		return Classify(Traits, Config) == ESnowTargetVerdict::Accept;
	}

	const TCHAR* VerdictToString(ESnowTargetVerdict Verdict)
	{
		switch (Verdict)
		{
		case ESnowTargetVerdict::Accept: return TEXT("Accept");
		case ESnowTargetVerdict::NotStaticMesh: return TEXT("NotStaticMesh");
		case ESnowTargetVerdict::Instanced: return TEXT("Instanced");
		case ESnowTargetVerdict::Pawn: return TEXT("Pawn");
		case ESnowTargetVerdict::Landscape: return TEXT("Landscape");
		case ESnowTargetVerdict::Hidden: return TEXT("Hidden");
		case ESnowTargetVerdict::HasOverlay: return TEXT("HasOverlay");
		case ESnowTargetVerdict::OptOut: return TEXT("OptOut");
		case ESnowTargetVerdict::TranslucentOnly: return TEXT("TranslucentOnly");
		case ESnowTargetVerdict::Nanite: return TEXT("Nanite");
		case ESnowTargetVerdict::TooSmall: return TEXT("TooSmall");
		case ESnowTargetVerdict::TooLarge: return TEXT("TooLarge");
		case ESnowTargetVerdict::Ground: return TEXT("Ground");
		case ESnowTargetVerdict::Interior: return TEXT("Interior");
		case ESnowTargetVerdict::Covered: return TEXT("Covered");
		default: return TEXT("?");
		}
	}

	bool IsInsideInterior(const FVector& MeshMin, const FVector& MeshMax, const FTransform& ZoneTransform, const FVector& ZoneHalfExtent, float TopToleranceCm)
	{
		const FVector Centre = (MeshMin + MeshMax) * 0.5;
		const FVector Local = ZoneTransform.InverseTransformPositionNoScale(Centre);
		if (FMath::Abs(Local.X) > ZoneHalfExtent.X || FMath::Abs(Local.Y) > ZoneHalfExtent.Y || FMath::Abs(Local.Z) > ZoneHalfExtent.Z)
		{
			return false;
		}
		// The zone's top in world Z (zones are upright boxes; a tilted one uses its highest corner).
		double ZoneTop = -UE_BIG_NUMBER;
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector LocalCorner((Corner & 1) ? ZoneHalfExtent.X : -ZoneHalfExtent.X, (Corner & 2) ? ZoneHalfExtent.Y : -ZoneHalfExtent.Y,
				(Corner & 4) ? ZoneHalfExtent.Z : -ZoneHalfExtent.Z);
			ZoneTop = FMath::Max(ZoneTop, ZoneTransform.TransformPositionNoScale(LocalCorner).Z);
		}
		return MeshMax.Z <= ZoneTop + TopToleranceCm;
	}

	bool IsBelowInteriorRoof(double MeshTopZ, double RoofTopZ, double ToleranceCm)
	{
		return MeshTopZ < RoofTopZ - ToleranceCm;
	}

	bool ShouldShowOverlay(float SnowAmount, float FrostAmount)
	{
		return FMath::Max(SnowAmount, 0.f) + FMath::Max(FrostAmount, 0.f) > 0.01f;
	}

	TArray<int32> OrderCandidates(const TArray<FSnowCandidate>& Candidates, float MaxDistanceCm, int32 MaxCount, bool bKeepAll)
	{
		const double MaxSq = MaxDistanceCm > 0.f ? FMath::Square(static_cast<double>(MaxDistanceCm)) : -1.0;
		TArray<FSnowCandidate> Within;
		Within.Reserve(Candidates.Num());
		for (const FSnowCandidate& Candidate : Candidates)
		{
			if (MaxSq < 0.0 || Candidate.DistanceSq <= MaxSq)
			{
				Within.Add(Candidate);
			}
		}
		// Stable: equal distances keep the input order (deterministic between refreshes).
		Algo::StableSortBy(Within, &FSnowCandidate::DistanceSq);
		TArray<int32> Order;
		Order.Reserve(Within.Num());
		for (const FSnowCandidate& Candidate : Within)
		{
			if (!bKeepAll && MaxCount > 0 && Order.Num() >= MaxCount)
			{
				break;
			}
			Order.Add(Candidate.Index);
		}
		return Order;
	}
}
