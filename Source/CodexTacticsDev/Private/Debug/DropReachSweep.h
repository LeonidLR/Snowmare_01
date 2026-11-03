#pragma once

// Shared sweep of the drop smokes (DropSoftlockSmoke, DropLinkReachSmoke; user bug 2026-11-03, the roof-hole soft-lock - docs/systems/
// stances-and-movement.md "Falls and drops", "The soft-lock"): every generated drop link (UDropLinkSubsystem) and every one-way nav link of a
// NavLinkProxy (the level generator's hole links) must land where the main navmesh is reachable (FDropLinkReach). Header-only, inline (unity
// builds: never copy these into two .cpp files).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Falls/DropLinkReach.h"
#include "Falls/DropLinkSubsystem.h"
#include "GameFramework/PlayerStart.h"
#include "Navigation/NavLinkProxy.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"

namespace DropReachSweep
{
	struct FResult
	{
		bool bBuilt = false;
		int32 DropLinks = 0;
		int32 HoleLinks = 0;
		int32 Bad = 0;
		/** The report line (the first dead-end links listed). */
		FString Line;
	};

	inline FResult Run(UWorld& World)
	{
		FResult Result;
		TArray<FDropLinkReach::FLinkEnds> Ends;
		if (const UDropLinkSubsystem* Links = World.GetSubsystem<UDropLinkSubsystem>())
		{
			for (int32 Index = 0; Index < Links->GetLinks().Num(); ++Index)
			{
				const FDropLink& Link = Links->GetLinks()[Index];
				Ends.Add({ Link.Start, Link.End, FString::Printf(TEXT("drop %d"), Index) });
			}
			Result.DropLinks = Links->GetLinks().Num();
		}
		for (TActorIterator<ANavLinkProxy> It(&World); It; ++It)
		{
			const FTransform& Transform = It->GetActorTransform();
			for (const FNavigationLink& Point : It->PointLinks)
			{
				// Only a one-way link can strand a man (a two-way one walks back the way he came).
				if (Point.Direction == ENavLinkDirection::BothWays)
				{
					continue;
				}
				const bool bLeftToRight = Point.Direction == ENavLinkDirection::LeftToRight;
				Ends.Add({ Transform.TransformPosition(bLeftToRight ? Point.Left : Point.Right), Transform.TransformPosition(bLeftToRight ? Point.Right : Point.Left),
					It->GetActorNameOrLabel() });
				++Result.HoleLinks;
			}
		}
		FDropLinkReach Reach;
		Result.bBuilt = Reach.Build(World);
		FString BadList;
		for (const FDropLinkReach::FLinkEnds& Link : Ends)
		{
			if (!Reach.LandingReachesMain(World, Link.End))
			{
				++Result.Bad;
				if (Result.Bad <= 12)
				{
					// A second opinion from the pathfinder: the landing's path to the player start (partial = a dead end indeed).
					FString Path = TEXT("no player start");
					for (TActorIterator<APlayerStart> Start(&World); Start; ++Start)
					{
						const UNavigationPath* Nav = UNavigationSystemV1::FindPathToLocationSynchronously(&World, Link.End, Start->GetActorLocation());
						Path = !Nav || !Nav->IsValid() ? TEXT("no path") : Nav->IsPartial() ? TEXT("path partial") : TEXT("PATH COMPLETE");
						break;
					}
					BadList += FString::Printf(TEXT(" [%s %s -> %s: %s]"), *Link.Name, *Link.Start.ToCompactString(), *Link.End.ToCompactString(), *Path);
				}
			}
		}
		Result.Line = FString::Printf(TEXT("%s: %d drop links + %d one-way hole links checked (reach graph %s in %.1f ms: %d polys, %d reach the main navmesh from %d anchors): %d land in a dead end%s"),
			*World.GetMapName(), Result.DropLinks, Result.HoleLinks, Result.bBuilt ? TEXT("built") : TEXT("NOT BUILT"), Reach.GetBuildMs(), Reach.GetPolyCount(),
			Reach.GetReachingCount(), Reach.GetAnchorCount(), Result.Bad, *BadList);
		return Result;
	}
}

#endif // !UE_BUILD_SHIPPING
