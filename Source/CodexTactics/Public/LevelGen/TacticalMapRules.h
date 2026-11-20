#pragma once

#include "CoreMinimal.h"

/**
 * Pure rules of the TACTICAL MAP (docs/design/TACTICAL_MAP.md P1, designer 2026-11-17: "bots read the terrain"): the runtime file
 * Content/Data/LevelGen/tactical/<Map>.tactical.json ("codex.tactical_map/1", written by the Training Ground «Принять» / «Собрать в
 * Unreal» / scripts/export-tactical-map.ts from the layout's danger analysis - Tools/TrainingGround/src/levelgen/tacticalMap.ts) and the
 * queries the AI asks it (dangerous places, ambush / overwatch / window / drop positions, chokepoint crossing, detour), plus the seeded
 * ladder-only roof picker for the placement. Thresholds / radii / weights: Content/Data/LevelGen/tactical_runtime.json
 * ("codex.tactical_runtime/1", FTacticalRuntimeConfig). No world: UTacticalMapSubsystem holds the loaded file. All points are WORLD cm.
 * UE-only (no Godot reference: the Godot archive had no generated maps).
 */

/** Whose danger DangerAt measures. */
enum class ETacticalSide : uint8
{
	/** The player squad: the dangerous places plus the enemy's positions over them (drop / window / roof / hiding). */
	Squad,
	/** The PMC: only the places themselves (standing IN a chokepoint is bad for anyone); the threat positions are its own. */
	Pmc
};

/** A dangerous place on a line (a chokepoint of any kind). */
struct CODEXTACTICS_API FTacticalChokepoint
{
	FString Id;
	/** only_way / drop_ambush / canyon / clutter / narrow / hiding / window_fire / sniper (chokepoints.json kind_priority). */
	FName Kind;
	FString KindName;
	TArray<FName> Tags;
	float Score = 0.f;
	FString Layer;
	/** The narrowest point. */
	FVector Location = FVector::ZeroVector;
	TArray<FVector> Line;
	/** The outline on the place's floor (world XY at Location.Z). */
	TArray<FVector2D> Poly;
	/** The outline's 2D bounds (Parse / UpdateDerived; invalid without an outline): the cheap reject before InsidePolygon. */
	FBox2D Bounds = FBox2D(ForceInit);
	float LengthM = 0.f;
	/** -1 = open ground (null in the file). */
	float WidthM = -1.f;
	float LaneM = -1.f;
	FString Route;
	/** "none" = no way round (the only path), "detour" = a way round DetourM longer, "n/a" = the stretch ends a line. */
	FString Alternative;
	/** -1 when there is no detour figure. */
	float DetourM = -1.f;
	TArray<FString> DropSpots;
	TArray<FString> Windows;
	TArray<FString> Overwatch;
	TArray<FString> HidingSpots;
	float RevealM = -1.f;
	float RevealStealthM = -1.f;
	FString Building;
	int32 Floor = 0;
};

/** A drop-down ambush: a jumper waits on Edge (roof / wall top) and lands on Land. */
struct CODEXTACTICS_API FTacticalDropSpot
{
	FString Id;
	FString Surface;
	bool bWallTop = false;
	FString Layer;
	FVector Edge = FVector::ZeroVector;
	FVector Land = FVector::ZeroVector;
	float DropCm = 0.f;
	float GapCm = 0.f;
	FString Access;
	bool bLadderOnly = false;
	TArray<FString> Jumpers;
	FString Line;
	TArray<FString> Places;
};

/** A window / loophole whose fire covers the lines. */
struct CODEXTACTICS_API FTacticalWindow
{
	FString Id;
	FString Building;
	int32 Floor = 0;
	FString Layer;
	FString Kind;
	/** The window point on the wall (floor z) and the stand point behind it, inside. */
	FVector Location = FVector::ZeroVector;
	FVector Stand = FVector::ZeroVector;
	/** Outward look yaw, degrees (UE). */
	float Yaw = 0.f;
	float CoveredM = 0.f;
	float MinDistM = 0.f;
	TArray<FString> Places;
};

