// Wall-following movement round a cover object (game designer 2026-10-20; UE-only, no Godot reference). See Tactics/CoverWrapRules.h.

#include "Tactics/CoverWrapRules.h"

#include "Algo/Reverse.h"
#include "Combat/EnemyGhostActor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"
#include "Tactics/CoverTraceRules.h"

namespace CoverWrapRules
{
	namespace
	{
		const FVector2D& WrapVertex(const FCoverFootprint& Footprint, int32 Index)
		{
			const int32 Count = Footprint.Num();
			return Footprint.Vertices[((Index % Count) + Count) % Count];
		}

		FVector2D WrapEdgePoint(const FCoverFootprint& Footprint, int32 Edge, float T)
		{
			const FVector2D& A = WrapVertex(Footprint, Edge);
			const FVector2D& B = WrapVertex(Footprint, Edge + 1);
			return A + (B - A) * T;
		}

		/** Appends a path point; a point on top of the last one (< 1 cm) replaces the face of the last instead. */
		void WrapAddPoint(FCoverWrapPath& Path, const FVector2D& Point, int32 Edge)
		{
			if (Path.Points.Num() > 0 && FVector2D::Distance(Path.Points.Last(), Point) < 1.f)
			{
				Path.PointEdge.Last() = Edge;
				return;
			}
			Path.Points.Add(Point);
			Path.PointEdge.Add(Edge);
		}

		float WrapPolylineLength(const TArray<FVector2D>& Points)
		{
			float Length = 0.f;
			for (int32 Index = 1; Index < Points.Num(); ++Index)
			{
				Length += static_cast<float>(FVector2D::Distance(Points[Index - 1], Points[Index]));
			}
			return Length;
		}
	}

	float SignedArea(const FCoverFootprint& Footprint)
	{
		double Sum = 0.0;
		for (int32 Index = 0; Index < Footprint.Num(); ++Index)
		{
			const FVector2D& A = Footprint.Vertices[Index];
			const FVector2D& B = WrapVertex(Footprint, Index + 1);
			Sum += A.X * B.Y - B.X * A.Y;
		}
		return static_cast<float>(0.5 * Sum);
	}

	void MakeCounterClockwise(FCoverFootprint& Footprint)
	{
		if (Footprint.IsValid() && SignedArea(Footprint) < 0.f)
		{
			Algo::Reverse(Footprint.Vertices);
		}
	}

	FCoverFootprint MakeBoxFootprint(const FVector2D& Centre, const FVector2D& AxisX, float HalfX, float HalfY)
	{
		const FVector2D X = AxisX.GetSafeNormal().IsNearlyZero() ? FVector2D(1.f, 0.f) : AxisX.GetSafeNormal();
		const FVector2D Y(-X.Y, X.X);
		FCoverFootprint Footprint;
		Footprint.Vertices = { Centre - X * HalfX - Y * HalfY, Centre + X * HalfX - Y * HalfY, Centre + X * HalfX + Y * HalfY, Centre - X * HalfX + Y * HalfY };
		MakeCounterClockwise(Footprint);
		return Footprint;
	}

	FVector2D EdgeNormal(const FCoverFootprint& Footprint, int32 Edge)
	{
		const FVector2D Delta = WrapVertex(Footprint, Edge + 1) - WrapVertex(Footprint, Edge);
		return FVector2D(Delta.Y, -Delta.X).GetSafeNormal();
	}

	bool IsConvexVertex(const FCoverFootprint& Footprint, int32 Vertex)
	{
		const FVector2D In = WrapVertex(Footprint, Vertex) - WrapVertex(Footprint, Vertex - 1);
		const FVector2D Out = WrapVertex(Footprint, Vertex + 1) - WrapVertex(Footprint, Vertex);
		return In.X * Out.Y - In.Y * Out.X > 0.f;
	}

	bool IsInside(const FCoverFootprint& Footprint, const FVector2D& Point)
	{
		bool bInside = false;
		for (int32 Index = 0, Prev = Footprint.Num() - 1; Index < Footprint.Num(); Prev = Index++)
		{
			const FVector2D& A = Footprint.Vertices[Index];
			const FVector2D& B = Footprint.Vertices[Prev];
			if ((A.Y > Point.Y) != (B.Y > Point.Y) && Point.X < (B.X - A.X) * (Point.Y - A.Y) / (B.Y - A.Y) + A.X)
			{
				bInside = !bInside;
			}
		}
		return bInside;
	}

