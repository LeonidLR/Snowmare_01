#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/**
 * Pure geometry view of one codex.levelgen_layout/1 layout for the props placement (docs/design/levelgen/PROPS_LAYERS.md section 2.1,
 * task C2 of section 5): walk layers + Z, the blocked test, doorways (building doors + compound gates), the mandatory route and the
 * exclusion shapes. No world, no actors, no navmesh. Godot reference: none (UE-only level authoring); the layer / wall conventions
 * mirror the Training Ground's Tools/TrainingGround/src/levelgen/schema.ts (layersOf, layerLevel, layerWalls), which the TS port of
 * the placement (T2) reads too.
 *
 * Determinism: every input and output is integer cm and every decision (blocked, walkable, excluded, "door on the mandatory route") is
 * exact integer math (int64), no floats - the TS port reproduces it bit for bit. Exactness holds for segments up to MaxSegmentCm and
 * radii up to MaxRadiusCm (Build refuses longer walls / route legs and thicker walls; radii are clamped).
 * C4 adds the sight blockers of the turret coverage (GetSightBlockers / IsSightBlocked), the routes / covers / buildings accessors and
 * the integer helpers of PropsGeometry. Tests: CodexTactics.LevelGen.Props.LayoutView.*, CodexTactics.LevelGen.Props.C4.*.
 */

/**
 * Geometry conventions. Defaults are the values of the design / the TS schema / the map builder. The designer-tunable rule numbers (gate
 * widths, the "on the mandatory route" rule) are data: props_balance.json "layout_view", read by FPropsLayoutView::ReadConfig; the
 * defaults below equal that file (a test checks it).
 */
struct FPropsLayoutConfig
{
	/** Gate = a gap between collinear segments of one fence / wall of this width, inclusive (designer 2026-11-10: 80-600 cm). */
	int32 GateMinGapCm = 80;
	int32 GateMaxGapCm = 600;
	/**
	 * "On the mandatory route" (designer 2026-11-10) = the route passes THROUGH the doorway or turns around AT it; passing by is not
	 * enough. Reach of the opening across its wall: half the opening width + this slack (route polylines are approximate nav paths).
	 */
	int32 DoorThroughSlackCm = 60;
	/** "Turns at it": a route vertex where the route reverses or ends, at most this far in front of the doorway (outward side). */
	int32 DoorTurnReachCm = 250;
	/** Opening width assumed for a building door without a matching wall gap (WidthCm 0). */
	int32 DoorFallbackWidthCm = 180;
	/** Walkable roof = footprint minus this inset (2.1: 50 cm). */
	int32 RoofInsetCm = 50;
	/** Half the side of a square roof / floor hole (schema.ts HOLE_HALF_CM). The rim (exclude.hole_rim_cm) is added on top. */
	int32 HoleHalfSideCm = 120;
	/** Half the width of a stair ramp (build_levelgen_map.py STAIR_W 150 / 2); a stair exclusion adds stair_cm to it. */
	int32 StairHalfWidthCm = 75;
	/** cm per height level (schema.ts LEVEL_HEIGHT): floor n sits at ground_z + n x this, a roof at ground_z + floors x this. */
	int32 LevelHeightCm = 300;
	/** Thickness of the synthetic layer walls of schema.ts layerWalls: upper floor perimeter 30, roof parapet 20, cut edges 20. */
	int32 FloorPerimeterThicknessCm = 30;
	int32 ParapetThicknessCm = 20;
	int32 CutEdgeThicknessCm = 20;
	/** A building door takes the width of the building wall gap whose centre is within this distance (cm, per axis). */
	int32 DoorGapMatchCm = 2;
	/** Sight (C4 turrets): a cover at least this tall blocks a standing man's view on the ground (evaluate.ts tallCoverSegs 140). */
	int32 TallCoverMinCm = 140;
	/** Sight: eye above the surface a man stands on (evaluate.ts EYE_CM 160); on a roof only obstacles reaching that height block. */
	int32 EyeHeightCm = 160;
	/** Anchors (C4): "roof edges" = the walkable roof's lattice ring this far inside the footprint edge (2.1: inset 100 cm). */
	int32 RoofEdgeInsetCm = 100;
};

