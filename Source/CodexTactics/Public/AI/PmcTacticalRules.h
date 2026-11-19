#pragma once

#include "CoreMinimal.h"
#include "AI/PmcCommanderRules.h"

/**
 * The PMC side of the TACTICAL MAP (docs/design/TACTICAL_MAP.md P2, designer 2026-11-17: "the dangerous places must already affect gameplay"):
 * the terrain terms of the utility commander and the squad's placement on the analysis spots - ambushers at chokepoint EXITS (hiding spots
 * from TacticalMapRules::BestAmbushSpots), marksmen on overwatch roofs, riflemen at windows covering the route, an enemy on PART of the
 * ladder-only roofs (TacticalMapRules::PickLadderOnlyRoofs, the file's 0.65), jumpers above drop spots where the archetype may jump; never
 * stand IN a chokepoint (DangerAt(p, Pmc) as a cost). Pure rules: UPmcSquadSubsystem gathers the candidates from UTacticalMapSubsystem
 * and executes the result. Every term is 1 / empty without a tactical file (danger 0, no candidates): the old behaviour, unchanged.
 * Weights: pmc_commander.json "terrain_danger", "ambush_exit", "overwatch_roof", "window_position", "route_cover", "chokepoint_avoid",
 * "choke_leave_danger", "window_share" (trainer genes, pmc_commander.bounds.json) + "tactical_reach_m", "marksman_band_min_m",
 * "ladder_roof_men" (frozen). UE-only, no Godot reference. Tested in CodexTactics.AI.PmcTactical.*.
 */

/** One candidate spot for a post (gathered by UPmcSquadSubsystem from the tactical map). */
struct CODEXTACTICS_API FPmcPostCandidate
{
	EPmcPostKind Kind = EPmcPostKind::None;
	/** Where the man stands (world cm, the item's floor). */
	FVector Location = FVector::ZeroVector;
	/** Where he looks, degrees (UE yaw). */
	float Yaw = 0.f;
	/** The analysis query's own score, 0..1. */
	float Score = 0.f;
	/** DangerAt(Location, Pmc): inside a chokepoint is bad for anyone. */
	float Danger = 0.f;
	/** One man per group (a roof's building, a window's building + floor, a hiding spot's id). */
	FString Group;
	/** Drop spots: the archetypes that may jump from it (TacticalMapFile drop_spots[].jumpers). */
	TArray<FString> Jumpers;
	/** The index into the source list of the tactical file (hiding spot / roof / window / ladder roof / drop spot) and the roof's spot. */
	int32 SourceIndex = INDEX_NONE;
	int32 SpotIndex = INDEX_NONE;
	/** Overwatch roofs: overwatch_roofs[].range_m in cm. 0 for every other kind. */
	float RangeCm = 0.f;
};

/** One fighter as the placement sees him. */
struct CODEXTACTICS_API FPmcPlacementFighter
{
	/** A long-reach weapon (the sniper rifle; MarkMarksmen flags the longest reach when nobody is). */
	bool bMarksman = false;
	/** The fighter's archetype id for the drop spots' jumpers list ("pmc" for the operative body). */
	FString Archetype = TEXT("pmc");
	/** His weapon band's max, cm (the longest reach is the marksman). */
	float BandMaxCm = 0.f;
};

/** A fighter's post: where he spawns / waits before contact, and where he looks. */
struct CODEXTACTICS_API FPmcTacticalPost
{
	EPmcPostKind Kind = EPmcPostKind::None;
	FVector Location = FVector::ZeroVector;
	float Yaw = 0.f;
	/** The effective score the placement chose it with (0..~2). */
	float Score = 0.f;
	float Danger = 0.f;
	FString Group;
	int32 SourceIndex = INDEX_NONE;
	int32 SpotIndex = INDEX_NONE;
	/** Overwatch roofs: overwatch_roofs[].range_m in cm (0 for every other kind). The engagement range is the smaller of this and the weapon reach. */
	float RangeCm = 0.f;

	bool IsValid() const { return Kind != EPmcPostKind::None; }
};

namespace PmcTacticalRules
{
	/** "none" / "ambush" / "overwatch" / "window" / "ladder_roof" / "drop" (logs, JSON). */
	CODEXTACTICS_API const TCHAR* PostKindName(EPmcPostKind Kind);

	/** The "danger of my position" factor of a static order / a position: 1 - TerrainDanger x Danger (1 at danger 0). */
	CODEXTACTICS_API float PositionFactor(float Danger, const FPmcWeights& Weights);

	/** The urge to leave a dangerous spot (TakeCover / Relocate): 1 + TerrainDanger x Danger (1 at danger 0). */
	CODEXTACTICS_API float LeaveFactor(float Danger, const FPmcWeights& Weights);

	/** The weight of a post kind in the placement / the Ambush order: 1 + ambush_exit / overwatch_roof / window_position (1 for the rest). */
	CODEXTACTICS_API float PostKindFactor(EPmcPostKind Kind, const FPmcWeights& Weights);

	/** A candidate's effective score: Score x PostKindFactor x PositionFactor(Danger). */
	CODEXTACTICS_API float CandidateScore(const FPmcPostCandidate& Candidate, const FPmcWeights& Weights);

	/**
	 * A cover slot's terrain factor (FindCoverNear): PositionFactor(its danger) x (1 + RouteCover x RouteFacing x (1 near an analysis cover
	 * spot, else 0.5)). RouteFacing = how squarely the slot's wall shields him from the squad's route (0..1; 0 without a route).
	 */
	CODEXTACTICS_API float SlotTerrainFactor(float Danger, float RouteFacingValue, bool bNearAnalysisCover, const FPmcWeights& Weights);

	/** How squarely a wall (its normal points at the man's back side: covers the -WallNormal side) faces RoutePoint: clamp(dot(-n, dir), 0, 1). */
	CODEXTACTICS_API float RouteFacing(const FVector& Slot, const FVector& WallNormal, const FVector& RoutePoint);

	/** The point of the polyline Route nearest to Point (planar; Route's own Z). Point itself for an empty route. */
	CODEXTACTICS_API FVector NearestOnRoute(TConstArrayView<FVector> Route, const FVector& Point);

	/** The planar distance from Point to the polyline Route (a huge number for an empty route). */
	CODEXTACTICS_API float DistanceToRoute(TConstArrayView<FVector> Route, const FVector& Point);

	/**
	 * The fall-back directions tried (degrees off straight away from the threat): 0, +35, -35, +70, -70 - the first five FallBack goal
	 * candidates (PmcCommanderRules::FallBackPoint rotated round the fighter).
	 */
	CODEXTACTICS_API TArray<float> FallBackAngles();

	/**
	 * The fall-back goal among candidates at AnglesDeg off straight away with the PMC-side danger Dangers: score = (0.5 + 0.5 cos angle) x
	 * (1 - ChokepointAvoid x danger); the best index (straight away wins ties and every candidate at danger 0). INDEX_NONE for none.
	 */
	CODEXTACTICS_API int32 PickFallBack(TConstArrayView<float> AnglesDeg, TConstArrayView<float> Dangers, const FPmcWeights& Weights);

	/** When nobody carries a long-reach weapon, the fighter with the longest band max (the lowest index on a tie) becomes the marksman. */
	CODEXTACTICS_API void MarkMarksmen(TArray<FPmcPlacementFighter>& Fighters);

	/** The archetype may jump from a drop spot: its jumpers list names it (case-insensitive) or "any". */
	CODEXTACTICS_API bool CanJump(const FString& Archetype, TConstArrayView<FString> Jumpers);

	/**
	 * The squad's posts on the tactical map: marksmen first on the best overwatch roof spots (one man per roof), then up to
	 * Weights.LadderRoofMen men on the picked ladder-only roofs (the candidates are only the picked ones), a jumper archetype on the best
	 * drop edge, the first of the rest on the best ambush spot (the squad's ambush at a chokepoint exit), the others at windows (a seeded
	 * WindowShare, when window candidates exist) or ambush spots (best effective score first); a post at least MinSpacingCm from every other one, one per group. A fighter with nothing left gets an invalid post
	 * (Kind None: the old line spawn). Deterministic for the same input + Seed. Empty candidates = every post invalid.
	 */
	CODEXTACTICS_API TArray<FPmcTacticalPost> PlanPlacement(const TArray<FPmcPlacementFighter>& Fighters, const TArray<FPmcPostCandidate>& Candidates,
		const FPmcWeights& Weights, int32 Seed, float MinSpacingCm = 300.f);

	/** The man stands on his post: within ArriveCm (planar) and on its level (|dz| <= LevelToleranceCm). */
	CODEXTACTICS_API bool IsAtPost(const FVector& Feet, const FVector& Post, float ArriveCm, float LevelToleranceCm);

	/** A post walk that gets him less than this much closer to his post ended short (it counts toward the three tries that drop it). */
	constexpr float PostWalkMinProgressCm = 100.f;

	/**
	 * The post walk issued at WalkFromCm from the post ended short of it: he is not at least PostWalkMinProgressCm closer now (NowCm).
	 * False without a walk (WalkFromCm < 0) - a walk that made progress, or one another order interrupted, is no failed try.
	 */
	CODEXTACTICS_API bool PostWalkEndedShort(float WalkFromCm, float NowCm);

	/**
	 * The point InsetCm before a roof edge that the shot reaches at ExitDistanceCm along Dir (planar). Dir need not be a unit.
	 * Already within InsetCm, a zero Dir, or no positive exit stays at Location. Z is unchanged.
	 */
	CODEXTACTICS_API FVector PointInsetFromRoofEdge(const FVector& Location, const FVector2D& Dir, float ExitDistanceCm, float InsetCm);

	/**
	 * Slide an overwatch spot toward the street until it is InsetCm inside Footprint, along Dir (the shot, not the post's facing).
	 * The spot stays when it is already that close, outside the footprint, or Dir is zero. Z is unchanged.
	 * About 60 cm: a kneeling muzzle then meets the parapet inside lip_cm, and the point is still on the roof.
	 */
	CODEXTACTICS_API FVector SnapOverwatchSpotToParapet(const FVector& Location, const FVector2D& Dir, const FBox2D& Footprint, float InsetCm);
}