	FCoverOutlinePoint ProjectOntoEdge(const FCoverFootprint& Footprint, int32 Edge, const FVector2D& Point)
	{
		FCoverOutlinePoint Out;
		if (!Footprint.IsValid() || Edge < 0 || Edge >= Footprint.Num())
		{
			return Out;
		}
		const FVector2D& A = WrapVertex(Footprint, Edge);
		const FVector2D Delta = WrapVertex(Footprint, Edge + 1) - A;
		const double Length2 = FMath::Max(Delta.SizeSquared(), UE_KINDA_SMALL_NUMBER);
		Out.Edge = Edge;
		Out.T = FMath::Clamp(static_cast<float>(FVector2D::DotProduct(Point - A, Delta) / Length2), 0.f, 1.f);
		Out.Point = A + Delta * Out.T;
		Out.SignedDistanceCm = static_cast<float>(FVector2D::DotProduct(Point - A, EdgeNormal(Footprint, Edge)));
		return Out;
	}

	FCoverOutlinePoint NearestOutlinePoint(const FCoverFootprint& Footprint, const FVector2D& Point)
	{
		FCoverOutlinePoint Best;
		double BestDistance = TNumericLimits<double>::Max();
		for (int32 Edge = 0; Edge < Footprint.Num(); ++Edge)
		{
			const FCoverOutlinePoint Candidate = ProjectOntoEdge(Footprint, Edge, Point);
			const double Distance = FVector2D::Distance(Candidate.Point, Point);
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = Candidate;
			}
		}
		if (Best.IsValid())
		{
			Best.SignedDistanceCm = static_cast<float>(IsInside(Footprint, Point) ? -BestDistance : BestDistance);
		}
		return Best;
	}

	int32 FindEdgeForWall(const FCoverFootprint& Footprint, const FVector2D& WallPoint, const FVector2D& WallNormal, float MaxDistanceCm, float MinDot)
	{
		int32 Best = INDEX_NONE;
		float BestDistance = TNumericLimits<float>::Max();
		const FVector2D Normal = WallNormal.GetSafeNormal();
		for (int32 Edge = 0; Edge < Footprint.Num(); ++Edge)
		{
			if (FVector2D::DotProduct(EdgeNormal(Footprint, Edge), Normal) < MinDot)
			{
				continue;
			}
			const FVector2D& A = WrapVertex(Footprint, Edge);
			const FVector2D Delta = WrapVertex(Footprint, Edge + 1) - A;
			const float Length = static_cast<float>(Delta.Size());
			const float Along = Length > 1.f ? static_cast<float>(FVector2D::DotProduct(WallPoint - A, Delta / Length)) : 0.f;
			const float Line = FMath::Abs(static_cast<float>(FVector2D::DotProduct(WallPoint - A, EdgeNormal(Footprint, Edge))));
			if (Along < -MaxDistanceCm || Along > Length + MaxDistanceCm || Line > MaxDistanceCm)
			{
				continue;
			}
			if (Line < BestDistance)
			{
				BestDistance = Line;
				Best = Edge;
			}
		}
		return Best;
	}

	FCoverWrapPath BuildPath(const FCoverFootprint& Footprint, const FCoverOutlinePoint& From, const FCoverOutlinePoint& To, int32 Direction, float OffsetCm,
		int32 ArcPoints)
	{
		FCoverWrapPath Path;
		const int32 Count = Footprint.Num();
		if (!Footprint.IsValid() || !From.IsValid() || !To.IsValid() || Direction == 0)
		{
			return Path;
		}
		Path.Direction = Direction > 0 ? 1 : -1;
		// The faces walked along, in order. From and To on one edge with To ahead (in Direction): that edge alone; else round.
		TArray<int32> Edges{ From.Edge };
		const bool bSameEdgeAhead = From.Edge == To.Edge && (Path.Direction > 0 ? To.T >= From.T : To.T <= From.T);
		if (!bSameEdgeAhead)
		{
			int32 Edge = From.Edge;
			do
			{
				Edge = (Edge + Path.Direction + Count) % Count;
				Edges.Add(Edge);
			}
			while (Edge != To.Edge && Edges.Num() <= Count + 1);
		}
		WrapAddPoint(Path, WrapEdgePoint(Footprint, From.Edge, From.T) + EdgeNormal(Footprint, From.Edge) * OffsetCm, From.Edge);
		FVector2D LegStart = Path.Points[0];
		for (int32 Index = 0; Index + 1 < Edges.Num(); ++Index)
		{
			const int32 Edge = Edges[Index];
			const int32 NextEdge = Edges[Index + 1];
			const int32 Vertex = Path.Direction > 0 ? (Edge + 1) % Count : Edge; // the vertex the two faces share
			const FVector2D Corner = WrapVertex(Footprint, Vertex);
			const FVector2D N1 = EdgeNormal(Footprint, Edge);
			const FVector2D N2 = EdgeNormal(Footprint, NextEdge);
			if (IsConvexVertex(Footprint, Vertex))
			{
				// Round the outer corner on a quarter arc at the slot distance; he holds the next face from the arc's start (the body turns with it).
				const FVector2D ArcStart = Corner + N1 * OffsetCm;
				Path.Legs.Add({ Edge, LegStart, ArcStart, N1 });
				WrapAddPoint(Path, ArcStart, NextEdge);
				const float From1 = FMath::Atan2(N1.Y, N1.X);
				const float Turn = FMath::FindDeltaAngleRadians(From1, FMath::Atan2(N2.Y, N2.X));
				for (int32 Step = 1; Step <= ArcPoints; ++Step)
				{
					const float Angle = From1 + Turn * Step / (ArcPoints + 1);
					WrapAddPoint(Path, Corner + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * OffsetCm, NextEdge);
				}
				LegStart = Corner + N2 * OffsetCm;
				WrapAddPoint(Path, LegStart, NextEdge);
				++Path.ConvexCorners;
			}
			else
			{
				// Into the inner corner: the two offset lines meet at the mitre point.
				const float Dot = static_cast<float>(FVector2D::DotProduct(N1, N2));
				const FVector2D Mitre = Corner + (N1 + N2) * (OffsetCm / FMath::Max(1.f + Dot, 0.05f));
				Path.Legs.Add({ Edge, LegStart, Mitre, N1 });
				WrapAddPoint(Path, Mitre, NextEdge);
				LegStart = Mitre;
				++Path.ConcaveCorners;
			}
		}
		const FVector2D GoalNormal = EdgeNormal(Footprint, To.Edge);
		const FVector2D Goal = WrapEdgePoint(Footprint, To.Edge, To.T) + GoalNormal * OffsetCm;
		WrapAddPoint(Path, Goal, To.Edge);
		Path.Legs.Add({ To.Edge, LegStart, Goal, GoalNormal });
		Path.LengthCm = WrapPolylineLength(Path.Points);
		return Path;
	}

	FCoverWrapPath BuildShorterPath(const FCoverFootprint& Footprint, const FCoverOutlinePoint& From, const FCoverOutlinePoint& To, float OffsetCm,
		int32 ArcPoints)
	{
		FCoverWrapPath Forward = BuildPath(Footprint, From, To, 1, OffsetCm, ArcPoints);
		FCoverWrapPath Backward = BuildPath(Footprint, From, To, -1, OffsetCm, ArcPoints);
		if (!Backward.IsValid() || (Forward.IsValid() && Forward.LengthCm <= Backward.LengthCm + 0.5f))
		{
			return Forward;
		}
		return Backward;
	}

	bool SnapGoalToCorner(const FCoverFootprint& Footprint, FCoverOutlinePoint& InOutTo, int32 Direction, float CornerSnapCm,
		TFunctionRef<float(ECoverFacing)> StandOffCm, ECoverFacing& OutFacing)
	{
		OutFacing = Direction > 0 ? ECoverFacing::Right : ECoverFacing::Left;
		if (!Footprint.IsValid() || !InOutTo.IsValid())
		{
			return false;
		}
		const int32 Edge = InOutTo.Edge;
		const FVector2D& A = WrapVertex(Footprint, Edge);
		const FVector2D Delta = WrapVertex(Footprint, Edge + 1) - A;
		const float Length = static_cast<float>(Delta.Size());
		if (Length < 1.f)
		{
			return false;
		}
		// Along a counter-clockwise edge his right (+RightTangent) is the edge's own direction: vertex Edge + 1 lies to his right.
		const float Along = InOutTo.T * Length;
		const float ToLeftEnd = Along;
		const float ToRightEnd = Length - Along;
		const bool bLeftOk = IsConvexVertex(Footprint, Edge) && ToLeftEnd <= CornerSnapCm;
		const bool bRightOk = IsConvexVertex(Footprint, Edge + 1) && ToRightEnd <= CornerSnapCm;
		if (!bLeftOk && !bRightOk)
		{
			return false;
		}
		const ECoverFacing Facing = bRightOk && (!bLeftOk || ToRightEnd <= ToLeftEnd) ? ECoverFacing::Right : ECoverFacing::Left;
		const float StandOff = FMath::Min(FMath::Max(StandOffCm(Facing), 0.f), Length * 0.5f);
		const float NewAlong = Facing == ECoverFacing::Right ? Length - StandOff : StandOff;
		InOutTo.T = NewAlong / Length;
		InOutTo.Point = A + Delta * InOutTo.T;
		OutFacing = Facing;
		return true;
	}

	ECoverWrapVerdict PlanPath(const FCoverFootprint& Footprint, const FVector2D& From, int32 FromEdge, const FVector2D& To, const FCoverWrapConfig& Config,
		TFunctionRef<float(ECoverFacing)> StandOffCm, FCoverWrapPath& OutPath, ECoverFacing& OutGoalFacing, bool* bOutGoalAtCorner)
	{
		OutPath = FCoverWrapPath();
		if (bOutGoalAtCorner)
		{
			*bOutGoalAtCorner = false;
		}
		if (!Config.bEnabled)
		{
			return ECoverWrapVerdict::Disabled;
		}
		if (!Footprint.IsValid())
		{
			return ECoverWrapVerdict::NoOutline;
		}
		const FCoverOutlinePoint Start = FromEdge >= 0 && FromEdge < Footprint.Num() ? ProjectOntoEdge(Footprint, FromEdge, From) : NearestOutlinePoint(Footprint, From);
		FCoverOutlinePoint Goal = NearestOutlinePoint(Footprint, To);
		if (!Start.IsValid() || !Goal.IsValid())
		{
			return ECoverWrapVerdict::NoOutline;
		}
		if (Goal.SignedDistanceCm > Config.MaxSurfaceDistanceCm)
		{
			return ECoverWrapVerdict::TooFar;
		}
		// The way round decides the facing of a goal in the open middle of a face; a goal near a face end takes that corner.
		const FCoverWrapPath Rough = BuildShorterPath(Footprint, Start, Goal, Config.OffsetCm, Config.ArcPoints);
		const bool bCorner = SnapGoalToCorner(Footprint, Goal, Rough.Direction, Config.CornerSnapCm, StandOffCm, OutGoalFacing);
		OutPath = bCorner ? BuildShorterPath(Footprint, Start, Goal, Config.OffsetCm, Config.ArcPoints) : Rough;
		if (bOutGoalAtCorner)
		{
			*bOutGoalAtCorner = bCorner;
		}
		if (!OutPath.IsValid())
		{
			return OutPath.Points.Num() == 1 ? ECoverWrapVerdict::TooShort : ECoverWrapVerdict::NoOutline; // one point: the goal is where he stands
		}
		if (OutPath.LengthCm < Config.MinTravelCm)
		{
			return ECoverWrapVerdict::TooShort;
		}
		if (OutPath.LengthCm > Config.MaxTravelCm)
		{
			return ECoverWrapVerdict::TooLong;
		}
		return ECoverWrapVerdict::Wrap;
	}

	const TCHAR* VerdictName(ECoverWrapVerdict Verdict)
	{
		switch (Verdict)
		{
		case ECoverWrapVerdict::Wrap: return TEXT("wrap");
		case ECoverWrapVerdict::Disabled: return TEXT("disabled");
		case ECoverWrapVerdict::NotInCover: return TEXT("not in cover");
		case ECoverWrapVerdict::NoOutline: return TEXT("no outline of his cover object");
		case ECoverWrapVerdict::TooFar: return TEXT("too far from the object");
		case ECoverWrapVerdict::TooShort: return TEXT("too short");
		case ECoverWrapVerdict::TooLong: return TEXT("too long");
		case ECoverWrapVerdict::Blocked: return TEXT("blocked");
		default: return TEXT("?");
		}
	}

	FVector2D PointAtDistance(const TArray<FVector2D>& Points, float Distance)
	{
		if (Points.Num() == 0)
		{
			return FVector2D::ZeroVector;
		}
		float Left = FMath::Max(Distance, 0.f);
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			const float Segment = static_cast<float>(FVector2D::Distance(Points[Index - 1], Points[Index]));
			if (Left <= Segment && Segment > 0.f)
			{
				return FMath::Lerp(Points[Index - 1], Points[Index], Left / Segment);
			}
			Left -= Segment;
		}
		return Points.Last();
	}

	float ProgressAlongPolyline(const TArray<FVector2D>& Points, const FVector2D& Point, int32 FirstSegment, int32 Segments)
	{
		float Start = 0.f;
		for (int32 Index = 1; Index <= FirstSegment && Index < Points.Num(); ++Index)
		{
			Start += static_cast<float>(FVector2D::Distance(Points[Index - 1], Points[Index]));
		}
		float Best = Start;
		double BestDistance = TNumericLimits<double>::Max();
		float Along = Start;
		const int32 First = FMath::Max(FirstSegment, 0);
		const int32 End = Segments >= Points.Num() ? Points.Num() - 1 : FMath::Min(First + FMath::Max(Segments, 1), Points.Num() - 1);
		for (int32 Segment = First; Segment < End; ++Segment)
		{
			const FVector2D A = Points[Segment];
			const FVector2D Delta = Points[Segment + 1] - A;
			const double Length2 = Delta.SizeSquared();
			const double T = Length2 > UE_KINDA_SMALL_NUMBER ? FMath::Clamp(FVector2D::DotProduct(Point - A, Delta) / Length2, 0.0, 1.0) : 0.0;
			const double Distance = FVector2D::Distance(A + Delta * T, Point);
			const float Length = static_cast<float>(FMath::Sqrt(Length2));
			if (Distance < BestDistance - 0.01)
			{
				BestDistance = Distance;
				Best = Along + Length * static_cast<float>(T);
			}
			Along += Length;
		}
		return Best;
	}

	float DistanceToPolyline(const TArray<FVector2D>& Points, const FVector2D& Point)
	{
		if (Points.Num() == 1)
		{
			return static_cast<float>(FVector2D::Distance(Points[0], Point));
		}
		float Best = TNumericLimits<float>::Max();
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			const FVector2D A = Points[Index - 1];
			const FVector2D Delta = Points[Index] - A;
			const double Length2 = Delta.SizeSquared();
			const double T = Length2 > UE_KINDA_SMALL_NUMBER ? FMath::Clamp(FVector2D::DotProduct(Point - A, Delta) / Length2, 0.0, 1.0) : 0.0;
			Best = FMath::Min(Best, static_cast<float>(FVector2D::Distance(A + Delta * T, Point)));
		}
		return Best;
	}
}

