#include "Misc/AutomationTest.h"
#include "Interactables/VisualMeshRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// 2026-10-21 (the burnt BTR-82 of the Demo 01 siphon quest): an art mesh on an interactable / a blast gate leaf keeps its own
// proportions; the placeholder cube still fills the box.

#define VISUAL_MESH_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Interactables.VisualMesh." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

VISUAL_MESH_TEST(FVisualMeshPlaceholderTest, "PlaceholderFillsTheBox")
bool FVisualMeshPlaceholderTest::RunTest(const FString&)
{
	const FTransform Art(FRotator(0.f, 30.f, 0.f), FVector(-63.f, -13.f, -157.f), FVector(1.f));
	const FTransform Cube = VisualMeshRules::ResolveTransform(false, FVector(780.f, 350.f, 324.f), Art);
	TestTrue(TEXT("the cube is scaled to the box (100 cm cube)"), Cube.GetScale3D().Equals(FVector(7.8f, 3.5f, 3.24f), 1e-4f));
	TestTrue(TEXT("centred, unrotated (the art transform is ignored)"), Cube.GetLocation().IsNearlyZero() && Cube.GetRotation().Equals(FQuat::Identity));
	// The old InteractableActor rule: scale = box extent / 50.
	const FVector Extent(25.f, 20.f, 35.f);
	TestTrue(TEXT("same as extent / 50"), VisualMeshRules::ResolveTransform(false, Extent * 2.f, FTransform::Identity).GetScale3D().Equals(Extent / 50.f, 1e-4f));
	return true;
}

VISUAL_MESH_TEST(FVisualMeshArtTest, "ArtMeshIsNeverStretched")
bool FVisualMeshArtTest::RunTest(const FString&)
{
	const FTransform Art(FRotator(0.f, 30.f, 0.f), FVector(-63.f, -13.f, -157.f), FVector(1.f));
	for (const FVector& Box : { FVector(100.f), FVector(780.f, 350.f, 324.f), FVector(40.f, 350.f, 400.f) })
	{
		const FTransform Resolved = VisualMeshRules::ResolveTransform(true, Box, Art);
		TestTrue(TEXT("the authored transform, whatever the box"), Resolved.Equals(Art));
	}
	return true;
}

VISUAL_MESH_TEST(FVisualMeshMirrorTest, "RightLeafMirrorsTheLeft")
bool FVisualMeshMirrorTest::RunTest(const FString&)
{
	const FTransform Left(FRotator(5.f, 20.f, 10.f), FVector(12.f, -40.f, -200.f), FVector(1.f, 1.2f, 0.9f));
	const FTransform Right = VisualMeshRules::MirrorAcrossY(Left);
	for (const FVector& Vertex : { FVector(0.f), FVector(100.f, 0.f, 0.f), FVector(0.f, 175.f, 0.f), FVector(-20.f, 60.f, 300.f) })
	{
		FVector Expected = Left.TransformPosition(Vertex);
		Expected.Y = -Expected.Y;
		FVector MirroredVertex = Vertex;
		MirroredVertex.Y = -MirroredVertex.Y;
		// The right leaf shows the mirror image: the left leaf's point of a vertex, reflected across the centre line.
		TestTrue(FString::Printf(TEXT("vertex %s mirrored"), *Vertex.ToString()), Right.TransformPosition(Vertex).Equals(Expected, 0.01f));
	}
	TestTrue(TEXT("mirroring twice gives the left leaf back"), VisualMeshRules::MirrorAcrossY(Right).Equals(Left, 1e-3f));
	return true;
}

#endif