struct CODEXTACTICS_API FTacticalOverwatchSpot
{
	FVector Location = FVector::ZeroVector;
	float FacingYaw = 0.f;
	float CoveredM = 0.f;
	float LongestLosM = 0.f;
	FString Building;
	int32 Floor = 0;
};

/** A roof access point (stairs, ladder, hatch) with its landing point on the roof. */
struct CODEXTACTICS_API FTacticalRoofAccess
{
	FString Id;
	/** stairs / ladder / hatch */
	FString Kind;
	FVector Top = FVector::ZeroVector;
};

/** A roof a marksman can reach, with its best spots. */
struct CODEXTACTICS_API FTacticalOverwatchRoof
{
	FString Building;
	FString Layer;
	int32 Level = 0;
	int32 Floor = 0;
	float Z = 0.f;
	float HeightCm = 0.f;
	float RangeM = 0.f;
	TArray<FString> Access;
	/** stairs / ladder / hatch. */
	FString AccessKind;
	TArray<FTacticalRoofAccess> AccessPoints;
	bool bLadderOnly = false;
	TArray<FTacticalOverwatchSpot> Spots;
	TArray<FString> Places;
};

/** A spot the squad walking a line sees only from close (an ambusher waits here). */
struct CODEXTACTICS_API FTacticalHidingSpot
{
	FString Id;
	FString Layer;
	FVector Location = FVector::ZeroVector;
	/** Towards the threatened route point, degrees. */
	float FacingYaw = 0.f;
	float RevealNormalM = 0.f;
	float RevealStealthM = 0.f;
	bool bStealthOnly = false;
	bool bBlind = false;
	float RushM = 0.f;
	FString Cover;
	float CoverHeightCm = 0.f;
	FString ThreatLine;
	FString ThreatRoute;
	FVector ThreatLocation = FVector::ZeroVector;
	FString Place;
	/** Place resolved to an index into FTacticalMapFile::Chokepoints (Parse / UpdateDerived; INDEX_NONE: none). */
	int32 PlaceIndex = INDEX_NONE;
	FString Building;
	int32 Floor = 0;
};

/** A walkable roof reached by ladder(s) only (the P2 placement puts enemies on PART of them). */
struct CODEXTACTICS_API FTacticalLadderRoof
{
	FString Building;
	FString Layer;
	float Z = 0.f;
	FVector Center = FVector::ZeroVector;
	TArray<FVector2D> Poly;
	TArray<FString> Ladders;
	TArray<FVector> LadderTops;
	TArray<FTacticalRoofAccess> AccessPoints;
	bool bOverwatch = false;
	TArray<FString> DropSpots;
};

/** Every walkable roof in the layout with its floor, z, access points and ladder-only flag. */
struct CODEXTACTICS_API FTacticalWalkableRoof
{
	FString Building;
	FString Layer;
	int32 Floor = 0;
	float Z = 0.f;
	TArray<FTacticalRoofAccess> Access;
	bool bLadderOnly = false;
};

/** Route segments >= 10 m with no cover within 2 m of the path, and the cover objects that see them. */
struct CODEXTACTICS_API FTacticalOpenCrossing
{
	FString Id;
	FString Route;
	FVector Start = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
	float LengthM = 0.f;
	float NearestCoverCm = 0.f;
	TArray<FString> SeeingCovers;
};

/** Cover objects within 25 m of a mission goal with the sector each covers. */
struct CODEXTACTICS_API FTacticalGoalSite
{
	FString GoalId;
	FString CoverId;
	FVector Location = FVector::ZeroVector;
	float DistM = 0.f;
	float FacingYaw = 0.f;
	float SectorArcDeg = 90.f;
	FString CoverKind;
	float CoverHeightCm = 0.f;
};

/** The analysis thresholds the file carries (defaults = chokepoints.json today). */
struct CODEXTACTICS_API FTacticalThresholds
{
	float DropReachCm = 400.f;
	float WindowRangeM = 60.f;
	float SniperRangeM = 110.f;
	float RevealCloseM = 12.f;
	float DetourCriticalM = 40.f;
	float MinScore = 0.25f;
	/** The share of ladder-only roofs that get an enemy (designer 2026-11-17: 0.65). */
	float LadderOnlyFactor = 0.65f;
};