/**
 * Exact integer helpers shared by the layout view and the props placement (C4). The TS port copies them (JS: safe integers / BigInt).
 */
namespace PropsGeometry
{
	/** Scale of the sin / cos table: round(sin(d deg) x 16384). */
	constexpr int64 SinScale = 16384;
	/** Sight segments are in units of 1 / SightScale cm (2 x SinScale, so a rotated cover corner is an exact integer). */
	constexpr int64 SightScale = 32768;

	/** round(sin(Deg) x 16384) from the 0..90 table, any integer Deg. */
	CODEXTACTICS_API int64 SinQ(int32 Deg);
	/** SinQ(Deg + 90). */
	CODEXTACTICS_API int64 CosQ(int32 Deg);
	/** floor(sqrt(V)) exactly, V >= 0 (0 for negatives). */
	CODEXTACTICS_API int64 ISqrt(int64 V);
	/** floor(A / B) towards minus infinity (JS Math.floor(a / b)), B != 0. */
	CODEXTACTICS_API int64 FloorDiv(int64 A, int64 B);
}

enum class EPropsWalkLayerKind : uint8
{
	Ground,
	Cut,
	Floor,
	Roof
};

/** One walk layer (schema.ts layersOf): "ground", "cut", "floor:<B>:<n>" (non-hollow buildings, 1 <= n < floors), "roof:<B>". */
struct FPropsWalkLayer
{
	FString Name;
	EPropsWalkLayerKind Kind = EPropsWalkLayerKind::Ground;
	/** Building id for floors / roofs. */
	FString Building;
	/** Height level: ground 0, cut = cut.level (-1), floor n = n, roof = floors. */
	int32 Level = 0;
	/** Absolute slab Z (cm): ground_z + Level x LevelHeightCm; the cut uses ground_z + cut.floor_z when present. */
	int32 ZCm = 0;
	int32 BuildingIndex = INDEX_NONE;
};

enum class EPropsDoorwayKind : uint8
{
	/** buildings[].doors[i], id "<building>#<i>". */
	BuildingDoor,
	/** A gap of GateMinGapCm..GateMaxGapCm between collinear segments of one fence / wall, id "<wall group>#g<i>". */
	Gate
};

/** A doorway. The cut's gaps (cut_wall) are crossings, never doorways; interior wall gaps are not doorways either. */
struct FPropsDoorway
{
	FString Id;
	EPropsDoorwayKind Kind = EPropsDoorwayKind::BuildingDoor;
	/** Building id (doors only). */
	FString Building;
	/** Wall group of the opening (wall id without the trailing "_<n>", e.g. F_compound_N); empty for a door without a matching gap. */
	FString WallGroup;
	/** Door pos from the layout / gate centre (integer cm, floored). */
	FIntPoint Pos = FIntPoint::ZeroValue;
	/** Unit axis vector out of the building (the nearest footprint side). Zero for gates. */
	FIntPoint Outward = FIntPoint::ZeroValue;
	/** Unit axis vector along the wall the opening is in (a tripwire spans it; "across the opening" is the perpendicular). */
	FIntPoint Along = FIntPoint::ZeroValue;
	/** Opening width from the wall gap; 0 when the door has no matching gap. */
	int32 WidthCm = 0;
	int32 Floor = 0;
	FString WalkLayer = TEXT("ground");
	/** Distance (cm, rounded up) to the nearest mandatory route leg on WalkLayer; MAX_int32 when there is none. */
	int32 MandatoryRouteDistCm = MAX_int32;
	/**
	 * THROUGH: a mandatory leg with both ends on WalkLayer crosses (or touches) the doorway's wall line - the axis line through Pos along
	 * Along - at most H from Pos, H = (WidthCm, or DoorFallbackWidthCm when 0) / 2 (floored) + DoorThroughSlackCm. A leg lying on the
	 * line does not cross it. Exact integer test.
	 */
	bool bRouteThrough = false;
	/**
	 * TURNS AT: a mandatory route vertex on WalkLayer that is the route's first / last vertex or a reversal (dot(V - Prev, Next - V) < 0),
	 * with lateral offset |dot(V - Pos, Along)| <= H and depth in front of the doorway 0 <= dot(V - Pos, Outward) <= DoorTurnReachCm
	 * (gates have no outward side: |depth across the wall| <= DoorTurnReachCm). Exact integer test.
	 */
	bool bRouteTurnsAt = false;
	/** bRouteThrough || bRouteTurnsAt (designer 2026-11-10: passing by within a distance is NOT on the route). */
	bool bOnMandatoryRoute = false;
};