namespace CoverWrapWorld
{
	bool FootprintOf(const UPrimitiveComponent* Component, FCoverFootprint& OutFootprint, float MaxHalfExtentCm)
	{
		OutFootprint = FCoverFootprint();
		if (!Component || Component->IsA<UInstancedStaticMeshComponent>())
		{
			return false;
		}
		const FTransform& ToWorld = Component->GetComponentTransform();
		if (ToWorld.GetUnitAxis(EAxis::Z).Z < 0.97)
		{
			return false; // tilted: no upright outline
		}
		const FBox Local = Component->GetLocalBounds().GetBox();
		if (!Local.IsValid)
		{
			return false;
		}
		const FVector Corners[4] = { FVector(Local.Min.X, Local.Min.Y, 0.f), FVector(Local.Max.X, Local.Min.Y, 0.f),
			FVector(Local.Max.X, Local.Max.Y, 0.f), FVector(Local.Min.X, Local.Max.Y, 0.f) };
		for (const FVector& Corner : Corners)
		{
			const FVector World = ToWorld.TransformPosition(Corner);
			OutFootprint.Vertices.Add(FVector2D(World.X, World.Y));
		}
		const float SideX = static_cast<float>(FVector2D::Distance(OutFootprint.Vertices[0], OutFootprint.Vertices[1]));
		const float SideY = static_cast<float>(FVector2D::Distance(OutFootprint.Vertices[1], OutFootprint.Vertices[2]));
		if (SideX < 5.f || SideY < 5.f || SideX > 2.f * MaxHalfExtentCm || SideY > 2.f * MaxHalfExtentCm)
		{
			OutFootprint = FCoverFootprint();
			return false;
		}
		CoverWrapRules::MakeCounterClockwise(OutFootprint);
		return true;
	}