/** The parsed runtime file. */
struct CODEXTACTICS_API FTacticalMapFile
{
	FString Schema;
	FString Map;
	FString Layout;
	FString GeometryHash;
	FString AnalysisSource;
	FString Weather;
	FString TimeOfDay;
	float GroundZ = 0.f;
	FTacticalThresholds Thresholds;
	TArray<FTacticalChokepoint> Chokepoints;
	TArray<FTacticalDropSpot> DropSpots;
	TArray<FTacticalWindow> Windows;
	TArray<FTacticalOverwatchRoof> OverwatchRoofs;
	TArray<FTacticalHidingSpot> HidingSpots;
	TArray<FTacticalLadderRoof> LadderOnlyRoofs;
	TArray<FTacticalWalkableRoof> WalkableRoofs;
	TArray<FTacticalOpenCrossing> OpenCrossings;
	TArray<FTacticalGoalSite> GoalSites;

	bool IsEmpty() const
	{
		return Chokepoints.IsEmpty() && DropSpots.IsEmpty() && Windows.IsEmpty() && OverwatchRoofs.IsEmpty() && HidingSpots.IsEmpty() && LadderOnlyRoofs.IsEmpty() && WalkableRoofs.IsEmpty() && OpenCrossings.IsEmpty() && GoalSites.IsEmpty();
	}
};

/** Content/Data/LevelGen/tactical_runtime.json - every radius / weight of the queries (the defaults below = the shipped file). */
struct CODEXTACTICS_API FTacticalRuntimeConfig
{
	/** Same floor: |z - item z| within this, cm. */
	float FloorToleranceCm = 150.f;

	/** DangerAt: a place counts fully inside its outline and fades to 0 over this distance outside it, cm. */
	float ZoneFalloffCm = 300.f;
	/** DangerAt (squad side): the weight of each threat kind (combined with the places as 1 - prod(1 - term) over kinds). */
	float DropWeight = 0.6f;
	float WindowWeight = 0.35f;
	float SniperWeight = 0.1f;
	float HidingWeight = 0.4f;
	/** A ladder-only drop roof scales its drop term by the ladder-only factor (jumpers must be placed there). */
	bool bScaleLadderOnlyDrops = true;
	/** Window fire: full up to this distance, linear to 0 at the file's window range; the half sector either side of the yaw. */
	float WindowFullM = 28.f;
	float WindowHalfFovDeg = 75.f;
	/** A hiding spot threatens points within this of its threatened route point, cm. */
	float HidingThreatRadiusCm = 300.f;

	/** BestAmbushSpots: candidates threaten a point within this of the segment, cm; score weights; a stealth-only spot's factor. */
	float AmbushSegmentRadiusCm = 800.f;
	float AmbushRevealWeight = 0.5f;
	float AmbushExitWeight = 0.3f;
	float AmbushPlaceWeight = 0.2f;
	float AmbushStealthOnlyFactor = 0.5f;

	/** OverwatchSpots: score = distance weight x (1 - d / range) + coverage weight x min(1, covered / full). */
	float OverwatchDistanceWeight = 0.5f;
	float OverwatchCoverageWeight = 0.5f;
	float OverwatchCoverageFullM = 1000.f;

	/** IsChokepoint / SafeDetour: the path is sampled every this many cm. */
	float PathSampleStepCm = 100.f;
	/** SafeDetour: take the way round when it is at most this many metres longer. */
	float ShortDetourM = 40.f;

	/** Ladder-only factor when the file carries none. */
	float LadderOnlyFactorFallback = 0.65f;

	/** Debug draw (Codex.TacticalMap.Draw): only items within this of the view, cm; lift above the floor, cm. */
	float DrawRadiusCm = 9000.f;
	float DrawLiftCm = 20.f;

