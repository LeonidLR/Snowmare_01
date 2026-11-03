// CodexTactics.Combat.VisionCone.Elevated.* - vision cones of an elevated observer (game designer bug 09, 2026-11-01, L_FormationLabCombat:
// "from the tower he sees everything behind the wall"; UE-only, no Godot reference). Pure 2.5D geometry: the wall shadow and the perch's
// dead zone formulas, the elevated ground decision, a ray classified from a 4 m tower over a 3 m wall, and a whole cone from the tower
// whose drawn level agrees with an exact 3D line test against box obstacles (the tower itself one of them); a ground-level observer keeps
// the flat model.

#include "Combat/SightRules.h"
#include "Combat/VisionConeVisibility.h"
#include "Misc/AutomationTest.h"

namespace VisionConeElevatedTest
{
	/** A box obstacle of the cone frame: planar footprint + top height above the GROUND (not the observer's feet). */
	struct FTowerGroundBox
	{
		FBox2D Box;
		float TopCm = 0.f;
	};

	/**
	 * Segment [0, MaxCm] along Dir against a box footprint (slab method): entry / exit distance, clamped to the segment, and the entry /
	 * exit faces' normals (what the component's front / back face traces report: a corner turns them and is refined).
	 */
	bool SegmentCrossing(const FVector2D& Dir, float MaxCm, const FBox2D& Box, float& OutNear, float& OutFar,
		FVector2D* OutNearNormal = nullptr, FVector2D* OutFarNormal = nullptr)
	{
		float Near = 0.f;
		float Far = MaxCm;
		FVector2D NearNormal = FVector2D::ZeroVector;
		FVector2D FarNormal = FVector2D::ZeroVector;
		for (int32 Axis = 0; Axis < 2; ++Axis)
		{
			const float D = Axis == 0 ? Dir.X : Dir.Y;
			const float Min = Axis == 0 ? Box.Min.X : Box.Min.Y;
			const float Max = Axis == 0 ? Box.Max.X : Box.Max.Y;
			if (FMath::Abs(D) < 1.e-8f)
			{
				if (0.f < Min || 0.f > Max)
				{
					return false;
				}
				continue;
			}
			float T0 = Min / D;
			float T1 = Max / D;
			float Sign = -1.f;
			if (T0 > T1)
			{
				Swap(T0, T1);
				Sign = 1.f;
			}
			if (T0 > Near)
			{
				Near = T0;
				NearNormal = Axis == 0 ? FVector2D(Sign, 0.f) : FVector2D(0.f, Sign);
			}
			if (T1 < Far)
			{
				Far = T1;
				FarNormal = Axis == 0 ? FVector2D(-Sign, 0.f) : FVector2D(0.f, -Sign);
			}
			if (Near > Far)
			{
				return false;
			}
		}
		OutNear = Near;
		OutFar = Far;
		if (OutNearNormal)
		{
			*OutNearNormal = NearNormal;
		}
		if (OutFarNormal)
		{
			*OutFarNormal = FarNormal;
		}
		return true;
	}

	/** The scene: an observer on a 3 x 3 m, 4 m tower (feet on its roof at the origin), a 3 m wall, a 1.2 m crate, a 6 m block. */
	struct FTowerScene
	{
		float RoofCm = 400.f;
		FTowerGroundBox Tower{ FBox2D(FVector2D(-150.f, -150.f), FVector2D(150.f, 150.f)), 400.f };
		TArray<FTowerGroundBox> Boxes;

		FTowerScene()
		{
			Boxes.Add({ FBox2D(FVector2D(1000.f, -600.f), FVector2D(1040.f, 300.f)), 300.f });  // the lab wall, 10 m out
			Boxes.Add({ FBox2D(FVector2D(600.f, 500.f), FVector2D(680.f, 620.f)), 120.f });    // a crate
			Boxes.Add({ FBox2D(FVector2D(1400.f, 700.f), FVector2D(1500.f, 900.f)), 600.f });  // a block taller than the eye
		}
	};