/** A gap between collinear segments of one wall group (any wall kind). */
struct FPropsWallGap
{
	FString Group;
	/** Wall kind: fence | wall | building | interior | cut_wall | ... */
	FString WallKind;
	FString Building;
	/** Gap centre, floored to integer cm. */
	FIntPoint Center = FIntPoint::ZeroValue;
	int32 WidthCm = 0;
	/** The wall runs along Y (a.x == b.x). */
	bool bAlongY = false;
};

struct FPropsZone
{
	FString Id;
	FString Kind;
	FString SetPiece;
	FString Intent;
	TArray<FIntPoint> Poly;
};

struct FPropsAnchorPoint
{
	FString Id;
	FIntPoint Pos = FIntPoint::ZeroValue;
};

/** A route polyline of the layout (routes[]), one walk layer name per point. */
struct FPropsRoute
{
	FString Id;
	bool bMandatory = false;
	TArray<FIntPoint> Points;
	TArray<FString> Layers;
};

/** A layout cover (covers[]): an oriented box size x / y centred on pos, rotated by yaw; Height = size[2] (0 when absent). */
struct FPropsCover
{
	FString Id;
	FIntPoint Pos = FIntPoint::ZeroValue;
	int32 Yaw = 0;
	int32 SizeX = 0;
	int32 SizeY = 0;
	int32 HeightCm = 0;
};

/** What the placement needs to know of a building (buildings[]), in layout order. */
struct FPropsBuildingSummary
{
	FString Id;
	FIntPoint Min = FIntPoint::ZeroValue;
	FIntPoint Max = FIntPoint::ZeroValue;
	int32 Floors = 1;
	bool bHollow = false;
	bool bEnterable = false;
	bool bRoofWalkable = false;
};

/** A sight blocker in units of 1 / PropsGeometry::SightScale cm (exact; thickness ignored). */
struct FPropsSightSegment
{
	int64 AX = 0;
	int64 AY = 0;
	int64 BX = 0;
	int64 BY = 0;
};

/** Radii of a rules.exclude block (cm); 0 / false = that shape is not produced. Mapping from the rules JSON is the placement's job (C3). */
struct FPropsExclusionParams
{
	int32 SpawnCm = 0;
	int32 EndCm = 0;
	int32 LadderCm = 0;
	int32 StairCm = 0;
	int32 DoorCm = 0;
	int32 RouteMandatoryCm = 0;
	int32 RouteVariantsCm = 0;
	int32 HoleRimCm = 0;
	bool bCrossing = false;
};

enum class EPropsExclusionKind : uint8
{
	Spawn,
	End,
	LadderFoot,
	LadderTop,
	Stair,
	Door,
	Route,
	Crossing,
	HoleRim
};

/** A keep-out shape: a disk / capsule / corridor (polyline + radius) or an area (closed polygon, + radius round its edges). */
struct CODEXTACTICS_API FPropsExclusion
{
	EPropsExclusionKind Kind = EPropsExclusionKind::Spawn;
	FString SourceId;
	/** Empty = every walk layer. */
	FString WalkLayer;
	TArray<FIntPoint> Points;
	int32 RadiusCm = 0;
	bool bArea = false;

	/** Exact integer test: P lies in the shape (boundary included) on the given walk layer. */
	bool Contains(FIntPoint P, const FString& Layer) const;
};

