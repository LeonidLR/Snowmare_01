// CodexTactics.Combat.VisionCone.Layer.* - bug #38 (user 2026-11-04, L_Gen_Depot_1: the 120 m cones were slow, stair-stepped, stacked three
// translucent layers and shot thin shards across the scene from a roof) and the weapon zone of bug #39. UE-only, no Godot reference.
// Pure geometry: world-anchored adaptive base rays, the fair refinement budget, the dropped no-ground rays, no slanted / upward triangles
// between two ground levels, the slivers, the merged layer (every point drawn once at the best level), the weapon zone band / edge.

#include "Combat/VisionConeVisibility.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace VisionConeLayerTest
{
	FVisionConeSightSpec OpenSpec(float RangeCm, float HalfAngleDeg = 45.f)
	{
		FVisionConeSightSpec Spec = VisionConeVisibility::MakeOperativeSightSpec(160.f, RangeCm, 2);
		Spec.HalfAngleDeg = HalfAngleDeg;
		return Spec;
	}

	/** Open ground: nothing in the way. */
	void OpenSample(float, FVisionConeRay&)
	{
	}

	FVector2D Direction(float AngleDeg)
	{
		return FVector2D(FMath::Cos(FMath::DegreesToRadians(AngleDeg)), FMath::Sin(FMath::DegreesToRadians(AngleDeg)));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisionConeLayerRaysTest, "CodexTactics.Combat.VisionCone.Layer.Rays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVisionConeLayerRaysTest::RunTest(const FString&)
{
	using namespace VisionConeLayerTest;
	// Adaptive step: a 120 m cone ~0.95 deg (2 m arcs), a 18 m peek cone the widest step.
	FVisionConeSightSpec Long = OpenSpec(12000.f);
	Long.RaySpacingCm = 200.f;
	Long.MinRayStepDeg = 0.5f;
	Long.RayStepDeg = 1.5f;
	TestTrue(TEXT("120 m: ~0.95 deg"), FMath::IsNearlyEqual(Long.GetBaseStepDeg(), FMath::RadiansToDegrees(200.f / 12000.f), 0.001f));
	FVisionConeSightSpec Short = Long;
	Short.Levels[0].RangeCm = Short.Levels[1].RangeCm = 1800.f;
	TestEqual(TEXT("18 m: the widest step 1.5 deg"), Short.GetBaseStepDeg(), 1.5f);
	FVisionConeSightSpec Huge = Long;
	Huge.Levels[0].RangeCm = Huge.Levels[1].RangeCm = 50000.f;
	TestEqual(TEXT("500 m: the narrowest step 0.5 deg"), Huge.GetBaseStepDeg(), 0.5f);

	// World-anchored grid: the inner rays sit on world bearings that are multiples of the step; a 0.7 deg turn keeps them.
	const TArray<float> A = VisionConeVisibility::GridAngles(45.f, 1.f, 320, 10.f);
	const TArray<float> B = VisionConeVisibility::GridAngles(45.f, 1.f, 320, 10.7f);
	TestTrue(TEXT("edges included"), A.Num() > 10 && A[0] == -45.f && A.Last() == 45.f && B[0] == -45.f && B.Last() == 45.f);
	bool bOnGrid = true;
	bool bSorted = true;
	for (int32 Index = 1; Index + 1 < B.Num(); ++Index)
	{
		const float World = 10.7f + B[Index];
		bOnGrid &= FMath::IsNearlyEqual(World, FMath::RoundToFloat(World), 0.001f);
		bSorted &= B[Index] > B[Index - 1];
	}
	TestTrue(TEXT("inner rays on whole-degree world bearings, sorted"), bOnGrid && bSorted && B.Last() > B[B.Num() - 2]);
	int32 Shared = 0;
	for (int32 Index = 1; Index + 1 < A.Num(); ++Index)
	{
		Shared += B.ContainsByPredicate([&A, Index](float Angle) { return FMath::IsNearlyEqual(Angle + 10.7f, A[Index] + 10.f, 0.001f); }) ? 1 : 0;
	}
	TestTrue(TEXT("a small turn keeps almost every inner world bearing"), Shared >= A.Num() - 4);
	TestTrue(TEXT("the cap widens the step"), VisionConeVisibility::GridAngles(45.f, 0.1f, 50, 0.f).Num() <= 50);

	// Fair refinement: 20 shadow edges (a picket fence) each get a share - the last edges are refined as finely as the first.
	FVisionConeSightSpec Fence = OpenSpec(3000.f, 40.f);
	Fence.RayStepDeg = 2.f;
	Fence.MaxRays = 160;
	Fence.MaxRefineDepth = 6;
	auto FenceSample = [](float AngleDeg, FVisionConeRay& Ray)
	{
		// A 3 m wall 10 m out wherever floor(angle / 4) is even (posts 4 deg wide every 8 deg).
		if (FMath::FloorToInt((AngleDeg + 40.f) / 4.f) % 2 == 0)
		{
			Ray.Obstacles.Add({ 1000.f, 1000.f, VisionConeVisibility::FullHeightCm });
			Ray.BlockerKey = 7;
		}
	};
	const TArray<FVisionConeRay> Rays = VisionConeVisibility::CastRays(Fence, FenceSample);
	float FirstGap = 0.f;
	float LastGap = 0.f;
	for (int32 Index = 1; Index < Rays.Num(); ++Index)
	{
		if (Rays[Index].BlockerKey != Rays[Index - 1].BlockerKey)
		{
			const float Gap = Rays[Index].AngleDeg - Rays[Index - 1].AngleDeg;
			FirstGap = FirstGap == 0.f ? Gap : FirstGap;
			LastGap = Gap;
		}
	}
	TestTrue(TEXT("the last shadow edge is refined like the first (fair budget)"), FirstGap > 0.f && LastGap <= FirstGap * 2.f + 1.e-3f && Rays.Num() <= Fence.MaxRays);

	// An elevated ray without a measured ground is dropped (no needle at the roof's level).
	FVisionConeSightSpec Perch = OpenSpec(5000.f, 30.f);
	auto PerchSample = [](float AngleDeg, FVisionConeRay& Ray)
	{
		if (FMath::Abs(AngleDeg - 10.f) < 2.f)
		{
			Ray.bNoGround = true;
			return;
		}
		Ray.GroundCm = -900.f;
		Ray.Obstacles.Add({ 0.f, 500.f, 0.f, FVector2D::ZeroVector, FVector2D::ZeroVector, true });
	};
	const TArray<FVisionConeRay> PerchRays = VisionConeVisibility::CastRays(Perch, PerchSample);
	TestTrue(TEXT("no-ground rays dropped"), PerchRays.Num() > 10 && !PerchRays.ContainsByPredicate([](const FVisionConeRay& Ray) { return Ray.bNoGround; }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisionConeLayerShapeTest, "CodexTactics.Combat.VisionCone.Layer.Shape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVisionConeLayerShapeTest::RunTest(const FString&)
{
	using namespace VisionConeLayerTest;
	// Two grounds side by side (a roof's edge along the ray direction): no triangle spans the two levels, nothing above the feet.
	TArray<FVisionConeRay> Rays;
	for (int32 Index = 0; Index <= 20; ++Index)
	{
		FVisionConeRay Ray;
		Ray.AngleDeg = -20.f + 2.f * Index;
		Ray.GroundCm = Index < 10 ? -900.f : (Index == 15 ? 300.f : -300.f);
		Ray.Spans.Add({ 800.f, 6000.f, 2 });
		Rays.Add(Ray);
	}
	const FVisionConeShape Shape = VisionConeVisibility::BuildShape(Rays, 2, 20.f, 8.f, 0.f, 0.5f);
	int32 Slanted = 0;
	int32 Above = 0;
	for (const FVisionConeMesh* Mesh : { &Shape.Full, &Shape.Outline })
	{
		for (int32 Index = 0; Index + 2 < Mesh->Triangles.Num(); Index += 3)
		{
			const float Z0 = Mesh->Vertices[Mesh->Triangles[Index]].Z;
			const float Z1 = Mesh->Vertices[Mesh->Triangles[Index + 1]].Z;
			const float Z2 = Mesh->Vertices[Mesh->Triangles[Index + 2]].Z;
			Slanted += FMath::Max3(Z0, Z1, Z2) - FMath::Min3(Z0, Z1, Z2) > 30.f ? 1 : 0;
			Above += FMath::Max3(Z0, Z1, Z2) > 5.f ? 1 : 0;
		}
	}
	TestTrue(TEXT("two grounds: no slanted triangle, nothing above the feet"), Shape.Full.Triangles.Num() > 0 && Slanted == 0 && Above == 0);

	// Slivers: one ray sees 40 m out through a crack, its neighbours do not.
	TArray<FVisionConeRay> Crack;
	for (int32 Index = 0; Index <= 40; ++Index)
	{
		FVisionConeRay Ray;
		Ray.AngleDeg = -10.f + 0.5f * Index;
		Ray.Spans.Add({ 0.f, 1000.f, 2 });
		if (Index == 20)
		{
			Ray.Spans.Add({ 3000.f, 5000.f, 2 });
		}
		Crack.Add(Ray);
	}
	TArray<TArray<bool, TInlineAllocator<4>>> Flags;
	TestEqual(TEXT("the crack is the one sliver (the near fan is wide)"), VisionConeVisibility::FindSlivers(Crack, 60.f, 1500.f, &Flags), 1);
	TestTrue(TEXT("flagged on its ray"), Flags[20].Num() == 2 && Flags[20][1] && !Flags[20][0]);

	// The weapon zone: an open 80 m fan, M16 28 / 60 m - band vertices between 28 and 60 m, the edge round 28 m.
	FVisionConeSightSpec Spec = OpenSpec(8000.f, 30.f);
	const TArray<FVisionConeRay> Open = VisionConeVisibility::CastRays(Spec, &OpenSample);
	FVisionConeShapeOptions Options;
	Options.Weapon.EffectiveCm = 2800.f;
	Options.Weapon.MaxCm = 6000.f;
	Options.Weapon.EdgeWidthCm = 20.f;
	const FVisionConeShape Zone = VisionConeVisibility::BuildShape(Open, 2, 30.f, Options);
	bool bBand = Zone.WeaponBand.Vertices.Num() > 0;
	for (const FVector& Vertex : Zone.WeaponBand.Vertices)
	{
		const float Distance = FVector2D(Vertex.X, Vertex.Y).Size();
		bBand &= Distance >= 2790.f && Distance <= 6001.f;
	}
	bool bEdge = Zone.WeaponEdge.Vertices.Num() > 0;
	for (const FVector& Vertex : Zone.WeaponEdge.Vertices)
	{
		const float Distance = FVector2D(Vertex.X, Vertex.Y).Size();
		bEdge &= Distance >= 2780.f && Distance <= 2811.f;
	}
	TestTrue(TEXT("weapon band from the effective range to the max reach"), bBand);
	TestTrue(TEXT("weapon edge along the effective range"), bEdge);
	TestTrue(TEXT("no zone without a weapon"), VisionConeVisibility::BuildShape(Open, 2, 30.f, FVisionConeShapeOptions()).WeaponBand.Vertices.Num() == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisionConeLayerMergeTest, "CodexTactics.Combat.VisionCone.Layer.Merge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVisionConeLayerMergeTest::RunTest(const FString&)
{
	using namespace VisionConeLayerTest;
	// Three squad cones 2 m apart, 40 m, looking roughly the same way; one of them sees only the light tone beyond 20 m.
	struct FCone
	{
		FVector2D Origin;
		float Yaw;
		int32 Priority;
		bool bLightFar;
	};
	const FCone Cones[] = { { FVector2D(0.f, 0.f), 0.f, 1, false }, { FVector2D(-150.f, 200.f), 10.f, 2, true }, { FVector2D(-150.f, -200.f), -15.f, 3, false } };
	TArray<TSharedPtr<TArray<FVisionConeRay>, ESPMode::ThreadSafe>> Sources;
	for (const FCone& Cone : Cones)
	{
		const bool bLightFar = Cone.bLightFar;
		FVisionConeSightSpec Spec = OpenSpec(4000.f, 40.f);
		TArray<FVisionConeRay> Rays = VisionConeVisibility::CastRays(Spec, &OpenSample);
		if (bLightFar)
		{
			for (FVisionConeRay& Ray : Rays)
			{
				Ray.Spans = { { 0.f, 2000.f, 2 }, { 2000.f, 4000.f, 1 } };
			}
		}
		Sources.Add(MakeShared<TArray<FVisionConeRay>, ESPMode::ThreadSafe>(MoveTemp(Rays)));
	}
	auto Member = [&Cones, &Sources](int32 Index)
	{
		FVisionConeLayerMember Out;
		Out.Rays = Sources[Index];
		Out.HalfAngleDeg = 40.f;
		Out.MaxRangeCm = 4000.f;
		Out.Origin = Cones[Index].Origin;
		Out.YawDeg = Cones[Index].Yaw;
		Out.Priority = Cones[Index].Priority;
		return Out;
	};
	TArray<TArray<FVisionConeRay>> Drawn;
	int32 Cut = 0;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Cones); ++Index)
	{
		TArray<FVisionConeLayerMember> Others;
		for (int32 Other = 0; Other < UE_ARRAY_COUNT(Cones); ++Other)
		{
			if (Other != Index)
			{
				Others.Add(Member(Other));
			}
		}
		TArray<FVisionConeRay> Rays = *Sources[Index];
		Cut += VisionConeVisibility::ClipToLayer(Rays, Cones[Index].Origin, Cones[Index].Yaw, Cones[Index].Priority, Others, 100.f);
		Drawn.Add(MoveTemp(Rays));
	}
	TestTrue(TEXT("the overlapping cones were cut"), Cut > 0);
	// A 50 cm grid: covered by any source <=> drawn by exactly one cone (bar the seams), at the best level any source shows.
	int32 Points = 0;
	int32 Double = 0;
	int32 Missing = 0;
	int32 WrongLevel = 0;
	for (float X = -500.f; X <= 4200.f; X += 50.f)
	{
		for (float Y = -3000.f; Y <= 3000.f; Y += 50.f)
		{
			const FVector2D Point(X, Y);
			uint8 Best = 0;
			int32 DrawnBy = 0;
			uint8 DrawnLevel = 0;
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(Cones); ++Index)
			{
				const FVisionConeLayerMember Source = Member(Index);
				Best = FMath::Max(Best, Source.LevelAt(Point));
				FVisionConeLayerMember Clipped = Source;
				Clipped.Rays = MakeShared<TArray<FVisionConeRay>, ESPMode::ThreadSafe>(Drawn[Index]);
				if (const uint8 Level = Clipped.LevelAt(Point))
				{
					++DrawnBy;
					DrawnLevel = FMath::Max(DrawnLevel, Level);
				}
			}
			if (Best == 0)
			{
				continue;
			}
			++Points;
			Double += DrawnBy > 1 ? 1 : 0;
			Missing += DrawnBy == 0 ? 1 : 0;
			WrongLevel += DrawnBy == 1 && DrawnLevel != Best ? 1 : 0;
		}
	}
	const FString Summary = FString::Printf(TEXT("%d points: %d drawn twice, %d missing, %d at a lower level"), Points, Double, Missing, WrongLevel);
	TestTrue(*FString::Printf(TEXT("one layer (<= 1 %% seams): %s"), *Summary), Points > 1000 && Double <= Points / 100 && Missing <= Points / 100 && WrongLevel <= Points / 100);
	// Alone: nothing to cut.
	TArray<FVisionConeRay> Alone = *Sources[0];
	TestEqual(TEXT("no other member: nothing cut"), VisionConeVisibility::ClipToLayer(Alone, FVector2D::ZeroVector, 0.f, 1, {}, 100.f), 0);
	return true;
}

#endif
