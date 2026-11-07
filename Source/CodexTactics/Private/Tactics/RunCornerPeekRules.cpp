// RunCornerPeekRules: the run-and-peek click (#55, user request 2026-11-07) - when a click on a wall's end sends an operative from the open
// straight into the corner peek, which face of the wall he takes, when his cover entry is that peek's arrival, and who carries it out.
// Godot reference: none (UE-only; docs/systems/cover-and-peek.md "Run-and-peek").

#include "Tactics/RunCornerPeekRules.h"

namespace RunCornerPeekRules
{
	bool MayRunToPeek(const FClickFacts& Facts)
	{
		return Facts.bEnabled && !Facts.bTurnBased && !Facts.bPausePlanning && !Facts.bSquadWide && !Facts.bTutorialLocked && !Facts.bHitPawn
			&& !Facts.bHitObject && !Facts.bInCoverAtClickedObject && !Facts.bCoverSwitchTarget;
	}

	bool IsFaceWideEnough(bool bLeftExposed, float LeftCm, bool bRightExposed, float RightCm, float MinWidthCm)
	{
		if (!bLeftExposed || !bRightExposed)
		{
			return true; // a closed side: the wall goes on past the corner probes
		}
		return FMath::Max(LeftCm, 0.f) + FMath::Max(RightCm, 0.f) >= MinWidthCm;
	}

	bool IsNewFace(const TArray<FVector2D>& Known, const FVector2D& Normal)
	{
		const FVector2D Unit = Normal.GetSafeNormal();
		if (Unit.IsNearlyZero())
		{
			return false;
		}
		return !Known.ContainsByPredicate([&Unit](const FVector2D& Each) { return FVector2D::DotProduct(Each.GetSafeNormal(), Unit) >= SameFaceDot; });
	}

	int32 ChooseNearFace(const TArray<FFace>& Faces, const FVector2D& Operative)
	{
		int32 Best = INDEX_NONE;
		double BestScore = 0.0;
		double BestDistance = 0.0;
		for (int32 Index = 0; Index < Faces.Num(); ++Index)
		{
			const FFace& Face = Faces[Index];
			const FVector2D ToHim = Operative - Face.Point;
			const double Distance = ToHim.Size();
			const double Score = Distance > KINDA_SMALL_NUMBER ? FVector2D::DotProduct(Face.Normal.GetSafeNormal(), ToHim / Distance) : 0.0;
			const bool bBetter = Best == INDEX_NONE || Score > BestScore + 0.01 || (Score >= BestScore - 0.01 && Distance < BestDistance);
			if (bBetter)
			{
				Best = Index;
				BestScore = Score;
				BestDistance = Distance;
			}
		}
		return Best;
	}

	TArray<FVector2D> ProbeDirections(const FVector2D& Primary, int32 Count)
	{
		TArray<FVector2D> Result;
		FVector2D First = Primary.GetSafeNormal();
		if (First.IsNearlyZero())
		{
			First = FVector2D(1.0, 0.0);
		}
		const int32 Num = FMath::Max(Count, 1);
		for (int32 Index = 0; Index < Num; ++Index)
		{
			const double Angle = 2.0 * UE_DOUBLE_PI * Index / Num;
			const double Cos = FMath::Cos(Angle);
			const double Sin = FMath::Sin(Angle);
			Result.Add(FVector2D(First.X * Cos - First.Y * Sin, First.X * Sin + First.Y * Cos));
		}
		return Result;
	}

	bool IsPeekArrival(bool bArmed, bool bSameWall, float DistanceFromOrderedCm, float ToleranceCm)
	{
		return bArmed && bSameWall && DistanceFromOrderedCm <= ToleranceCm;
	}

	EGroup DecideGroup(bool bMultiSelection, bool bOperativeSelected, bool bGroupCornerPeekOn, bool bSoloMode, int32 GroupCoverOthers)
	{
		if (bMultiSelection && bOperativeSelected)
		{
			return bGroupCornerPeekOn ? EGroup::BoxSelection : EGroup::Single;
		}
		if (!bSoloMode && GroupCoverOthers > 0)
		{
			return EGroup::Formation;
		}
		return EGroup::Single;
	}
}