	/** TacticalMapSmoke: navmesh projection box (half extents, cm) and the surface trace tolerance for spots off the navmesh, cm. */
	FVector ProjectExtentCm = FVector(150.f, 150.f, 250.f);
	float SurfaceToleranceCm = 120.f;
	/** TacticalMapSmoke: DangerAt inside a top place at least / on an open street at most; an open street is this far from every place. */
	float SmokeHighDanger = 0.6f;
	float SmokeLowDanger = 0.35f;
	float SmokeOpenClearanceCm = 2000.f;
};

/** DangerAt's answer. */
struct CODEXTACTICS_API FTacticalDanger
{
	/** 0..1. */
	float Danger = 0.f;
	/** The strongest place term and its id (empty: none). */
	float ZoneTerm = 0.f;
	FString ZoneId;
	/** The place tags and threat kinds that contribute (drop_ambush / window_fire / sniper / hiding + the places' tags). */
	TArray<FName> Kinds;
};

/** A ranked candidate of a query: the index into the file's list (+ the spot index on an overwatch roof) and its score. */
struct CODEXTACTICS_API FTacticalPick
{
	int32 Index = INDEX_NONE;
	int32 SpotIndex = INDEX_NONE;
	float Score = 0.f;
	float DistanceCm = 0.f;
};

/** SafeDetour's answer (the simple version: the crossed chokepoint's own detour data). */
struct CODEXTACTICS_API FTacticalDetour
{
	bool bCrossesChokepoint = false;
	/** The highest-scored place the straight way crosses. */
	int32 ChokepointIndex = INDEX_NONE;
	FString Alternative;
	float DetourM = -1.f;
	/** There is no way round (alternative "none"): cross fast, one at a time, with overwatch. */
	bool bOnlyWay = false;
	/** A way round exists and is at most ShortDetourM longer: prefer it. */
	bool bTakeDetour = false;
};

namespace TacticalMapRules
{
	extern CODEXTACTICS_API const TCHAR* const Schema;
	extern CODEXTACTICS_API const TCHAR* const SchemaV1;
	extern CODEXTACTICS_API const TCHAR* const SchemaV2;
	extern CODEXTACTICS_API const TCHAR* const ConfigSchema;

	/**
	 * Parses the runtime file. False (OutErrors says why) on bad JSON or a wrong / newer schema; a bad item is skipped with an error line
	 * and the rest kept. An empty file (no items) parses fine.
	 */
	CODEXTACTICS_API bool Parse(const FString& Json, FTacticalMapFile& Out, TArray<FString>& OutErrors);

	/** Fills the derived fields (the places' Bounds, the hiding spots' PlaceIndex); Parse calls it - call it after editing a file by hand. */
	CODEXTACTICS_API void UpdateDerived(FTacticalMapFile& File);

	/** Parses tactical_runtime.json over the defaults (missing keys keep their default). False on bad JSON / a wrong schema. */
	CODEXTACTICS_API bool ParseConfig(const FString& Json, FTacticalRuntimeConfig& Out, TArray<FString>& OutErrors);

	/** 2D distance from a point to a polygon's outline; 0 inside. */
	CODEXTACTICS_API float DistanceToPolygon(const FVector2D& Point, TConstArrayView<FVector2D> Polygon);

	/** Same floor: |A.Z - B.Z| <= tolerance. */
	CODEXTACTICS_API bool IsSameFloor(float ZA, float ZB, float ToleranceCm);

	/** The places within RadiusCm (outline distance, same floor) of Point: indices sorted by distance, then score (desc), then id. */
	CODEXTACTICS_API TArray<int32> GetZonesNear(const FTacticalMapFile& File, const FTacticalRuntimeConfig& Config, const FVector& Point, float RadiusCm);

	/**
	 * The danger of standing at Point (0..1 + the contributing kinds). Place term = its score inside its outline, fading linearly to 0
	 * over ZoneFalloffCm outside it (max over places). Squad side adds the threat terms (max per kind): a drop spot's land point within
	 * the file's drop reach, a window's sector within its range (full to WindowFullM), an overwatch spot within its roof's range, a hiding
	 * spot whose threatened point is within HidingThreatRadiusCm (and whose reveal is close). Danger = 1 - prod(1 - term) over the kinds.
	 */
	CODEXTACTICS_API FTacticalDanger DangerAt(const FTacticalMapFile& File, const FTacticalRuntimeConfig& Config, const FVector& Point, ETacticalSide Side);