/** Built once from a parsed layout; immutable afterwards. */
class CODEXTACTICS_API FPropsLayoutView
{
public:
	/** Longest wall / route leg the exact int64 geometry supports (300 m); longer ones are refused by Build. */
	static constexpr int32 MaxSegmentCm = 30000;
	/** Largest radius / clearance (100 m); larger values are clamped. */
	static constexpr int32 MaxRadiusCm = 10000;
	/** Thickest wall Build accepts (20 m; generated walls are <= 1.5 m). */
	static constexpr int32 MaxWallThicknessCm = 2000;

	/**
	 * Parses and builds. Returns false + Errors on invalid JSON or a foreign schema (empty view), or when any entry is bad: a bad entry
	 * (missing required field, wrong type) is reported and skipped, the rest is kept. Fields are read strictly through TryGetField
	 * (LevelGenJsonRead.h). Never crashes. Errors are also logged (LogCodexTactics, Warning).
	 */
	static bool Build(const FString& Json, FPropsLayoutView& Out, TArray<FString>& Errors, const FPropsLayoutConfig& Config = FPropsLayoutConfig());
	static bool Build(const FJsonObject& Root, FPropsLayoutView& Out, TArray<FString>& Errors, const FPropsLayoutConfig& Config = FPropsLayoutConfig());

	/**
	 * Reads props_balance.json "layout_view" (gate_min_gap_cm, gate_max_gap_cm, door_through_slack_cm, door_turn_reach_cm,
	 * door_fallback_width_cm, tall_cover_min_cm, eye_height_cm, roof_edge_inset_cm) over the defaults. A missing block / key keeps the default; a key of the wrong type or a negative value is
	 * an error (default kept). Returns false on any error.
	 */
	static bool ReadConfig(const FJsonObject& Balance, FPropsLayoutConfig& Out, TArray<FString>& Errors);

	const FString& GetName() const { return Name; }
	const FPropsLayoutConfig& GetConfig() const { return Config; }
	FIntPoint GetBoundsMin() const { return BoundsMin; }
	FIntPoint GetBoundsMax() const { return BoundsMax; }
	const TArray<FPropsWalkLayer>& GetLayers() const { return Layers; }
	const FPropsWalkLayer* FindLayer(const FString& LayerName) const;
	/** Doorways sorted by id (ordinal). */
	const TArray<FPropsDoorway>& GetDoorways() const { return Doorways; }
	const FPropsDoorway* FindDoorway(const FString& Id) const;
	/** Every gap between collinear wall segments, incl. the cut's (WallKind "cut_wall": crossings, never doorways). */
	const TArray<FPropsWallGap>& GetWallGaps() const { return WallGaps; }
	const TArray<FPropsZone>& GetZones() const { return Zones; }
	const FPropsZone* FindZone(const FString& Id) const;
	const TArray<FPropsAnchorPoint>& GetAmbushPoints() const { return AmbushPoints; }
	const TArray<FString>& GetMandatoryRouteIds() const { return MandatoryRouteIds; }
	/** Every route in layout order. */
	const TArray<FPropsRoute>& GetRoutes() const { return Routes; }
	const FPropsRoute* FindRoute(const FString& Id) const;
	/** Every cover in layout order (Height = size[2]). */
	const TArray<FPropsCover>& GetCovers() const { return Covers; }
	/** Every building in layout order. */
	const TArray<FPropsBuildingSummary>& GetBuildings() const { return BuildingSummaries; }
	FIntPoint GetSpawnPos() const { return SpawnPos; }
	FIntPoint GetEndPos() const { return EndPos; }

	bool IsInBounds(FIntPoint P) const;
	bool IsInZone(FIntPoint P, const FString& ZoneId) const;

	/**
	 * Ground: in bounds and not over the cut (a crossing counts as ground). Cut: inside the cut polygon. Roof: footprint minus the roof
	 * inset minus the roof holes. Floor: footprint minus that floor's holes. Unknown layer: false.
	 */
	bool IsWalkable(FIntPoint P, const FString& WalkLayer) const;

