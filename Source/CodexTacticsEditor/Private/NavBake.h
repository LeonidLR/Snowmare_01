#pragma once

#include "CoreMinimal.h"

class UWorld;

/** One navigation data actor after a bake: its agent and how many tiles carry data. */
struct FCodexNavBakeAgent
{
	FString AgentName;
	FString ActorName;
	int32 ActiveTiles = 0;
};

/** Outcome of CodexNavBake::BuildWorldNavigation. */
struct FCodexNavBakeResult
{
	/** True when UNavigationSystemV1::Build ran to completion. */
	bool bBuilt = false;
	/** True when the editor's AsyncLoadLock (0x20) was held and had to be released. */
	bool bReleasedAsyncLoadLock = false;
	/** RecastNavMesh actors destroyed before the build because their origin was not at the world origin. */
	int32 RemovedMisalignedNavData = 0;
	TArray<FCodexNavBakeAgent> Agents;
	/** Empty on success. */
	FString Error;

	/** Active tiles of the given agent's navmesh, 0 when it has none. */
	int32 GetActiveTiles(const FString& AgentName) const;
	/** One-line summary for logs ("Default=123 Vehicle=45"). */
	FString Describe() const;
};

/**
 * Headless navmesh bake. No Godot reference: Unreal-only tooling (docs/port/TRAPS.md, navmesh rows).
 *
 * Why it exists: in an editor-mode world UNavigationSystemV1::DoInitialSetup adds ENavigationBuildLock::AsyncLoadLock
 * (bWaitForAsyncLoadingBeforeBuildingNavigationAutomatically, default true) and only a core-ticker callback (16 ticks +
 * 2 s idle) removes it. Commandlets never tick that ticker, so Build() bails ("navigation build is locked (flags: 0x20)")
 * and every commandlet save leaves an empty RecastNavMesh behind.
 */
namespace CodexNavBake
{
	/**
	 * Builds all navigation data of an editor world synchronously: finishes asset compilation, makes sure an editor-mode
	 * navigation system exists, releases the AsyncLoadLock / InitialLock, destroys RecastNavMesh actors whose origin is not
	 * tile-aligned (they would be recreated empty at runtime), then UNavigationSystemV1::Build (spawns missing nav data,
	 * blocks until done). Does not save anything.
	 */
	FCodexNavBakeResult BuildWorldNavigation(UWorld& World);
}
