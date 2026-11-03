#pragma once

#include "CoreMinimal.h"
#include "AI/Navigation/NavigationTypes.h"
#include "NavMesh/RecastNavMesh.h"

class ARecastNavMesh;
class UWorld;

/**
 * Pure reachability over a directed graph (the navmesh's polys and its nav links; the soft-lock fix of 2026-11-03, docs/systems/
 * stances-and-movement.md "Falls and drops", "The soft-lock"). Which nodes can still get to the MAIN area (the anchors: the player start /
 * the squad's spawn) - a one-way drop (a drop link, a hole link) must only land on such a node, else it drops the squad into a dead-end pocket
 * it can never walk out of. No UObject: unit tested in CodexTactics.Falls.Reach.*. UE-only (no Godot reference).
 */
class CODEXTACTICS_API FReachGraph
{
public:
	/** Empties the graph and makes NodeCount nodes (0..NodeCount-1), none reaching. */
	void Reset(int32 NodeCount);

	/** A directed edge: From can walk / drop / climb to To. Before Solve, or after it (AddEdge then updates the reaching set at once). */
	void AddEdge(int32 From, int32 To);

	/** Marks the anchors and computes every node that reaches one of them (backwards over the edges). */
	void Solve(const TArray<int32>& Anchors);

	/** Node reaches an anchor (false for an unknown node). */
	bool Reaches(int32 Node) const { return Reaching.IsValidIndex(Node) && Reaching[Node]; }

	int32 NumNodes() const { return Reverse.Num(); }
	int32 NumReaching() const;

	/** A one-way link candidate (Start -> End) for SelectLinks. */
	struct FCandidate
	{
		int32 Start = INDEX_NONE;
		int32 End = INDEX_NONE;
	};

	/**
	 * The candidates worth keeping: a one-way link whose End reaches the anchors - over the graph and the other KEPT links (a drop onto a lower roof
	 * that is left only by another drop is kept when that one is). Kept links become edges of this graph. Returns per candidate: kept or not.
	 * A candidate with an unknown end is never kept; one with an unknown start is judged by its end alone.
	 */
	TArray<bool> SelectLinks(const TArray<FCandidate>& Candidates);

private:
	/** Marks Node and everything that reaches it (backwards) as reaching. */
	void Spread(int32 Node);

	/** Per node: the nodes with an edge INTO it. */
	TArray<TArray<int32>> Reverse;
	TBitArray<> Reaching;
	bool bSolved = false;
};

/**
 * The world's infantry navmesh as an FReachGraph (every ground poly and nav link poly; the Null-area ones left out): built once on demand,
 * the anchors = the player starts (none: the operatives). Used by UDropLinkSubsystem (a generated drop link must land where the main navmesh is
 * reachable), the operatives' off-navmesh rescue (ULedgeFallComponent: the nearest point that reaches the main area) and the smokes' sweep.
 */
class CODEXTACTICS_API FDropLinkReach
{
public:
	/** A link's ends (the smokes' sweep). */
	struct FLinkEnds
	{
		FVector Start = FVector::ZeroVector;
		FVector End = FVector::ZeroVector;
		FString Name;
	};

	/** Builds the graph of World's default navmesh. False without a recast navmesh / without an anchor on it. */
	bool Build(UWorld& World);

	/** The poly under a point (the landing's box: 40 x 40 x 80 cm); 0 when none. */
	NavNodeRef FindPoly(UWorld& World, const FVector& Point, const FVector& Extent = FVector(40.f, 40.f, 80.f)) const;

	/** The landing poly reaches the main area (false off the navmesh / before Build). */
	bool LandingReachesMain(UWorld& World, const FVector& Landing) const;

	/**
	 * The soft-lock rescue (ULedgeFallComponent): the nearest navmesh point that reaches the main area for a body at Feet (FallRules::PickRescuePoint
	 * over the ground polys within rescue.max_cm: his level first; the point is the closest one on the chosen poly). False when none.
	 */
	bool FindRescuePoint(UWorld& World, const FVector& Feet, FVector& OutPoint) const;

	/** The node index of a poly (INDEX_NONE when unknown). */
	int32 NodeOf(NavNodeRef Poly) const;

	FReachGraph& GetGraph() { return Graph; }
	const FReachGraph& GetGraph() const { return Graph; }
	bool IsBuilt() const { return bBuilt; }
	int32 GetPolyCount() const { return Graph.NumNodes(); }
	int32 GetReachingCount() const { return Graph.NumReaching(); }
	int32 GetAnchorCount() const { return AnchorCount; }
	double GetBuildMs() const { return BuildMs; }

private:
	TMap<NavNodeRef, int32> Index;
	/** The ground polys (refs and centres; the link polys are not here). */
	TArray<FNavPoly> GroundPolys;
	FReachGraph Graph;
	bool bBuilt = false;
	int32 AnchorCount = 0;
	double BuildMs = 0.0;
};