	/**
	 * Obstacle test on a walk layer; ClearanceCm = the object's half size + min_gap_any_cm. Walls of the layer (schema.ts layerWalls:
	 * ground = every wall on floor 0 incl. building / fence / cut walls; floor:<B>:<n> = the footprint perimeter + B's walls on floor n;
	 * roof = the parapet; cut = the trench edges), each widened by thickness / 2 + clearance; on the ground also the footprints of
	 * non-enterable buildings (chimneys, facades) and the covers (oriented boxes, silos included). Unknown layer: false.
	 */
	bool IsBlocked(FIntPoint P, const FString& WalkLayer, int32 ClearanceCm) const;

	/** IsWalkable && !IsBlocked. */
	bool IsFree(FIntPoint P, const FString& WalkLayer, int32 ClearanceCm) const;

	/** Lattice scan rectangle of a walk layer (inclusive): ground = bounds, cut = the cut polygon's box, floor / roof = the footprint. */
	bool GetLayerExtent(const FString& WalkLayer, FIntPoint& OutMin, FIntPoint& OutMax) const;

	/** Exact: a wall of the layer (schema.ts layerWalls) has its face within ClearanceCm of P (centre line <= thickness / 2 + clearance). */
	bool IsNearWall(FIntPoint P, const FString& WalkLayer, int32 ClearanceCm) const;

	/** Exact: a cover box is within ClearanceCm of P (covers stand on the ground layer only). */
	bool IsNearCover(FIntPoint P, const FString& WalkLayer, int32 ClearanceCm) const;

	/**
	 * The nearest wall of the layer whose face is within ClearanceCm (smallest exact ceil distance to its centre line; ties = the first
	 * in the layer's wall order). False when there is none. bOutAlongX: the wall runs mostly along X (|dx| >= |dy|).
	 */
	bool FindNearestWallAxis(FIntPoint P, const FString& WalkLayer, int32 ClearanceCm, bool& bOutAlongX) const;

	/** Distance (cm, rounded up) from P to the nearest mandatory route leg whose both ends lie on WalkLayer; MAX_int32 when none. */
	int32 DistanceToMandatoryRouteCm(FIntPoint P, const FString& WalkLayer) const;

	/** Exact: a mandatory route leg on WalkLayer passes within RadiusCm of P. */
	bool IsNearMandatoryRoute(FIntPoint P, const FString& WalkLayer, int32 RadiusCm) const;

	/**
	 * Distance (cm, rounded up, CeilDistance) from P to the nearest leg of Route; Layer non-null = only legs with both ends on that
	 * layer, null = every leg (2D across layers). MAX_int32 when no leg qualifies.
	 */
	int32 DistanceToRouteCm(FIntPoint P, const FPropsRoute& Route, const FString* Layer) const;

	/** DistanceToRouteCm over every mandatory route, every leg (2D across layers). MAX_int32 when there is none. */
	int32 DistanceToMandatoryRouteAnyLayerCm(FIntPoint P) const;

	/**
	 * The sight blockers of an eye standing on EyeLayer (evaluate.ts sightWalls): a roof = every wall whose top (floor x LevelHeightCm +
	 * height) and every cover whose height reaches the eye (roof level x LevelHeightCm + EyeHeightCm); any other layer = that layer's
	 * walls (schema.ts layerWalls), on the ground plus the 4 edges of every cover at least TallCoverMinCm tall. Walls in wall order, then
	 * covers in layout order, each cover's corners (+x+y, -x+y, -x-y, +x-y in its local frame, the table rotation) joined in that order.
	 * Unknown layer: empty.
	 */
	void GetSightBlockers(const FString& EyeLayer, TArray<FPropsSightSegment>& Out) const;

	/** Exact: the closed segment From -> To touches or crosses a blocker (closed segments, collinear overlap counts). */
	static bool IsSightBlocked(FIntPoint From, FIntPoint To, const TArray<FPropsSightSegment>& Blockers);