	/**
	 * The component's sampler on the scene (UVisionConeComponent SamplePerch + SampleRay): the tower's edge along the ray and the ground
	 * below it (GroundCm -RoofCm, the perch obstacle 0 .. edge hiding what stands under it), then the boxes as full walls (top at least
	 * the high trace) or low obstacles (above the low trace), heights from the observer's feet.
	 */
	void SampleTowerScene(const FVisionConeSightSpec& Spec, const FTowerScene& Scene, bool bElevated, float AngleDeg, FVisionConeRay& Ray,
		int32 MaxLow = -1, bool bLostPerch = false)
	{
		const float Radians = FMath::DegreesToRadians(AngleDeg);
		const FVector2D Dir(FMath::Cos(Radians), FMath::Sin(Radians));
		const float Range = Spec.GetMaxRange();
		// bLostPerch: the 2026-11-02 failure - on the roof, but the perch was not found (a multi-slab roof, the wall top under its edge):
		// the flat model at the roof's level (targets assumed up there, no perch obstacle).
		const float FeetZ = bElevated || bLostPerch ? Scene.RoofCm : 0.f;
		float Edge = 0.f;
		if (bElevated)
		{
			float Near = 0.f;
			FVector2D EdgeNormal;
			SegmentCrossing(Dir, Range, Scene.Tower.Box, Near, Edge, nullptr, &EdgeNormal);
			Ray.GroundCm = VisionConeVisibility::ElevatedGroundCm(FeetZ, 0.f);
			FVisionConeObstacle Roof;
			Roof.NearCm = 0.f;
			Roof.FarCm = Edge;
			Roof.TopCm = Scene.Tower.TopCm - FeetZ;
			Roof.FarNormal = EdgeNormal;
			Roof.bHidesInside = true;
			Ray.Obstacles.Add(Roof);
		}
		const float High = Spec.GetHighTraceHeight(Ray.GroundCm);
		const float Low = Spec.GetLowTraceHeight(Ray.GroundCm);
		float Wall = Range;
		for (int32 Index = 0; Index < Scene.Boxes.Num(); ++Index)
		{
			float Near = 0.f;
			float Far = 0.f;
			FVector2D Normal;
			if (Scene.Boxes[Index].TopCm - FeetZ >= High && SegmentCrossing(Dir, Range, Scene.Boxes[Index].Box, Near, Far, &Normal) && Near < Wall)
			{
				Wall = Near;
				Ray.BlockerKey = Index + 1;
				Ray.BlockerNormal = Normal;
			}
		}
		if (Ray.BlockerKey != 0)
		{
			Ray.Obstacles.Add({ Wall, Wall, VisionConeVisibility::FullHeightCm, Ray.BlockerNormal, Ray.BlockerNormal });
		}
		// The low obstacles nearest first, at most MaxLow of them (the component's per-ray budget; < 0 = all).
		TArray<FVisionConeObstacle> LowObstacles;
		for (const FTowerGroundBox& Box : Scene.Boxes)
		{
			float Near = 0.f;
			float Far = 0.f;
			FVector2D NearNormal;
			FVector2D FarNormal;
			const float Top = Box.TopCm - FeetZ;
			if (Top < High && Top > Low && SegmentCrossing(Dir, Range, Box.Box, Near, Far, &NearNormal, &FarNormal) && Near < Wall - 1.f && Near > Edge)
			{
				LowObstacles.Add({ Near, FMath::Min(Far, Wall), Top, NearNormal, FarNormal });
			}
		}
		LowObstacles.Sort([](const FVisionConeObstacle& A, const FVisionConeObstacle& B) { return A.NearCm < B.NearCm; });
		for (int32 Index = 0; Index < LowObstacles.Num() && (MaxLow < 0 || Index < MaxLow); ++Index)
		{
			Ray.Obstacles.Add(LowObstacles[Index]);
		}
	}

