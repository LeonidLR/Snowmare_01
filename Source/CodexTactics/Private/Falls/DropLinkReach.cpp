// FReachGraph / FDropLinkReach: which navmesh polys still reach the main area (the soft-lock fix of 2026-11-03: no one-way drop into a dead-end
// pocket). UE-only system (no Godot reference).

#include "Falls/DropLinkReach.h"

#include "Characters/OperativeCharacter.h"
#include "Engine/World.h"
#include "Falls/FallRules.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/PlatformTime.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationSystem.h"
#if WITH_RECAST
#include "Detour/DetourNavMesh.h"
#endif

// --- FReachGraph (pure) ------------------------------------------------------------------------------------------------------

void FReachGraph::Reset(int32 NodeCount)
{
	Reverse.Reset();
	Reverse.SetNum(FMath::Max(0, NodeCount));
	Reaching.Init(false, FMath::Max(0, NodeCount));
	bSolved = false;
}

void FReachGraph::AddEdge(int32 From, int32 To)
{
	if (!Reverse.IsValidIndex(From) || !Reverse.IsValidIndex(To) || From == To)
	{
		return;
	}
	Reverse[To].AddUnique(From);
	if (bSolved && Reaching[To] && !Reaching[From])
	{
		Spread(From);
	}
}

void FReachGraph::Spread(int32 Node)
{
	TArray<int32> Open;
	Open.Add(Node);
	Reaching[Node] = true;
	while (!Open.IsEmpty())
	{
		const int32 Current = Open.Pop(EAllowShrinking::No);
		for (const int32 Before : Reverse[Current])
		{
			if (!Reaching[Before])
			{
				Reaching[Before] = true;
				Open.Add(Before);
			}
		}
	}
}

void FReachGraph::Solve(const TArray<int32>& Anchors)
{
	Reaching.Init(false, Reverse.Num());
	for (const int32 Anchor : Anchors)
	{
		if (Reverse.IsValidIndex(Anchor) && !Reaching[Anchor])
		{
			Spread(Anchor);
		}
	}
	bSolved = true;
}

int32 FReachGraph::NumReaching() const
{
	int32 Count = 0;
	for (TConstSetBitIterator<> It(Reaching); It; ++It)
	{
		++Count;
	}
	return Count;
}

TArray<bool> FReachGraph::SelectLinks(const TArray<FCandidate>& Candidates)
{
	TArray<bool> Kept;
	Kept.Init(false, Candidates.Num());
	// A fixed point: a link that lands on a reaching node makes its start reach too, which may open another candidate (a chain of drops).
	bool bChanged = true;
	while (bChanged)
	{
		bChanged = false;
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			const FCandidate& Link = Candidates[Index];
			if (!Kept[Index] && Reaches(Link.End))
			{
				Kept[Index] = true;
				// An unknown start (its poly not found) is still a good link - it only cannot open anything else.
				if (Reverse.IsValidIndex(Link.Start) && !Reaches(Link.Start))
				{
					AddEdge(Link.Start, Link.End);
					bChanged = true;
				}
			}
		}
	}
	return Kept;
}

// --- FDropLinkReach (the navmesh) --------------------------------------------------------------------------------------------

int32 FDropLinkReach::NodeOf(NavNodeRef Poly) const
{
	const int32* Found = Index.Find(Poly);
	return Found ? *Found : INDEX_NONE;
}

NavNodeRef FDropLinkReach::FindPoly(UWorld& World, const FVector& Point, const FVector& Extent) const
{
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
	const ANavigationData* NavData = Nav ? Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) : nullptr;
	FNavLocation Out;
	return NavData && Nav->ProjectPointToNavigation(Point, Out, Extent, NavData) ? Out.NodeRef : 0;
}

bool FDropLinkReach::LandingReachesMain(UWorld& World, const FVector& Landing) const
{
	return bBuilt && Graph.Reaches(NodeOf(FindPoly(World, Landing)));
}

bool FDropLinkReach::Build(UWorld& World)
{
	const double StartTime = FPlatformTime::Seconds();
	bBuilt = false;
	AnchorCount = 0;
	Index.Reset();
	GroundPolys.Reset();
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
	const ARecastNavMesh* Recast = Nav ? Cast<ARecastNavMesh>(Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)) : nullptr;
	if (!Recast)
	{
		Graph.Reset(0);
		return false;
	}
	// The nodes: every ground poly of every tile, then the link polys their links lead to (found while walking the links).
	TArray<NavNodeRef> Nodes;
	TArray<FNavPoly> Polys;
	TArray<FNavTileRef> Tiles;
	Recast->GetAllNavMeshTiles(Tiles);
	for (const FNavTileRef Tile : Tiles)
	{
		Polys.Reset();
		if (Recast->GetPolysInTile(Tile, Polys))
		{
			for (const FNavPoly& Poly : Polys)
			{
				if (!Index.Contains(Poly.Ref))
				{
					Index.Add(Poly.Ref, Nodes.Add(Poly.Ref));
					GroundPolys.Add(Poly);
				}
			}
		}
	}
	auto IsNull = [Recast](NavNodeRef Poly)
	{
		uint16 PolyFlags = 0;
		uint16 AreaFlags = 0;
		return !Recast->GetPolyFlags(Poly, PolyFlags, AreaFlags) || AreaFlags == 0; // NavArea_Null (a switched-off link) has no area flags
	};
	// A nav link poly's ends (Detour's start / end point) and whether it runs both ways: Detour links a one-way link's polys both ways (the
	// direction lives in the link's flags, the pathfinder reads it), so the edges follow the link's own direction here.
	struct FLinkInfo
	{
		FVector Start = FVector::ZeroVector;
		FVector End = FVector::ZeroVector;
		bool bBothWays = true;
	};
	auto GetLinkInfo = [Recast](NavNodeRef Poly, FLinkInfo& Out)
	{
#if WITH_RECAST
		const dtNavMesh* Mesh = Recast->GetRecastMesh();
		const dtOffMeshConnection* Con = Mesh ? Mesh->getOffMeshConnectionByRef(static_cast<dtPolyRef>(Poly)) : nullptr;
		if (!Con)
		{
			return false;
		}
		Out.Start = FVector(-Con->pos[0], -Con->pos[2], Con->pos[1]); // Recast -> Unreal (Recast2UnrealPoint)
		Out.End = FVector(-Con->pos[3], -Con->pos[5], Con->pos[4]);
		Out.bBothWays = Con->getBiDirectional();
		return true;
#else
		return false;
#endif
	};
	// Is ground poly Ground on the link's start side (nearer its start point than its end point)?
	auto OnStartSide = [Recast](NavNodeRef Ground, const FLinkInfo& Link)
	{
		FVector AtStart;
		FVector AtEnd;
		if (!Recast->GetClosestPointOnPoly(Ground, Link.Start, AtStart) || !Recast->GetClosestPointOnPoly(Ground, Link.End, AtEnd))
		{
			return true;
		}
		return FVector::DistSquared(AtStart, Link.Start) <= FVector::DistSquared(AtEnd, Link.End);
	};
	TArray<TPair<int32, int32>> Edges;
	TArray<NavNodeRef> Neighbors;
	FLinkInfo Link;
	for (int32 Node = 0; Node < Nodes.Num(); ++Node) // Nodes grows with the link polys while walking
	{
		const NavNodeRef Poly = Nodes[Node];
		if (IsNull(Poly))
		{
			continue;
		}
		const bool bPolyIsLink = GetLinkInfo(Poly, Link);
		Neighbors.Reset();
		Recast->GetPolyNeighbors(Poly, Neighbors);
		for (const NavNodeRef Next : Neighbors)
		{
			if (Next == 0 || IsNull(Next))
			{
				continue;
			}
			int32 NextIndex = NodeOf(Next);
			if (NextIndex == INDEX_NONE)
			{
				NextIndex = Nodes.Add(Next);
				Index.Add(Next, NextIndex);
			}
			// ground -> link: only from its start side (or both ways); link -> ground: only to its end side (or both ways).
			FLinkInfo NextLink;
			if (bPolyIsLink && !Link.bBothWays && OnStartSide(Next, Link))
			{
				continue;
			}
			if (!bPolyIsLink && GetLinkInfo(Next, NextLink) && !NextLink.bBothWays && !OnStartSide(Poly, NextLink))
			{
				continue;
			}
			Edges.Emplace(Node, NextIndex);
		}
	}
	Graph.Reset(Nodes.Num());
	for (const TPair<int32, int32>& Edge : Edges)
	{
		Graph.AddEdge(Edge.Key, Edge.Value);
	}
	// The anchors: the player starts (the squad's spawn); a map without one: the operatives where they stand.
	TArray<int32> Anchors;
	for (TActorIterator<APlayerStart> It(&World); It; ++It)
	{
		Anchors.AddUnique(NodeOf(FindPoly(World, It->GetActorLocation(), FVector(200.f, 200.f, 300.f))));
	}
	Anchors.Remove(INDEX_NONE);
	if (Anchors.IsEmpty())
	{
		for (TActorIterator<AOperativeCharacter> It(&World); It; ++It)
		{
			Anchors.AddUnique(NodeOf(FindPoly(World, It->GetActorLocation(), FVector(100.f, 100.f, 200.f))));
		}
		Anchors.Remove(INDEX_NONE);
	}
	AnchorCount = Anchors.Num();
	Graph.Solve(Anchors);
	bBuilt = AnchorCount > 0;
	BuildMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
	return bBuilt;
}

bool FDropLinkReach::FindRescuePoint(UWorld& World, const FVector& Feet, FVector& OutPoint) const
{
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
	const ARecastNavMesh* Recast = Nav ? Cast<ARecastNavMesh>(Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)) : nullptr;
	if (!bBuilt || !Recast)
	{
		return false;
	}
	const FallRules::FFallConfig& Config = FallRules::GetConfig();
	// The closest point of every reaching ground poly near him (a poly centre can lie metres off his spot on a big slab).
	TArray<FallRules::FRescueCandidate> Candidates;
	TArray<NavNodeRef> Refs;
	const double SearchSq = FMath::Square(static_cast<double>(Config.RescueMaxCm) + 1500.0);
	for (const FNavPoly& Poly : GroundPolys)
	{
		if (FVector::DistSquared(Poly.Center, Feet) > SearchSq || !Graph.Reaches(NodeOf(Poly.Ref)))
		{
			continue;
		}
		FVector Closest;
		if (Recast->GetClosestPointOnPoly(Poly.Ref, Feet, Closest))
		{
			Candidates.Add({ Closest, true });
			Refs.Add(Poly.Ref);
		}
	}
	const int32 Best = FallRules::PickRescuePoint(Feet, Candidates, Config);
	if (Best == INDEX_NONE)
	{
		return false;
	}
	OutPoint = Candidates[Best].Point;
	return true;
}
