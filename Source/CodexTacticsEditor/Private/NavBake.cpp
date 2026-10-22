#include "NavBake.h"

#include "AssetCompilingManager.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"

int32 FCodexNavBakeResult::GetActiveTiles(const FString& AgentName) const
{
	int32 Tiles = 0;
	for (const FCodexNavBakeAgent& Agent : Agents)
	{
		if (Agent.AgentName == AgentName)
		{
			Tiles += Agent.ActiveTiles;
		}
	}
	return Tiles;
}

FString FCodexNavBakeResult::Describe() const
{
	TArray<FString> Parts;
	for (const FCodexNavBakeAgent& Agent : Agents)
	{
		Parts.Add(FString::Printf(TEXT("%s=%d"), *Agent.AgentName, Agent.ActiveTiles));
	}
	return Parts.Num() ? FString::Join(Parts, TEXT(" ")) : FString(TEXT("(no navigation data)"));
}

FCodexNavBakeResult CodexNavBake::BuildWorldNavigation(UWorld& World)
{
	FCodexNavBakeResult Result;

	// Generation reads collision from static meshes: they must be compiled first (ResavePackages does the same).
	FAssetCompilingManager::Get().FinishAllCompilation();

	// Like ResavePackages -BuildNavigationData: (re)initialize the navigation system in EditorMode even when the world
	// already has one. That registers the RecastNavMesh actors loaded with the map - without it Build() finds no
	// navigation data to rebuild on an already-baked map and leaves it with 0 tiles. It also adds the AsyncLoadLock.
	FNavigationSystem::AddNavigationSystemToWorld(World, FNavigationSystemRunMode::EditorMode);
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
	if (!Nav)
	{
		Result.Error = TEXT("the world has no UNavigationSystemV1 (WorldSettings navigation config?)");
		return Result;
	}
	if (!Nav->IsThereAnywhereToBuildNavigation())
	{
		Result.Error = TEXT("nothing to build: no NavMeshBoundsVolume in the world");
		return Result;
	}

	// AsyncLoadLock is only lifted by a core ticker that commandlets never run; InitialLock waits for streaming that a
	// bake does not do. NoRebuild: Build() below is the rebuild. NoUpdateInEditor is ignored by Build().
	Result.bReleasedAsyncLoadLock = Nav->IsNavigationBuildingLocked(ENavigationBuildLock::AsyncLoadLock);
	Nav->RemoveNavigationBuildLock(static_cast<uint8>(ENavigationBuildLock::AsyncLoadLock | ENavigationBuildLock::InitialLock),
		UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);

	// A saved navmesh whose origin (the actor location) is not tile-aligned is recreated empty at load (TRAPS.md).
	// Build() respawns missing navigation data at the origin.
	TArray<ARecastNavMesh*> Misaligned;
	for (TActorIterator<ARecastNavMesh> It(&World); It; ++It)
	{
		if (!It->GetActorLocation().IsNearlyZero() || !It->GetActorRotation().IsNearlyZero())
		{
			Misaligned.Add(*It);
		}
	}
	for (ARecastNavMesh* NavMesh : Misaligned)
	{
		NavMesh->CleanUpAndMarkPendingKill();
	}
	Result.RemovedMisalignedNavData = Misaligned.Num();

	Nav->Build();

	if (Nav->IsNavigationBuildingLocked(static_cast<uint8>(~ENavigationBuildLock::NoUpdateInEditor)))
	{
		Result.Error = TEXT("navigation build is still locked");
		return Result;
	}
	Result.bBuilt = true;

	for (TActorIterator<ARecastNavMesh> It(&World); It; ++It)
	{
		if (!IsValid(*It))
		{
			continue;
		}
		FCodexNavBakeAgent& Agent = Result.Agents.AddDefaulted_GetRef();
		Agent.AgentName = It->GetConfig().Name.ToString();
		Agent.ActorName = It->GetName();
		Agent.ActiveTiles = It->GetNumActiveTiles();
	}
	return Result;
}