	/** Exact 3D sight: the segment from the eye (feet + EyeHeightCm) to the target's profile point over the ground, against every box. */
	bool SeesFromHeight(const FVisionConeSightSpec& Spec, const FTowerScene& Scene, bool bElevated, int32 Level, const FVector2D& Target)
	{
		const float Distance = Target.Size();
		if (Distance > Spec.Levels[Level].RangeCm || Distance <= 0.f)
		{
			return false;
		}
		const float Eye = (bElevated ? Scene.RoofCm : 0.f) + Spec.EyeHeightCm;
		const float Profile = Spec.Levels[Level].ProfileHeightCm;
		TArray<FTowerGroundBox> All = Scene.Boxes;
		if (bElevated)
		{
			All.Add(Scene.Tower);
		}
		for (const FTowerGroundBox& Box : All)
		{
			float Near = 0.f;
			float Far = 0.f;
			if (SegmentCrossing(Target / Distance, Distance, Box.Box, Near, Far))
			{
				const float HeightNear = SightRules::LineHeightAt(Eye, Profile, Near, Distance);
				const float HeightFar = SightRules::LineHeightAt(Eye, Profile, Far, Distance);
				if (FMath::Min(HeightNear, HeightFar) < Box.TopCm)
				{
					return false;
				}
			}
		}
		return true;
	}

	/** Grid points inside the cone: drawn level vs the exact test (points within 3 cm of a box or a level boundary skipped). */
	void CompareTowerGrid(const FVisionConeSightSpec& Spec, const FTowerScene& Scene, bool bElevated, const TArray<FVisionConeRay>& Rays,
		int32& OutChecked, int32& OutMismatches, int32& OutCut, FString& OutFirst)
	{
		auto Exact = [&](const FVector2D& Point)
		{
			uint8 Level = 0;
			for (int32 Index = 0; Index < Spec.Levels.Num(); ++Index)
			{
				Level += SeesFromHeight(Spec, Scene, bElevated, Index, Point) ? 1 : 0;
			}
			return Level;
		};
		for (float X = 23.f; X < Spec.GetMaxRange(); X += 37.f)
		{
			for (float Y = -Spec.GetMaxRange(); Y < Spec.GetMaxRange(); Y += 41.f)
			{
				const FVector2D Point(X, Y);
				const float Angle = FMath::RadiansToDegrees(FMath::Atan2(Y, X));
				if (FMath::Abs(Angle) > Spec.HalfAngleDeg - 0.1f || Point.Size() > Spec.GetMaxRange() - 3.f)
				{
					continue;
				}
				bool bStable = true;
				for (const FTowerGroundBox& Box : Scene.Boxes)
				{
					bStable &= !Box.Box.ExpandBy(3.f).IsInside(Point);
				}
				const uint8 Level = Exact(Point);
				for (const FVector2D& Offset : { FVector2D(3.f, 0.f), FVector2D(-3.f, 0.f), FVector2D(0.f, 3.f), FVector2D(0.f, -3.f) })
				{
					bStable &= Exact(Point + Offset) == Level;
				}
				if (!bStable)
				{
					continue;
				}
				++OutChecked;
				OutCut += Level == 0 ? 1 : 0;
				const uint8 Drawn = VisionConeVisibility::LevelAtPoint(Rays, Spec.HalfAngleDeg, Point);
				if (Drawn != Level)
				{
					++OutMismatches;
					if (OutFirst.IsEmpty())
					{
						OutFirst = FString::Printf(TEXT("(%.0f, %.0f) drawn %d exact %d"), X, Y, Drawn, Level);
					}
				}
			}
		}
	}