	bool FootprintForSlot(const FCoverSlot& Slot, FCoverFootprint& OutFootprint, int32& OutEdge)
	{
		OutEdge = INDEX_NONE;
		if (!Slot.IsValid() || !FootprintOf(Slot.WallComponent.Get(), OutFootprint))
		{
			return false;
		}
		OutEdge = CoverWrapRules::FindEdgeForWall(OutFootprint, FVector2D(Slot.WallPoint), FVector2D(Slot.WallNormal));
		return OutEdge != INDEX_NONE;
	}

	bool IsSameObject(const FCoverSlot& A, const FCoverSlot& B)
	{
		if (A.WallComponent.IsValid() && B.WallComponent.IsValid())
		{
			return A.WallComponent == B.WallComponent;
		}
		return A.WallActor.IsValid() && A.WallActor == B.WallActor;
	}

	namespace
	{
		/** The cover slot of the same object traced at a face point (the click on the face, traced from the open side). */
		bool WrapTraceFaceSlot(UWorld* World, const FCoverSlot& Current, const FVector2D& FacePoint, const FVector2D& Normal, float GroundZ,
			const TArray<const AActor*>& Ignored, FCoverSlot& OutSlot)
		{
			const FCoverTraceConfig TraceConfig;
			const FVector Click(FacePoint.X, FacePoint.Y, GroundZ + TraceConfig.ChestHeightCm);
			if (!CoverTraceRules::FindCoverSlotAt(World, Click, FVector(-Normal.X, -Normal.Y, 0.f), OutSlot, TraceConfig, Ignored))
			{
				return false;
			}
			return IsSameObject(OutSlot, Current) && FVector2D::DotProduct(FVector2D(OutSlot.WallNormal).GetSafeNormal(), Normal) >= 0.9f;
		}
	}