	/** Exclusion shapes for the given radii, in a fixed order: spawn, end, ladders, stairs, doorways, routes, crossings, holes. */
	TArray<FPropsExclusion> BuildExclusions(const FPropsExclusionParams& Params) const;

	/** True when any shape contains P on the layer. */
	static bool IsExcluded(const TArray<FPropsExclusion>& Shapes, FIntPoint P, const FString& WalkLayer);

private:
	struct FWall
	{
		FString Id;
		FString Kind;
		FString Building;
		FIntPoint A = FIntPoint::ZeroValue;
		FIntPoint B = FIntPoint::ZeroValue;
		int32 Thickness = 0;
		/** Height level the wall stands on (missing = 0). */
		int32 Floor = 0;
		/** "height" (cm above its floor; missing = 0): the elevated sight rule (C4). */
		int32 Height = 0;
	};
	struct FLayerWall
	{
		FIntPoint A = FIntPoint::ZeroValue;
		FIntPoint B = FIntPoint::ZeroValue;
		int32 Thickness = 0;
	};
	struct FBuildingDoor
	{
		FIntPoint Pos = FIntPoint::ZeroValue;
		int32 Floor = 0;
	};
	struct FFloorHole
	{
		FIntPoint Pos = FIntPoint::ZeroValue;
		int32 Floor = 0;
	};
	struct FStair
	{
		FString Id;
		FIntPoint Pos = FIntPoint::ZeroValue;
		FIntPoint Top = FIntPoint::ZeroValue;
		bool bHasMid = false;
		FIntPoint Mid = FIntPoint::ZeroValue;
	};
	struct FBuilding
	{
		FString Id;
		FIntPoint Min = FIntPoint::ZeroValue;
		FIntPoint Max = FIntPoint::ZeroValue;
		int32 Floors = 1;
		bool bHollow = false;
		bool bEnterable = false;
		bool bRoofWalkable = false;
		TArray<FBuildingDoor> Doors;
		TArray<FStair> Stairs;
		TArray<FIntPoint> RoofHoles;
		TArray<FFloorHole> FloorHoles;
	};
	struct FLadder
	{
		FString Id;
		FIntPoint Pos = FIntPoint::ZeroValue;
		FIntPoint Foot = FIntPoint::ZeroValue;
		FIntPoint Top = FIntPoint::ZeroValue;
		FString From = TEXT("ground");
		FString To = TEXT("ground");
	};
	struct FCrossing
	{
		FString Id;
		TArray<FIntPoint> Poly;
	};

	void BuildLayers();
	void FindGaps();
	void BuildDoorways();
	static FString LayerNameFor(const FString& Building, int32 Floor);

	FString Name;
	FPropsLayoutConfig Config;
	FIntPoint BoundsMin = FIntPoint::ZeroValue;
	FIntPoint BoundsMax = FIntPoint::ZeroValue;
	int32 GroundZ = 0;
	FIntPoint SpawnPos = FIntPoint::ZeroValue;
	FIntPoint EndPos = FIntPoint::ZeroValue;
	TArray<FWall> Walls;
	TArray<FBuilding> Buildings;
	TArray<FPropsBuildingSummary> BuildingSummaries;
	TArray<FPropsCover> Covers;
	TArray<FLadder> Ladders;
	TArray<FPropsZone> Zones;
	TArray<FPropsAnchorPoint> AmbushPoints;
	TArray<FPropsRoute> Routes;
	TArray<FString> MandatoryRouteIds;
	bool bHasCut = false;
	TArray<FIntPoint> CutPoly;
	int32 CutLevel = -1;
	bool bHasCutFloorZ = false;
	int32 CutFloorZ = 0;
	TArray<FCrossing> Crossings;
	TArray<FPropsWalkLayer> Layers;
	/** Walls per walk layer, parallel to Layers (schema.ts layerWalls). */
	TArray<TArray<FLayerWall>> LayerWalls;
	TArray<FPropsWallGap> WallGaps;
	TArray<FPropsDoorway> Doorways;
};