	FVisionConeSightSpec StandingOperative()
	{
		FVisionConeSightSpec Spec = VisionConeVisibility::MakeOperativeSightSpec(SightRules::EyeHeight(EOperativeStance::Standing), 3000.f);
		Spec.HalfAngleDeg = 45.f;
		return Spec;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisionConeElevatedRulesTest, "CodexTactics.Combat.VisionCone.Elevated.ShadowRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVisionConeElevatedRulesTest::RunTest(const FString& Parameters)
{
	using namespace VisionConeVisibility;
	// The designer's rule: behind a wall h_w at d from an eye h_e > h_w the ground is hidden for h_w x d / (h_e - h_w).
	TestEqual(TEXT("eye 5.6 m (4 m tower + 1.6 m), 3 m wall 10 m out: ground shadow 11.5 m"), WallShadowLengthCm(560.f, 300.f, 1000.f), 300.f * 1000.f / 260.f, 0.01f);
	TestEqual(TEXT("a standing target's profile (1.5 m): shadow 1000 x 150 / 260"), WallShadowLengthCm(560.f, 300.f, 1000.f, 150.f), 1000.f * 150.f / 260.f, 0.01f);
	TestEqual(TEXT("eye not above the wall: everything behind it hidden"), WallShadowLengthCm(300.f, 300.f, 1000.f), TNumericLimits<float>::Max());
	TestEqual(TEXT("a profile above the wall: no shadow"), WallShadowLengthCm(560.f, 120.f, 1000.f, 150.f), 0.f);
	TestTrue(TEXT("higher eye: shorter shadow"), WallShadowLengthCm(800.f, 300.f, 1000.f) < WallShadowLengthCm(560.f, 300.f, 1000.f));
	// The perch's dead zone: standing at the roof's middle (edge 1.5 m away), eye 1.6 m over a 4 m roof.
	TestEqual(TEXT("dead zone of the ground: 150 x 560 / 160 = 5.25 m"), PerchDeadZoneCm(160.f, 400.f, 150.f), 525.f, 0.01f);
	TestEqual(TEXT("dead zone of a standing profile: 150 x 410 / 160"), PerchDeadZoneCm(160.f, 400.f, 150.f, 150.f), 150.f * 410.f / 160.f, 0.01f);
	TestTrue(TEXT("prone (eye 25 cm) on the roof: a far longer dead zone"), PerchDeadZoneCm(25.f, 400.f, 150.f) > 4.f * PerchDeadZoneCm(160.f, 400.f, 150.f));
	TestTrue(TEXT("at the very edge: a short dead zone"), PerchDeadZoneCm(160.f, 400.f, 20.f) < 100.f);
	TestEqual(TEXT("a profile up to the roof: nothing past the edge hidden"), PerchDeadZoneCm(160.f, 100.f, 150.f, 150.f), 150.f);
	// Elevated or not: a tower / roof yes, a crate or a step no (the flat model).
	TestEqual(TEXT("4 m above the ground: ground -400"), ElevatedGroundCm(400.f, 0.f), -400.f);
	TestEqual(TEXT("on a 50 cm crate: flat"), ElevatedGroundCm(50.f, 0.f), 0.f);
	TestEqual(TEXT("exactly the minimum drop: elevated"), ElevatedGroundCm(ElevatedMinDropCm, 0.f), -ElevatedMinDropCm);
	TestEqual(TEXT("ground above the feet: flat"), ElevatedGroundCm(0.f, 300.f), 0.f);
	// The trace heights follow the target ground; at ground level they are the old ones.
	const FVisionConeSightSpec Spec = VisionConeElevatedTest::StandingOperative();
	TestEqual(TEXT("flat: low trace at the crouched profile"), Spec.GetLowTraceHeight(), 90.f);
	TestEqual(TEXT("flat: high trace at the eye"), Spec.GetHighTraceHeight(), 160.f);
	TestEqual(TEXT("from 4 m: low trace 90 cm over the ground below"), Spec.GetLowTraceHeight(-400.f), -310.f);
	TestEqual(TEXT("from 4 m: high trace still at the eye"), Spec.GetHighTraceHeight(-400.f), 160.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisionConeElevatedRayTest, "CodexTactics.Combat.VisionCone.Elevated.TowerOverWall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVisionConeElevatedRayTest::RunTest(const FString& Parameters)
{
	using namespace VisionConeVisibility;
	// One ray of the designer's case: standing at the middle of a 4 m tower's roof (edge 1.5 m), a 3 m wall 10 - 10.4 m out.
	const FVisionConeSightSpec Spec = VisionConeElevatedTest::StandingOperative();
	FVisionConeRay Ray;
	Ray.GroundCm = -400.f;
	FVisionConeObstacle Roof;
	Roof.FarCm = 150.f;
	Roof.TopCm = 0.f;
	Roof.bHidesInside = true;
	Ray.Obstacles.Add(Roof);
	Ray.Obstacles.Add({ 1000.f, 1040.f, -100.f });
	ClassifyRay(Spec, Ray);
	const float StandingDead = PerchDeadZoneCm(160.f, 400.f, 150.f, 150.f);   // 384 cm
	const float CrouchedDead = PerchDeadZoneCm(160.f, 400.f, 150.f, 90.f);    // 441 cm
	const float StandingShadow = 1040.f + WallShadowLengthCm(560.f, 300.f, 1040.f, 150.f); // 1640 cm
	const float CrouchedShadow = 1040.f + WallShadowLengthCm(560.f, 300.f, 1040.f, 90.f);  // 1880 cm
	TestEqual(TEXT("under his own roof: hidden"), LevelAlong(Ray, 100.f), static_cast<uint8>(0));
	TestEqual(TEXT("the roof edge's dead zone at the foot: hidden"), LevelAlong(Ray, StandingDead - 20.f), static_cast<uint8>(0));
	TestEqual(TEXT("past the standing dead zone: a standing target only"), LevelAlong(Ray, 0.5f * (StandingDead + CrouchedDead)), static_cast<uint8>(1));
	TestEqual(TEXT("before the wall: everyone seen (full)"), LevelAlong(Ray, 800.f), static_cast<uint8>(2));
	TestEqual(TEXT("the wall's shadow: hidden"), LevelAlong(Ray, 0.5f * (1040.f + StandingShadow)), static_cast<uint8>(0));
	TestEqual(TEXT("the shadow's tail: a standing target only"), LevelAlong(Ray, 0.5f * (StandingShadow + CrouchedShadow)), static_cast<uint8>(1));
	TestEqual(TEXT("beyond the shadow: full again (he sees over the wall)"), LevelAlong(Ray, CrouchedShadow + 100.f), static_cast<uint8>(2));
	// The span ends sit on the analytic boundaries.
	bool bShadowEdge = false;
	for (const FVisionConeSpan& Span : Ray.Spans)
	{
		bShadowEdge |= FMath::IsNearlyEqual(Span.StartCm, StandingShadow, 0.5f);
	}
	TestTrue(FString::Printf(TEXT("a span starts at the shadow's end %.0f cm"), StandingShadow), bShadowEdge);

	// Before the fix the tower's view ignored the target ground: the same ray at ground level sees nothing behind a 3 m wall.
	FVisionConeRay Flat;
	Flat.Obstacles.Add({ 1000.f, 1000.f, FullHeightCm });
	ClassifyRay(Spec, Flat);
	TestEqual(TEXT("ground level: behind a 3 m wall nothing (unchanged)"), LevelAlong(Flat, 2500.f), static_cast<uint8>(0));
	TestEqual(TEXT("ground level: before it full (unchanged)"), LevelAlong(Flat, 800.f), static_cast<uint8>(2));

	// A low perch (1 m platform, eye 2.6 m) and the same 3 m wall: its top is above the eye - a full wall, nothing seen behind it.
	FVisionConeRay Low;
	Low.GroundCm = -100.f;
	TestTrue(TEXT("a 3 m wall from a 1 m platform is a full wall (above the high trace)"), 300.f - 100.f >= Spec.GetHighTraceHeight(Low.GroundCm));
	Low.Obstacles.Add({ 1000.f, 1000.f, FullHeightCm });
	ClassifyRay(Spec, Low);
	TestEqual(TEXT("eye below the wall top: everything behind hidden"), LevelAlong(Low, 2500.f), static_cast<uint8>(0));

	// Neighbouring rays on and off the perch (its corner): refined.
	FVisionConeRay A = Ray;
	FVisionConeRay B = Ray;
	B.GroundCm = 0.f;
	TestTrue(TEXT("another ground level between two rays: refined"), NeedsRefine(Spec, A, B));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisionConeElevatedConeTest, "CodexTactics.Combat.VisionCone.Elevated.ConeMatchesSight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVisionConeElevatedConeTest::RunTest(const FString& Parameters)
{
	using namespace VisionConeVisibility;
	using namespace VisionConeElevatedTest;
	const FVisionConeSightSpec Spec = StandingOperative();
	const FTowerScene Scene;
	for (const bool bElevated : { true, false })
	{
		const TCHAR* Who = bElevated ? TEXT("tower") : TEXT("ground");
		int32 Samples = 0;
		const TArray<FVisionConeRay> Rays = CastRays(Spec, [&](float Angle, FVisionConeRay& Ray) { SampleTowerScene(Spec, Scene, bElevated, Angle, Ray); }, &Samples);
		int32 Checked = 0;
		int32 Mismatches = 0;
		int32 Cut = 0;
		FString First;
		CompareTowerGrid(Spec, Scene, bElevated, Rays, Checked, Mismatches, Cut, First);
		TestTrue(FString::Printf(TEXT("%s: enough points %d (cut %d)"), Who, Checked, Cut), Checked > 1500 && Cut > 100);
		TestEqual(FString::Printf(TEXT("%s: drawn level = exact 3D sight (first mismatch %s)"), Who, *First), Mismatches, 0);
		TestTrue(FString::Printf(TEXT("%s: rays %d within the cap"), Who, Samples), Samples <= Spec.MaxRays);

		// The mesh lies on the ground the targets stand on: 4 m below the tower's roof, at the feet on the ground.
		const FVisionConeShape Shape = BuildShape(Rays, Spec.Levels.Num(), Spec.HalfAngleDeg, 8.f, 0.f, 0.5f);
		const float Expected = bElevated ? -400.f : 0.f;
		bool bOnGround = Shape.Full.Vertices.Num() > 0;
		for (const FVisionConeMesh* Mesh : { &Shape.Full, &Shape.Light })
		{
			for (const FVector& Vertex : Mesh->Vertices)
			{
				bOnGround &= FMath::IsNearlyEqual(static_cast<float>(Vertex.Z), Expected, 0.01f);
			}
		}
		bool bOutline = Shape.Outline.Vertices.Num() > 0;
		for (const FVector& Vertex : Shape.Outline.Vertices)
		{
			bOutline &= FMath::IsNearlyEqual(static_cast<float>(Vertex.Z), Expected + 0.5f, 0.01f);
		}
		TestTrue(FString::Printf(TEXT("%s: fill on the ground (z %.0f)"), Who, Expected), bOnGround);
		TestTrue(FString::Printf(TEXT("%s: outline a hair above it"), Who), bOutline);
		bool bFlatRays = true;
		for (const FVisionConeRay& Ray : Rays)
		{
			bFlatRays &= FMath::IsNearlyEqual(Ray.GroundCm, Expected);
		}
		TestTrue(FString::Printf(TEXT("%s: every ray's ground %.0f"), Who, Expected), bFlatRays);
	}
	// The designer's black triangle: from the tower the ground right behind the wall is cut, the ground beyond its shadow is seen;
	// at ground level everything behind the 3 m wall is cut.
	const TArray<FVisionConeRay> Tower = CastRays(Spec, [&](float Angle, FVisionConeRay& Ray) { SampleTowerScene(Spec, Scene, true, Angle, Ray); });
	TestEqual(TEXT("tower: right behind the wall cut"), LevelAtPoint(Tower, Spec.HalfAngleDeg, FVector2D(1300.f, 0.f)), static_cast<uint8>(0));
	TestEqual(TEXT("tower: beyond the wall's shadow seen"), LevelAtPoint(Tower, Spec.HalfAngleDeg, FVector2D(2200.f, 0.f)), static_cast<uint8>(2));
	TestEqual(TEXT("tower: before the wall seen"), LevelAtPoint(Tower, Spec.HalfAngleDeg, FVector2D(800.f, 0.f)), static_cast<uint8>(2));
	TestEqual(TEXT("tower: the foot of the tower cut (dead zone)"), LevelAtPoint(Tower, Spec.HalfAngleDeg, FVector2D(300.f, 0.f)), static_cast<uint8>(0));
	TestEqual(TEXT("tower: the 6 m block hides all behind it"), LevelAtPoint(Tower, Spec.HalfAngleDeg, FVector2D(2185.f, 1206.f)), static_cast<uint8>(0));
	const TArray<FVisionConeRay> Ground = CastRays(Spec, [&](float Angle, FVisionConeRay& Ray) { SampleTowerScene(Spec, Scene, false, Angle, Ray); });
	TestEqual(TEXT("ground: far behind the wall cut"), LevelAtPoint(Ground, Spec.HalfAngleDeg, FVector2D(2200.f, 0.f)), static_cast<uint8>(0));
	TestEqual(TEXT("ground: the foot of the tower seen (no perch)"), LevelAtPoint(Ground, Spec.HalfAngleDeg, FVector2D(300.f, 0.f)), static_cast<uint8>(2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisionConeElevatedRoofSniperTest, "CodexTactics.Combat.VisionCone.Elevated.RoofSniperOverWalls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVisionConeElevatedRoofSniperTest::RunTest(const FString& Parameters)
{
	// Game designer 2026-11-02 (L_Gen_Depot_1): the Engineer PRONE at the edge of a +3 hall roof (9 m), a sniper - his cone still covered the
	// ground behind the walls below. The model, solved exactly, cuts every wall's shadow; the two ways the old sampler lost them are shown
	// here: (1) a wall behind three crates was never measured (3 low obstacles per ray - from a roof every wall below the eye is "low"),
	// (2) a perch not recognised (a multi-slab roof / the wall top flush with its edge) fell back to the flat model at the roof's level.
	using namespace VisionConeVisibility;
	using namespace VisionConeElevatedTest;
	FTowerScene Scene;
	Scene.RoofCm = 900.f;
	Scene.Tower = { FBox2D(FVector2D(-2500.f, -1500.f), FVector2D(30.f, 1500.f)), 900.f }; // the hall's roof, its edge 30 cm in front of him
	Scene.Boxes.Reset();
	Scene.Boxes.Add({ FBox2D(FVector2D(400.f, -60.f), FVector2D(520.f, 60.f)), 110.f });    // three crates in a row on the street
	Scene.Boxes.Add({ FBox2D(FVector2D(700.f, -60.f), FVector2D(820.f, 60.f)), 110.f });
	Scene.Boxes.Add({ FBox2D(FVector2D(1000.f, -60.f), FVector2D(1120.f, 60.f)), 110.f });
	Scene.Boxes.Add({ FBox2D(FVector2D(1300.f, 400.f), FVector2D(1420.f, 520.f)), 150.f }); // a wreck
	Scene.Boxes.Add({ FBox2D(FVector2D(1800.f, -800.f), FVector2D(1830.f, 800.f)), 300.f }); // a 3 m wall 18 m out
	Scene.Boxes.Add({ FBox2D(FVector2D(2500.f, -1200.f), FVector2D(2510.f, 400.f)), 250.f }); // a fence
	Scene.Boxes.Add({ FBox2D(FVector2D(3100.f, -400.f), FVector2D(3130.f, 1200.f)), 300.f }); // another wall
	FVisionConeSightSpec Spec = VisionConeVisibility::MakeOperativeSightSpec(SightRules::EyeHeight(EOperativeStance::Prone), 4000.f);
	Spec.HalfAngleDeg = 30.f;
	Spec.MaxRays = 2048;
	const float Eye = Scene.RoofCm + Spec.EyeHeightCm; // 925 over the street

	const TArray<FVisionConeRay> Rays = CastRays(Spec, [&](float Angle, FVisionConeRay& Ray) { SampleTowerScene(Spec, Scene, true, Angle, Ray); });
	int32 Checked = 0;
	int32 Mismatches = 0;
	int32 Cut = 0;
	FString First;
	CompareTowerGrid(Spec, Scene, true, Rays, Checked, Mismatches, Cut, First);
	TestTrue(FString::Printf(TEXT("hall roof: enough points %d (cut %d)"), Checked, Cut), Checked > 2000 && Cut > 300);
	TestEqual(FString::Printf(TEXT("hall roof: drawn level = exact 3D sight, every wall's shadow cut (first mismatch %s)"), *First), Mismatches, 0);
	// The wall's shadow on the street: 300 x 1830 / (925 - 300) = 8.8 m of ground; a standing man 4.4 m, a crouched one 5.6 m behind it.
	const float GroundShadow = WallShadowLengthCm(Eye, 300.f, 1830.f);
	TestEqual(TEXT("ground shadow of the 3 m wall from the 9 m roof"), GroundShadow, 300.f * 1830.f / 625.f, 0.5f);
	const FVector2D InShadow(1830.f + 0.5f * WallShadowLengthCm(Eye, 300.f, 1830.f, 150.f), 0.f);
	const FVector2D Beyond(1830.f + WallShadowLengthCm(Eye, 300.f, 1830.f, 90.f) + 120.f, 600.f); // past the fence's end
	TestEqual(TEXT("behind the wall (its shadow): cut"), LevelAtPoint(Rays, Spec.HalfAngleDeg, InShadow), static_cast<uint8>(0));
	TestEqual(TEXT("beyond the shadow: seen over the wall"), LevelAtPoint(Rays, Spec.HalfAngleDeg, Beyond), static_cast<uint8>(2));
	TestEqual(TEXT("the street right under the roof's edge: the dead zone"), LevelAtPoint(Rays, Spec.HalfAngleDeg, FVector2D(150.f, 0.f)), static_cast<uint8>(0));

	// (1) The old budget: 3 low obstacles per ray - along the crates' row the wall behind them is never measured and its shadow is drawn seen.
	const TArray<FVisionConeRay> Capped = CastRays(Spec, [&](float Angle, FVisionConeRay& Ray) { SampleTowerScene(Spec, Scene, true, Angle, Ray, 3); });
	TestEqual(TEXT("budget 3: the shadow behind three crates drawn as seen (the bug)"), LevelAtPoint(Capped, Spec.HalfAngleDeg, InShadow), static_cast<uint8>(2));
	const TArray<FVisionConeRay> Budget = CastRays(Spec, [&](float Angle, FVisionConeRay& Ray) { SampleTowerScene(Spec, Scene, true, Angle, Ray, 12); });
	TestEqual(TEXT("budget 12 (MaxLowObstaclesPerRayElevated): the shadow cut"), LevelAtPoint(Budget, Spec.HalfAngleDeg, InShadow), static_cast<uint8>(0));

	// (2) The perch lost: the flat model at the roof's level - every wall below the sight lines, nothing behind them cut.
	const TArray<FVisionConeRay> Lost = CastRays(Spec, [&](float Angle, FVisionConeRay& Ray) { SampleTowerScene(Spec, Scene, false, Angle, Ray, -1, true); });
	TestEqual(TEXT("perch lost: the wall's shadow drawn as seen (the bug)"), LevelAtPoint(Lost, Spec.HalfAngleDeg, InShadow), static_cast<uint8>(2));
	bool bFlat = true;
	for (const FVisionConeRay& Ray : Lost)
	{
		bFlat &= Ray.GroundCm == 0.f;
	}
	TestTrue(TEXT("perch lost: every ray at the roof's level"), bFlat);
	return true;
}