	ECoverWrapVerdict PlanWrap(UWorld* World, const FCoverSlot& Current, const FVector& From, const FVector& Click, const FCoverWrapConfig& Config,
		TFunctionRef<float(ECoverFacing)> StandOffCm, FCoverWrapPlan& OutPlan, const TArray<const AActor*>& IgnoredActors)
	{
		OutPlan = FCoverWrapPlan();
		if (!Config.bEnabled)
		{
			return ECoverWrapVerdict::Disabled;
		}
		if (!World || !Current.IsValid())
		{
			return ECoverWrapVerdict::NotInCover;
		}
		FCoverFootprint Footprint;
		int32 FromEdge = INDEX_NONE;
		if (!FootprintForSlot(Current, Footprint, FromEdge))
		{
			return ECoverWrapVerdict::NoOutline;
		}
		bool bCorner = false;
		const ECoverWrapVerdict Verdict = CoverWrapRules::PlanPath(Footprint, FVector2D(From), FromEdge, FVector2D(Click), Config, StandOffCm, OutPlan.Path,
			OutPlan.GoalFacing, &bCorner);
		OutPlan.bGoalAtCorner = bCorner;
		if (Verdict != ECoverWrapVerdict::Wrap)
		{
			return Verdict;
		}
		const FCoverWrapPath& Path = OutPlan.Path;
		const float GroundZ = Current.WorldLocation.Z;
		// The waypoints on the navmesh (each close to its path point: a pulled point means the outline runs where nobody can walk).
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		for (const FVector2D& Point : Path.Points)
		{
			FVector Waypoint(Point.X, Point.Y, GroundZ);
			if (Nav)
			{
				FNavLocation OnNav;
				if (!Nav->ProjectPointToNavigation(Waypoint, OnNav, FVector(60.f, 60.f, 150.f)) || FVector::Dist2D(OnNav.Location, Waypoint) > 45.f)
				{
					return ECoverWrapVerdict::Blocked;
				}
				Waypoint = FVector(Point.X, Point.Y, OnNav.Location.Z);
			}
			OutPlan.Waypoints.Add(Waypoint);
		}
		// A clear way: a knee-high sphere along every segment (bodies ignored - they step aside or block the walk as any shimmy).
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CoverWrapSweep), false);
		for (const AActor* Actor : IgnoredActors)
		{
			Params.AddIgnoredActor(Actor);
		}
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
		for (TActorIterator<AEnemyGhostActor> It(World); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
		const FCollisionShape Sphere = FCollisionShape::MakeSphere(18.f);
		for (int32 Index = 1; Index < OutPlan.Waypoints.Num(); ++Index)
		{
			FHitResult Hit;
			const FVector Lift(0.f, 0.f, 50.f);
			if (World->SweepSingleByChannel(Hit, OutPlan.Waypoints[Index - 1] + Lift, OutPlan.Waypoints[Index] + Lift, FQuat::Identity, ECC_Pawn, Sphere, Params))
			{
				return ECoverWrapVerdict::Blocked;
			}
		}
		// A cover slot on every face walked along (his own first), then the goal's.
		for (int32 Leg = 0; Leg < Path.Legs.Num(); ++Leg)
		{
			const FCoverWrapLeg& Each = Path.Legs[Leg];
			if (Leg == 0)
			{
				OutPlan.Slots.Add(Current);
				continue;
			}
			// A little in from the corner the leg starts at (the edge probes then see that corner), at most its middle.
			const float Length = static_cast<float>(FVector2D::Distance(Each.Start, Each.End));
			const FVector2D Along = Length > 1.f ? (Each.End - Each.Start) / Length : FVector2D::ZeroVector;
			const FVector2D OnPath = Each.Start + Along * FMath::Min(30.f, Length * 0.5f);
			FCoverSlot FaceSlot;
			if (!WrapTraceFaceSlot(World, Current, OnPath - Each.Normal * Config.OffsetCm, Each.Normal, GroundZ, IgnoredActors, FaceSlot))
			{
				return ECoverWrapVerdict::Blocked;
			}
			FaceSlot.WorldLocation = FVector(OnPath.X, OnPath.Y, FaceSlot.WorldLocation.Z); // on the line he walks (the navmesh keeps further off)
			OutPlan.Slots.Add(FaceSlot);
		}
		const FCoverWrapLeg& Last = Path.Legs.Last();
		if (!WrapTraceFaceSlot(World, Current, Path.Points.Last() - Last.Normal * Config.OffsetCm, Last.Normal, GroundZ, IgnoredActors, OutPlan.GoalSlot))
		{
			return ECoverWrapVerdict::Blocked;
		}
		OutPlan.GoalSlot.WorldLocation = FVector(Path.Points.Last().X, Path.Points.Last().Y, OutPlan.GoalSlot.WorldLocation.Z);
		// The face held from each waypoint on: the slot index moves on whenever the path's face changes.
		// (A start right at a corner already holds the next face: the first point's face is then not the first leg's.)
		int32 SlotIndex = Path.PointEdge[0] != Path.Legs[0].Edge && OutPlan.Slots.Num() > 1 ? 1 : 0;
		for (int32 Index = 0; Index < Path.PointEdge.Num(); ++Index)
		{
			if (Index > 0 && Path.PointEdge[Index] != Path.PointEdge[Index - 1])
			{
				SlotIndex = FMath::Min(SlotIndex + 1, OutPlan.Slots.Num() - 1);
			}
			OutPlan.WaypointSlot.Add(SlotIndex);
		}
		return OutPlan.IsValid() ? ECoverWrapVerdict::Wrap : ECoverWrapVerdict::Blocked;
	}
}
