#pragma once

#include "CoreMinimal.h"
#include "Tactics/CoverTypes.h"

class UPrimitiveComponent;
class UWorld;
struct FCoverTraceConfig;

/**
 * Wall-following movement round a cover object (game designer 2026-10-20: "a system where the character can move along the wall
 * of any building / object without leaving it"; UE-only, no Godot reference). In cover at an object, a click on a point along the
 * SAME object's outline - its other faces, or the ground within MaxSurfaceDistanceCm of it on the outer side - sends him there
 * hugging the object: a shimmy along each face, round each corner (convex: a quarter arc at the slot distance, the body turning with
 * it; concave: the inner mitre point), ending in cover at the clicked point (the corner slot when it lies near a face's end). The
 * path is the object's footprint outline offset by the cover slot distance, the shorter way round.
 *
 * The footprint is a simple polygon in the XY plane (a placed box's oriented rectangle in the world: CoverWrapWorld::FootprintOf);
 * the pure rules below take any simple polygon (concave ones too) and are tested in CodexTactics.Tactics.CoverWrap.*.
 */
struct CODEXTACTICS_API FCoverWrapConfig
{
	/** Off: clicks round his cover object are the ordinary orders (a move leaves the cover). */
	bool bEnabled = true;
	/** A ground click counts when it lies at most this far outside the object's outline, cm ("within ~1 m of the surface"). */
	float MaxSurfaceDistanceCm = 100.f;
	/** The path runs this far off the faces (FCoverTraceConfig::SlotOffsetCm), cm. */
	float OffsetCm = 45.f;
	/** Less travel than this along the outline is no order (a click right next to him leaves the cover as before), cm. */
	float MinTravelCm = 60.f;
	/** More travel than this is no wrap (a far side of a huge building: the ordinary orders), cm. */
	float MaxTravelCm = 3000.f;
	/** Intermediate points on a convex corner's quarter arc. */
	int32 ArcPoints = 2;
	/** A goal this close to a convex end of its face becomes that corner's slot (facing it, ready to peek round it), cm. */
	float CornerSnapCm = 100.f;
};

/** A simple polygon in the XY plane, counter-clockwise (positive signed area) once MakeCounterClockwise ran. */
struct CODEXTACTICS_API FCoverFootprint
{
	TArray<FVector2D> Vertices;

	bool IsValid() const { return Vertices.Num() >= 3; }
	int32 Num() const { return Vertices.Num(); }
};

/** A point on the footprint's outline: edge Edge (from vertex Edge to Edge + 1) at T (0..1). */
struct CODEXTACTICS_API FCoverOutlinePoint
{
	int32 Edge = INDEX_NONE;
	float T = 0.f;
	FVector2D Point = FVector2D::ZeroVector;
	/** Distance of the query point from the outline: positive outside the polygon, negative inside, cm. */
	float SignedDistanceCm = 0.f;

	bool IsValid() const { return Edge != INDEX_NONE; }
};

/** One face walked along: its edge, the offset line's start / end on the path, the face's outward normal. */
struct CODEXTACTICS_API FCoverWrapLeg
{
	int32 Edge = INDEX_NONE;
	FVector2D Start = FVector2D::ZeroVector;
	FVector2D End = FVector2D::ZeroVector;
	FVector2D Normal = FVector2D::ZeroVector;
};

/** The perimeter path (BuildPath / PlanPath). */
struct CODEXTACTICS_API FCoverWrapPath
{
	/** The polyline from his place to the goal, the faces' offset lines joined by the corners (arc points / mitre points). */
	TArray<FVector2D> Points;
	/** Per point: the face (edge index) he is in cover on while walking from that point to the next (= Legs' edges). */
	TArray<int32> PointEdge;
	TArray<FCoverWrapLeg> Legs;
	/** +1: along the outline's vertex order (= his right along every face, +RightTangent), -1 against it. */
	int32 Direction = 0;
	int32 ConvexCorners = 0;
	int32 ConcaveCorners = 0;
	float LengthCm = 0.f;

	bool IsValid() const { return Points.Num() >= 2 && Points.Num() == PointEdge.Num() && Legs.Num() > 0; }
	int32 CornerCount() const { return ConvexCorners + ConcaveCorners; }
};

/** Why a click round his cover object is (not) a wrap. */
enum class ECoverWrapVerdict : uint8
{
	Wrap,
	Disabled,
	NotInCover,
	/** No footprint of his cover object (not a box-like component, tilted, huge, or not consistent with his slot). */
	NoOutline,
	/** The click lies further than MaxSurfaceDistanceCm outside the outline. */
	TooFar,
	/** Less than MinTravelCm along the outline. */
	TooShort,
	TooLong,
	/** The path is blocked, leaves the navmesh, or a face along it is no cover of the same object. */
	Blocked
};

/**
 * The world plan of a wrap: the path in 3D (on the ground), the cover slot of every face along it, the slot he ends in and the side
 * he faces there. Built by CoverWrapWorld::PlanWrap, executed by AOperativeCharacter::OrderCoverWrap.
 */
struct CODEXTACTICS_API FCoverWrapPlan
{
	FCoverWrapPath Path;
	/** Path.Points on the ground (navmesh height), same count. */
	TArray<FVector> Waypoints;
	/** Per waypoint: the index into Slots of the face he holds from that waypoint on. */
	TArray<int32> WaypointSlot;
	/** One traced cover slot per leg (face), in walking order; the last one is the goal's face. */
	TArray<FCoverSlot> Slots;
	FCoverSlot GoalSlot;
	ECoverFacing GoalFacing = ECoverFacing::Right;
	/** The goal was moved to a face end's corner slot (CornerSnapCm). */
	bool bGoalAtCorner = false;

	bool IsValid() const { return Path.IsValid() && Waypoints.Num() == Path.Points.Num() && WaypointSlot.Num() == Waypoints.Num() && Slots.Num() > 0 && GoalSlot.IsValid(); }
};

namespace CoverWrapRules
{
	/** Signed area (positive = counter-clockwise), cm^2. */
	CODEXTACTICS_API float SignedArea(const FCoverFootprint& Footprint);

	/** Reverses the vertex order of a clockwise polygon. */
	CODEXTACTICS_API void MakeCounterClockwise(FCoverFootprint& Footprint);

	/** A rectangle round Centre with its local X along AxisX (2D, any length), half sizes HalfX / HalfY; counter-clockwise. */
	CODEXTACTICS_API FCoverFootprint MakeBoxFootprint(const FVector2D& Centre, const FVector2D& AxisX, float HalfX, float HalfY);

	/** Outward unit normal of edge Edge of a counter-clockwise footprint. */
	CODEXTACTICS_API FVector2D EdgeNormal(const FCoverFootprint& Footprint, int32 Edge);

	/** The vertex is a convex (outer) corner of a counter-clockwise footprint; false = concave (inner). */
	CODEXTACTICS_API bool IsConvexVertex(const FCoverFootprint& Footprint, int32 Vertex);

	/** Point lies inside the polygon (even-odd). */
	CODEXTACTICS_API bool IsInside(const FCoverFootprint& Footprint, const FVector2D& Point);

	/** The outline point nearest to Point (any edge), with Point's signed distance (positive outside). */
	CODEXTACTICS_API FCoverOutlinePoint NearestOutlinePoint(const FCoverFootprint& Footprint, const FVector2D& Point);

	/** Point projected onto edge Edge only (clamped to it); SignedDistanceCm = its distance along the edge's outward normal. */
	CODEXTACTICS_API FCoverOutlinePoint ProjectOntoEdge(const FCoverFootprint& Footprint, int32 Edge, const FVector2D& Point);

	/**
	 * The edge a slot stands at: the one whose outward normal agrees best with WallNormal (>= MinDot) among those whose line lies within
	 * MaxDistanceCm of WallPoint, the nearest of those. INDEX_NONE when none.
	 */
	CODEXTACTICS_API int32 FindEdgeForWall(const FCoverFootprint& Footprint, const FVector2D& WallPoint, const FVector2D& WallNormal,
		float MaxDistanceCm = 30.f, float MinDot = 0.9f);

	/**
	 * The perimeter path from From to To round the footprint in Direction (+1 along the vertex order, -1 against), OffsetCm off the
	 * faces: from From's offset point along its face, at each corner passed a quarter arc (convex: ArcPoints intermediate points at the
	 * offset distance round the vertex) or the mitre point of the two offset lines (concave), to To's offset point. A From and To on the
	 * same edge with To behind From in Direction go all the way round.
	 */
	CODEXTACTICS_API FCoverWrapPath BuildPath(const FCoverFootprint& Footprint, const FCoverOutlinePoint& From, const FCoverOutlinePoint& To, int32 Direction,
		float OffsetCm, int32 ArcPoints = 2);

	/** The shorter of the two ways round (BuildPath both directions; a tie keeps +1). */
	CODEXTACTICS_API FCoverWrapPath BuildShorterPath(const FCoverFootprint& Footprint, const FCoverOutlinePoint& From, const FCoverOutlinePoint& To,
		float OffsetCm, int32 ArcPoints = 2);

	/**
	 * The goal near a face end: when To lies within CornerSnapCm of a CONVEX end of its edge, it moves to that corner's stand point
	 * (StandOffCm(facing) short of the vertex along the edge; never past the edge's middle) and OutFacing faces that end; else To stays
	 * and OutFacing is the side he walks to (Direction > 0 -> Right). True when snapped.
	 */
	CODEXTACTICS_API bool SnapGoalToCorner(const FCoverFootprint& Footprint, FCoverOutlinePoint& InOutTo, int32 Direction, float CornerSnapCm,
		TFunctionRef<float(ECoverFacing)> StandOffCm, ECoverFacing& OutFacing);

	/**
	 * The whole decision: From = his place (projected onto FromEdge when given, else the nearest edge), To = the click. Verdict Wrap with
	 * the shorter path (the goal snapped to a corner by SnapGoalToCorner, OutGoalFacing) or why not: TooFar (To further than
	 * MaxSurfaceDistanceCm outside the outline), TooShort / TooLong (travel along the outline). Disabled / NoOutline as named.
	 */
	CODEXTACTICS_API ECoverWrapVerdict PlanPath(const FCoverFootprint& Footprint, const FVector2D& From, int32 FromEdge, const FVector2D& To,
		const FCoverWrapConfig& Config, TFunctionRef<float(ECoverFacing)> StandOffCm, FCoverWrapPath& OutPath, ECoverFacing& OutGoalFacing,
		bool* bOutGoalAtCorner = nullptr);

	/** Log / smoke name of a verdict. */
	CODEXTACTICS_API const TCHAR* VerdictName(ECoverWrapVerdict Verdict);

	/** The 2D right-hand tangent of a wall with this outward normal (FCoverSlot::RightTangent in 2D). */
	inline FVector2D RightTangent2D(const FVector2D& Normal) { return FVector2D(-Normal.Y, Normal.X); }

	/** Point along the polyline at arc length Distance from its start (clamped). */
	CODEXTACTICS_API FVector2D PointAtDistance(const TArray<FVector2D>& Points, float Distance);

	/**
	 * Arc length along the polyline of Point's projection, searched on the Segments segments from FirstSegment on (segment i runs from
	 * point i to i + 1; the nearest projection wins), cm.
	 */
	CODEXTACTICS_API float ProgressAlongPolyline(const TArray<FVector2D>& Points, const FVector2D& Point, int32 FirstSegment = 0, int32 Segments = TNumericLimits<int32>::Max());

	/** Distance of Point from the polyline (2D), cm. */
	CODEXTACTICS_API float DistanceToPolyline(const TArray<FVector2D>& Points, const FVector2D& Point);
}

namespace CoverWrapWorld
{
	/**
	 * The footprint of a placed box-like component (its local bounds' rectangle in the world): false when there is none, the component
	 * is tilted (its up axis more than ~14 deg off the vertical), an instanced mesh, or bigger than MaxHalfExtentCm on a side.
	 */
	CODEXTACTICS_API bool FootprintOf(const UPrimitiveComponent* Component, FCoverFootprint& OutFootprint, float MaxHalfExtentCm = 4000.f);

	/**
	 * The footprint of the object a cover slot is at (Slot.WallComponent), checked against the slot: its wall point must lie on an edge
	 * (within 30 cm) whose normal agrees with the slot's (FindEdgeForWall). OutEdge = that edge. False otherwise.
	 */
	CODEXTACTICS_API bool FootprintForSlot(const FCoverSlot& Slot, FCoverFootprint& OutFootprint, int32& OutEdge);

	/** The two slots stand at the same object (the same component; without components the same actor). */
	CODEXTACTICS_API bool IsSameObject(const FCoverSlot& A, const FCoverSlot& B);

	/**
	 * The world plan (CoverWrapRules::PlanPath on the footprint of Current's object, then traced): the waypoints on the navmesh (each
	 * within 35 cm of its path point), a clear path (a knee-high sphere sweep along every segment, bodies ignored), a cover slot of the
	 * same object traced on every face walked along and at the goal. Verdict Wrap with OutPlan filled, or why not.
	 */
	CODEXTACTICS_API ECoverWrapVerdict PlanWrap(UWorld* World, const FCoverSlot& Current, const FVector& From, const FVector& Click,
		const FCoverWrapConfig& Config, TFunctionRef<float(ECoverFacing)> StandOffCm, FCoverWrapPlan& OutPlan,
		const TArray<const AActor*>& IgnoredActors = TArray<const AActor*>());
}
