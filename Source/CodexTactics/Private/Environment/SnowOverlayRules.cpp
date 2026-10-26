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

	/** The checks shared by the object and the ground overlay (Accept = none failed). */
	static ESnowTargetVerdict ClassifyCommon(const FSnowTargetTraits& Traits, const FSnowTargetConfig& Config)
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
		return ESnowTargetVerdict::Accept;
	}

	bool IsGroundShape(const FSnowTargetTraits& Traits, const FSnowTargetConfig& Config)
	{
		const float Width = Traits.MinWidthCm > 0.f ? Traits.MinWidthCm : Traits.FootprintCm;
		if (Width < Config.GroundMinWidthCm)
		{
			return false;
		}
		const bool bLowForWide = Traits.HeightCm < Traits.FootprintCm * Config.GroundWideMaxHeightShare;
		if (Traits.bFloorTemplate)
		{
			return bLowForWide;
		}
		if (Traits.FootprintCm >= Config.GroundMinFootprintCm && Traits.HeightCm < Traits.FootprintCm * Config.GroundMaxHeightShare)
		{
			return true;
		}
		return Config.MaxFootprintCm > 0.f && Traits.FootprintCm > Config.MaxFootprintCm && bLowForWide;
	}

	ESnowTargetVerdict Classify(const FSnowTargetTraits& Traits, const FSnowTargetConfig& Config)
	{
		const ESnowTargetVerdict Common = ClassifyCommon(Traits, Config);
		if (Common != ESnowTargetVerdict::Accept)
		{
			return Common;
		}
		if (Traits.BoundsRadiusCm < Config.MinBoundsRadiusCm)
		{
			return ESnowTargetVerdict::TooSmall;
		}
		// Ground slabs take the ground overlay (ClassifyGround), whatever their size.
		if (IsGroundShape(Traits, Config))
		{
			return ESnowTargetVerdict::Ground;
		}
		if (Config.MaxFootprintCm > 0.f && Traits.FootprintCm > Config.MaxFootprintCm)
		{
			return ESnowTargetVerdict::TooLarge;
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

	ESnowTargetVerdict ClassifyGround(const FSnowTargetTraits& Traits, const FSnowTargetConfig& Config)
	{
		const ESnowTargetVerdict Common = ClassifyCommon(Traits, Config);
		if (Common != ESnowTargetVerdict::Accept)
		{
			return Common;
		}
		if (!IsGroundShape(Traits, Config))
		{
			return ESnowTargetVerdict::NotGround;
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

	bool IsFloorTemplateMeshName(const FString& MeshName)
	{
		return MeshName.Contains(TEXT("Template_Map_Floor"), ESearchCase::IgnoreCase);
	}

	TArray<FVector2D> GroundCoverSamplePoints(const FVector2D& BoundsMin, const FVector2D& BoundsMax)
	{
		const FVector2D Centre = (BoundsMin + BoundsMax) * 0.5;
		const FVector2D Quarter = (BoundsMax - BoundsMin) * 0.25;
		return { Centre, Centre + FVector2D(-Quarter.X, -Quarter.Y), Centre + FVector2D(Quarter.X, -Quarter.Y), Centre + FVector2D(-Quarter.X, Quarter.Y),
			Centre + FVector2D(Quarter.X, Quarter.Y) };
	}

	bool IsGroundCovered(int32 CoveredSamples, int32 Samples)
	{
		return Samples > 0 && CoveredSamples * 2 > Samples;
	}

	double DistanceSqToBox2D(const FVector& Point, const FVector& BoxMin, const FVector& BoxMax)
	{
		const double Dx = FMath::Max3(BoxMin.X - Point.X, 0.0, Point.X - BoxMax.X);
		const double Dy = FMath::Max3(BoxMin.Y - Point.Y, 0.0, Point.Y - BoxMax.Y);
		return Dx * Dx + Dy * Dy;
	}

	void MakeGroundZoneParams(const FTransform& ZoneTransform, const FVector& ZoneHalfExtent, FLinearColor& OutA, FLinearColor& OutB)
	{
		const FVector Centre = ZoneTransform.GetLocation();
		const double YawRad = FMath::DegreesToRadians(ZoneTransform.Rotator().Yaw);
		double Top = -UE_BIG_NUMBER;
		double Bottom = UE_BIG_NUMBER;
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector LocalCorner((Corner & 1) ? ZoneHalfExtent.X : -ZoneHalfExtent.X, (Corner & 2) ? ZoneHalfExtent.Y : -ZoneHalfExtent.Y,
				(Corner & 4) ? ZoneHalfExtent.Z : -ZoneHalfExtent.Z);
			const double Z = ZoneTransform.TransformPositionNoScale(LocalCorner).Z;
			Top = FMath::Max(Top, Z);
			Bottom = FMath::Min(Bottom, Z);
		}
		OutA = FLinearColor(static_cast<float>(Centre.X), static_cast<float>(Centre.Y), static_cast<float>(ZoneHalfExtent.X), static_cast<float>(ZoneHalfExtent.Y));
		OutB = FLinearColor(static_cast<float>(FMath::Cos(YawRad)), static_cast<float>(FMath::Sin(YawRad)), static_cast<float>(Top), static_cast<float>(Bottom));
	}

	float GroundZoneMask(const FVector& Point, const FLinearColor& A, const FLinearColor& B, float SoftCm)
	{
		if (A.B <= 0.f)
		{
			return 0.f;
		}
		const float Dx = static_cast<float>(Point.X) - A.R;
		const float Dy = static_cast<float>(Point.Y) - A.G;
		const float Lx = Dx * B.R + Dy * B.G;
		const float Ly = -Dx * B.G + Dy * B.R;
		const float Ex = A.B - FMath::Abs(Lx);
		const float Ey = A.A - FMath::Abs(Ly);
		float Mask = FMath::Clamp(FMath::Min(Ex, Ey) / FMath::Max(SoftCm, 1.f) + 0.5f, 0.f, 1.f);
		const float Z = static_cast<float>(Point.Z);
		Mask *= (Z <= B.B ? 1.f : 0.f) * (Z >= B.A - 100.f ? 1.f : 0.f);
		return Mask;
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
		case ESnowTargetVerdict::NotGround: return TEXT("NotGround");
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

	bool ShouldShowWetOverlay(float Wetness, float PuddleAmount)
	{
		return FMath::Max(Wetness, 0.f) > 0.005f || FMath::Max(PuddleAmount, 0.f) > 0.005f;
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
