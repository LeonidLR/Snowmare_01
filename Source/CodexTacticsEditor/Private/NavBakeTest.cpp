#include "Misc/AutomationTest.h"
#include "Builders/CubeBuilder.h"
#include "BSPOps.h"
#include "Components/BrushComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/Polys.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "NavBake.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"

#if WITH_DEV_AUTOMATION_TESTS

// Headless navmesh bake (CodexNavBake::BuildWorldNavigation, used by -run=CodexTacticsEditor.CodexBuildNav): a fresh editor-mode world with a
// floor and a NavMeshBoundsVolume must come out with a built Default navmesh even though the editor's AsyncLoadLock
// (0x20) is held - the lock that made ResavePackages -BuildNavigationData and RebuildNavigation in commandlets a no-op.

namespace
{
	ANavMeshBoundsVolume* SpawnBoundsVolume(UWorld& World, const FVector& Center, const FVector& Size)
	{
		ANavMeshBoundsVolume* Volume = World.SpawnActor<ANavMeshBoundsVolume>(Center, FRotator::ZeroRotator);
		if (!Volume)
		{
			return nullptr;
		}
		Volume->PreEditChange(nullptr);
		Volume->PolyFlags = 0;
		Volume->Brush = NewObject<UModel>(Volume, NAME_None, RF_Transactional);
		Volume->Brush->Initialize(nullptr, true);
		Volume->Brush->Polys = NewObject<UPolys>(Volume->Brush, NAME_None, RF_Transactional);
		Volume->GetBrushComponent()->Brush = Volume->Brush;
		UCubeBuilder* Builder = NewObject<UCubeBuilder>(Volume, NAME_None, RF_Transactional);
		Builder->X = Size.X;
		Builder->Y = Size.Y;
		Builder->Z = Size.Z;
		Volume->BrushBuilder = Builder;
		Builder->Build(&World, Volume);
		FBSPOps::csgPrepMovingBrush(Volume);
		Volume->PostEditChange();
		return Volume;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavBakeBuildsTilesTest, "CodexTactics.Editor.Navigation.BakeBuildsTiles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNavBakeBuildsTilesTest::RunTest(const FString&)
{
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("engine cube mesh"), Cube))
	{
		return false;
	}

	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, TEXT("CodexNavBakeTest"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Editor);
	Context.SetCurrentWorld(World);

	// 40 m x 40 m floor (cube is 1 m) with its top at z = 0, a 60 m bounds volume around it.
	AStaticMeshActor* Floor = World->SpawnActor<AStaticMeshActor>(FVector(0.f, 0.f, -50.f), FRotator::ZeroRotator);
	Floor->GetStaticMeshComponent()->SetStaticMesh(Cube);
	Floor->SetActorScale3D(FVector(40.f, 40.f, 1.f));
	SpawnBoundsVolume(*World, FVector(0.f, 0.f, 0.f), FVector(6000.f, 6000.f, 1000.f));

	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (Nav)
	{
		// What a commandlet world is stuck with: the editor's async-load lock that nothing removes.
		Nav->AddNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock);
	}

	const FCodexNavBakeResult Result = CodexNavBake::BuildWorldNavigation(*World);
	TestTrue(FString::Printf(TEXT("built (%s)"), *Result.Error), Result.bBuilt);
	TestTrue(TEXT("Default agent has active tiles: ") + Result.Describe(), Result.GetActiveTiles(TEXT("Default")) > 0);
	Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	TestTrue(TEXT("AsyncLoadLock released"), Nav && !Nav->IsNavigationBuildingLocked(ENavigationBuildLock::AsyncLoadLock));

	// A second bake of an already-baked world rebuilds the existing navmesh (no duplicate actors, still tiles).
	const FCodexNavBakeResult Again = CodexNavBake::BuildWorldNavigation(*World);
	TestTrue(TEXT("re-bake: Default agent still has active tiles: ") + Again.Describe(), Again.GetActiveTiles(TEXT("Default")) > 0);
	TestEqual(TEXT("re-bake: same navigation data actors"), Again.Agents.Num(), Result.Agents.Num());

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavBakeNoBoundsTest, "CodexTactics.Editor.Navigation.BakeWithoutBoundsFails",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNavBakeNoBoundsTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, TEXT("CodexNavBakeNoBounds"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Editor);
	Context.SetCurrentWorld(World);

	const FCodexNavBakeResult Result = CodexNavBake::BuildWorldNavigation(*World);
	TestFalse(TEXT("nothing built without bounds"), Result.bBuilt);
	TestFalse(TEXT("error reported"), Result.Error.IsEmpty());
	TestEqual(TEXT("no tiles"), Result.GetActiveTiles(TEXT("Default")), 0);

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif
