#include "Camera/InteriorExitRules.h"

namespace InteriorExitRules
{
	void BuildEdgeSamples(const FVector2D& HalfExtent, int32 Edge, const FExitScanConfig& Config, TArray<FVector2D>& OutPoints,
		FVector2D& OutNormal)
	{
		OutPoints.Reset();
		const bool bAlongY = Edge == 0 || Edge == 1; // the X edges run along local Y
		const float Fixed = bAlongY ? (Edge == 0 ? -HalfExtent.X : HalfExtent.X) : (Edge == 2 ? -HalfExtent.Y : HalfExtent.Y);
		OutNormal = bAlongY ? FVector2D(Edge == 0 ? -1.f : 1.f, 0.f) : FVector2D(0.f, Edge == 2 ? -1.f : 1.f);
		const float Half = bAlongY ? HalfExtent.Y : HalfExtent.X;
		const float Spacing = FMath::Max(Config.SampleSpacingCm, 1.f);
		for (float Along = -Half + Config.CornerMarginCm; Along <= Half - Config.CornerMarginCm + KINDA_SMALL_NUMBER; Along += Spacing)
		{
			OutPoints.Add(bAlongY ? FVector2D(Fixed, Along) : FVector2D(Along, Fixed));
		}
	}

	TArray<FDoorRun> FindDoorRuns(const TArray<bool>& Crossable, float SpacingCm, float MinWidthCm)
	{
		TArray<FDoorRun> Runs;
		int32 Start = INDEX_NONE;
		for (int32 Index = 0; Index <= Crossable.Num(); ++Index)
		{
			const bool bOpen = Crossable.IsValidIndex(Index) && Crossable[Index];
			if (bOpen && Start == INDEX_NONE)
			{
				Start = Index;
			}
			else if (!bOpen && Start != INDEX_NONE)
			{
				FDoorRun Run;
				Run.First = Start;
				Run.Last = Index - 1;
				Run.WidthCm = (Run.Last - Run.First + 1) * SpacingCm;
				if (Run.WidthCm >= MinWidthCm)
				{
					Runs.Add(Run);
				}
				Start = INDEX_NONE;
			}
		}
		return Runs;
	}

	EExitOrderScope GetExitOrderScope(bool bSoloMode)
	{
		return bSoloMode ? EExitOrderScope::Selected : EExitOrderScope::Squad;
	}

	bool ShouldShowZoneCamera(bool bZoneActive, bool bHasCamera, bool bModeAllowed, bool bCinematic, bool bPlayerOutsideView)
	{
		return bZoneActive && bHasCamera && bModeAllowed && !bCinematic && !bPlayerOutsideView;
	}

	bool ShouldShowExitArrows(bool bZoneCameraShown, bool bTurnBased)
	{
		return bZoneCameraShown && !bTurnBased;
	}
}
