#include "UI/HordeArrowRules.h"

namespace HordeArrowRules
{
	float AngleDeltaDeg(float FromDeg, float ToDeg)
	{
		float Delta = FMath::Fmod(ToDeg - FromDeg, 360.f);
		if (Delta > 180.f)
		{
			Delta -= 360.f;
		}
		else if (Delta <= -180.f)
		{
			Delta += 360.f;
		}
		return Delta;
	}

	TArray<FGroup> GroupByBearing(const FVector& Origin, const TArray<FVector>& Positions, float HideRangeCm, float MergeAngleDeg)
	{
		struct FMember
		{
			float Bearing = 0.f;
			float Distance = 0.f;
			FVector Location = FVector::ZeroVector;
		};
		TArray<FMember> Members;
		for (const FVector& Position : Positions)
		{
			const FVector2D Flat(Position.X - Origin.X, Position.Y - Origin.Y);
			const float Distance = Flat.Size();
			if (Distance < FMath::Max(HideRangeCm, 1.f))
			{
				continue; // engaged: it is on top of us, no direction to warn about
			}
			FMember& Member = Members.AddDefaulted_GetRef();
			Member.Bearing = FMath::RadiansToDegrees(FMath::Atan2(Flat.Y, Flat.X));
			Member.Distance = Distance;
			Member.Location = Position;
		}
		TArray<FGroup> Result;
		if (Members.IsEmpty())
		{
			return Result;
		}
		Members.Sort([](const FMember& A, const FMember& B) { return A.Bearing < B.Bearing; });
		const int32 Num = Members.Num();
		// Split the circle at every gap of at least MergeAngleDeg; start right after the largest gap so no group straddles the seam.
		int32 Start = 0;
		float LargestGap = -1.f;
		for (int32 Index = 0; Index < Num; ++Index)
		{
			const float Gap = Num == 1 ? 360.f : FMath::Fmod(Members[(Index + 1) % Num].Bearing - Members[Index].Bearing + 720.f, 360.f);
			if (Gap > LargestGap)
			{
				LargestGap = Gap;
				Start = (Index + 1) % Num;
			}
		}
		TArray<TArray<int32>> Clusters;
		for (int32 Step = 0; Step < Num; ++Step)
		{
			const int32 Index = (Start + Step) % Num;
			bool bNew = Step == 0;
			if (!bNew)
			{
				const int32 Previous = (Start + Step - 1) % Num;
				bNew = FMath::Fmod(Members[Index].Bearing - Members[Previous].Bearing + 720.f, 360.f) >= MergeAngleDeg;
			}
			if (bNew)
			{
				Clusters.AddDefaulted();
			}
			Clusters.Last().Add(Index);
		}
		for (const TArray<int32>& Cluster : Clusters)
		{
			FGroup& Group = Result.AddDefaulted_GetRef();
			FVector2D Dir = FVector2D::ZeroVector;
			Group.NearestCm = TNumericLimits<float>::Max();
			for (const int32 Index : Cluster)
			{
				const FMember& Member = Members[Index];
				const float Radians = FMath::DegreesToRadians(Member.Bearing);
				Dir += FVector2D(FMath::Cos(Radians), FMath::Sin(Radians));
				Group.Centroid += Member.Location;
				Group.NearestCm = FMath::Min(Group.NearestCm, Member.Distance);
				++Group.Count;
			}
			Group.Centroid /= static_cast<double>(Group.Count);
			Group.BearingDeg = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
		}
		Result.Sort([](const FGroup& A, const FGroup& B) { return A.NearestCm < B.NearestCm; });
		return Result;
	}

	void SpreadTips(const FVector2D& ViewSize, float MarginPx, float MinGapPx, const TArray<FVector2D>& Occupied, const TArray<FVector2D>& Directions,
		TArray<FVector2D>& InOutTips)
	{
		TArray<FVector2D> Taken = Occupied;
		for (int32 Index = 0; Index < InOutTips.Num(); ++Index)
		{
			const FVector2D Direction = Directions.IsValidIndex(Index) ? Directions[Index] : FVector2D(1.f, 0.f);
			const FVector2D Side(-Direction.Y, Direction.X);
			const FVector2D Origin = InOutTips[Index];
			FVector2D Best = Origin;
			for (int32 Try = 0; Try <= 6; ++Try)
			{
				const float Offset = ((Try + 1) / 2) * MinGapPx * (Try % 2 == 1 ? 1.f : -1.f);
				FVector2D Candidate = Origin + Side * Offset;
				Candidate.X = FMath::Clamp(Candidate.X, MarginPx, FMath::Max(MarginPx, ViewSize.X - MarginPx));
				Candidate.Y = FMath::Clamp(Candidate.Y, MarginPx, FMath::Max(MarginPx, ViewSize.Y - MarginPx));
				bool bFree = true;
				for (const FVector2D& Other : Taken)
				{
					bFree = bFree && FVector2D::Distance(Candidate, Other) >= MinGapPx;
				}
				Best = Candidate;
				if (bFree)
				{
					break;
				}
			}
			InOutTips[Index] = Best;
			Taken.Add(Best);
		}
	}
}
