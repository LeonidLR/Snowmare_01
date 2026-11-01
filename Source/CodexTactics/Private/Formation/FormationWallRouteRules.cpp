// FormationWallRoute: one route round an obstacle for a group move into the wall layout (game designer play-test 2026-11-01, fixes d / e).
// Godot reference: none (UE-only).

#include "Formation/FormationWallRouteRules.h"

#include "Formation/FormationRuntimeRules.h"
#include "Formation/FormationTypes.h"

namespace FormationWallRoute
{
	bool NeedsRoute(const FVector2D& Member, const FVector2D& Slot, const TArray<FFormationWall>& Walls)
	{
		return FormationMath::SegmentBlocked(Member, Slot, Walls, RouteClearanceCm);
	}

	TArray<FPlace> PlacesAlongTrail(const TArray<FVector2D>& TrailWithLeader, int32 Places, float FirstBackCm, float SpacingCm)
	{
		TArray<FPlace> Out;
		if (TrailWithLeader.IsEmpty() || Places <= 0)
		{
			return Out;
		}
		const int32 Last = TrailWithLeader.Num() - 1;
		for (int32 Index = 0; Index < Places; ++Index)
		{
			double Back = FMath::Max(0.0, static_cast<double>(FirstBackCm) + static_cast<double>(Index) * SpacingCm);
			FPlace Place;
			Place.Pos = TrailWithLeader[0];
			// The direction at the start: the first segment that moves.
			for (int32 Seg = 1; Seg <= Last; ++Seg)
			{
				if (FVector2D::Distance(TrailWithLeader[Seg], TrailWithLeader[Seg - 1]) > 1e-3)
				{
					Place.Yaw = FormationMath::YawOf(TrailWithLeader[Seg] - TrailWithLeader[Seg - 1]);
					break;
				}
			}
			for (int32 Seg = Last; Seg >= 1; --Seg)
			{
				const FVector2D From = TrailWithLeader[Seg];
				const FVector2D To = TrailWithLeader[Seg - 1];
				const double Length = FVector2D::Distance(From, To);
				if (Length < 1e-3)
				{
					continue;
				}
				if (Back <= Length)
				{
					Place.Pos = From + (To - From) * (Back / Length);
					Place.Yaw = FormationMath::YawOf(From - To);
					break;
				}
				Back -= Length;
			}
			Out.Add(Place);
		}
		return Out;
	}

	TArray<int32> AssignColumnPlaces(const TArray<FVector2D>& Routed, const TArray<FVector2D>& ColumnPlaces)
	{
		if (Routed.IsEmpty() || Routed.Num() > ColumnPlaces.Num())
		{
			return TArray<int32>();
		}
		TArray<FVector2D> Nearest;
		for (int32 Index = 0; Index < Routed.Num(); ++Index)
		{
			Nearest.Add(ColumnPlaces[Index]);
		}
		return FormationRuntime::AssignByTravel(Routed, Nearest);
	}
}