	/**
	 * The N best hiding spots to ambush a squad walking Start -> End: spots whose threatened point lies within AmbushSegmentRadiusCm of
	 * the segment; score = reveal weight x (1 - reveal / reveal_close) + exit weight x (how far along the segment it bites: the EXIT of a
	 * place is better than its mouth) + place weight x (its place's score), x the stealth-only factor. Sorted best first (then id).
	 */
	CODEXTACTICS_API TArray<FTacticalPick> BestAmbushSpots(const FTacticalMapFile& File, const FTacticalRuntimeConfig& Config, const FVector& Start, const FVector& End, int32 N);

	/** Overwatch roof spots whose roof range reaches Target (+ RadiusCm): best first (distance / coverage score); at most N (N <= 0 = all). */
	CODEXTACTICS_API TArray<FTacticalPick> OverwatchSpots(const FTacticalMapFile& File, const FTacticalRuntimeConfig& Config, const FVector& Target, float RadiusCm, int32 N);

	/** Windows whose fire sector (yaw +- WindowHalfFovDeg) and range reach a point within RadiusCm of Target: nearest first. */
	CODEXTACTICS_API TArray<FTacticalPick> WindowPositions(const FTacticalMapFile& File, const FTacticalRuntimeConfig& Config, const FVector& Target, float RadiusCm);

	/** Drop spots whose landing point lies within RadiusCm of Center (same floor): nearest first. */
	CODEXTACTICS_API TArray<FTacticalPick> DropSpotsAbove(const FTacticalMapFile& File, const FTacticalRuntimeConfig& Config, const FVector& Center, float RadiusCm);

	/**
	 * The path (polyline, sampled every PathSampleStepCm) runs through a place (inside its outline, same floor) scoring at least the
	 * file's min score. OutIndex = the first one along the path.
	 */
	CODEXTACTICS_API bool IsChokepoint(const FTacticalMapFile& File, const FTacticalRuntimeConfig& Config, TConstArrayView<FVector> Path, int32* OutIndex = nullptr);

	/** Every place the path crosses, in path order (each once). */
	CODEXTACTICS_API TArray<int32> ChokepointsOnPath(const FTacticalMapFile& File, const FTacticalRuntimeConfig& Config, TConstArrayView<FVector> Path);

	/** The straight way From -> To: the highest-scored place it crosses and that place's detour data (TACTICAL_MAP.md §3). */
	CODEXTACTICS_API FTacticalDetour SafeDetour(const FTacticalMapFile& File, const FTacticalRuntimeConfig& Config, const FVector& From, const FVector& To);

	/** A stable roll in [0, 1) of (Seed, Key): FNV-1a of the key mixed with the seed and a murmur3 finaliser - platform independent. */
	CODEXTACTICS_API double SeededRoll(int32 Seed, const FString& Key);

	/**
	 * The ladder-only roofs that get an enemy for this seed: each roof independently when SeededRoll(Seed, building) < Factor (so PART
	 * of them, ~Factor on average, independent of the list order). Factor < 0 = the file's ladder-only factor. Returns indices, ascending.
	 */
	CODEXTACTICS_API TArray<int32> PickLadderOnlyRoofs(const FTacticalMapFile& File, int32 Seed, float Factor = -1.f);

	/** Returns all goal sites associated with a specific goal ID. */
	CODEXTACTICS_API TArray<FTacticalGoalSite> GetGoalSitesForGoal(const FTacticalMapFile& File, const FString& GoalId);

	/** The layer's danger kind names (for logs): "drop_ambush" etc. */
	CODEXTACTICS_API FString JoinKinds(TConstArrayView<FName> Kinds);

	/** <Content>/Data/LevelGen/tactical/<MapName>.tactical.json (absolute). */
	CODEXTACTICS_API FString GetDefaultPathForMap(const FString& MapName);

	/** <Content>/Data/LevelGen/tactical_runtime.json (absolute). */
	CODEXTACTICS_API FString GetConfigPath();
}
