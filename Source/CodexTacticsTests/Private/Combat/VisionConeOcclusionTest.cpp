// CodexTactics.Combat.VisionCone.Occlusion.* — occluded vision cones (game designer 2026-10-19: the cone must cut out what
// the enemy cannot see, Commandos style; UE-only, no Godot reference). Pure geometry: ray angles, the sight rule solved
// along a ray (SightRules::LineHeightAt), span classification, edge refinement by bisection, the mesh, and the drawn
// level at a point agreeing with an exact 3D line test against box obstacles.

#include "Combat/SightRules.h"
#include "Combat/VisionConeVisibility.h"
#include "Misc/AutomationTest.h"

namespace VisionConeOcclusionTest
{
	/** An axis-aligned box obstacle of the cone frame (planar footprint + top height above the observer's feet). */
	struct FBoxObstacle
	{
		FBox2D Box;
		float TopCm = 0.f;
	};

	/** Ray / box footprint intersection (slab method): entry / exit distance along Dir within [0, MaxCm]; entry axis normal. */
	bool IntersectRay(const FVector2D& Dir, float MaxCm, const FBox2D& Box, float& OutNear, float& OutFar, FVector2D& OutNormal,
		FVector2D* OutFarNormal = nullptr)
	{
		float Near = 0.f;
		float Far = MaxCm;
		OutNormal = FVector2D::ZeroVector;
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
				OutNormal = Axis == 0 ? FVector2D(Sign, 0.f) : FVector2D(0.f, Sign);
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
		if (OutFarNormal)
		{
			*OutFarNormal = FarNormal;
		}
		return true;
	}

	/** The world sampler's logic on boxes: the nearest box at least the high trace's height is a full wall; lower ones above the low trace are obstacles. */
	void SampleBoxes(const FVisionConeSightSpec& Spec, const TArray<FBoxObstacle>& Boxes, float AngleDeg, FVisionConeRay& Ray)
	{
		const float Radians = FMath::DegreesToRadians(AngleDeg);
		const FVector2D Dir(FMath::Cos(Radians), FMath::Sin(Radians));
		const float Range = Spec.GetMaxRange();
		float Wall = Range;
		for (int32 Index = 0; Index < Boxes.Num(); ++Index)
		{
			float Near = 0.f;
			float Far = 0.f;
			FVector2D Normal;
			if (Boxes[Index].TopCm >= Spec.GetHighTraceHeight() && IntersectRay(Dir, Range, Boxes[Index].Box, Near, Far, Normal) && Near < Wall)
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
		for (const FBoxObstacle& Box : Boxes)
		{
			float Near = 0.f;
			float Far = 0.f;
			FVector2D Normal;
			FVector2D FarNormal;
			if (Box.TopCm < Spec.GetHighTraceHeight() && Box.TopCm > Spec.GetLowTraceHeight() && IntersectRay(Dir, Range, Box.Box, Near, Far, Normal, &FarNormal)
				&& Near < Wall - 1.f)
			{
				Ray.Obstacles.Add({ Near, FMath::Min(Far, Wall), Box.TopCm, Normal, FarNormal });
			}
		}
	}

	/** Exact 3D sight test: the segment eye -> target profile against every box (line height linear: lowest at an end of the crossing). */
	bool SeesExactly(const FVisionConeSightSpec& Spec, const TArray<FBoxObstacle>& Boxes, int32 Level, const FVector2D& Target)
	{
		const float Distance = Target.Size();
		if (Distance > Spec.Levels[Level].RangeCm)
		{
			return false;
		}
		const float Eye = Spec.EyeHeightCm;
		const float Profile = Spec.Levels[Level].ProfileHeightCm;
		for (const FBoxObstacle& Box : Boxes)
		{
			float Near = 0.f;
			float Far = 0.f;
			FVector2D Normal;
			if (Distance > 0.f && IntersectRay(Target / Distance, Distance, Box.Box, Near, Far, Normal))
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

	FVisionConeSightSpec EnemySpec()
	{
		// A standing enemy: eyes 160 cm; a standing operative (150 cm profile) seen to 20 m, a crouched one (90 cm) to 14 m.
		FVisionConeSightSpec Spec;
		Spec.EyeHeightCm = 160.f;
		Spec.HalfAngleDeg = 45.f;
		Spec.Levels.Add({ 150.f, 2000.f });
		Spec.Levels.Add({ 90.f, 1400.f });
		return Spec;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisionConeOcclusionAnglesTest, "CodexTactics.Combat.VisionCone.Occlusion.RayAngles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVisionConeOcclusionAnglesTest::RunTest(const FString& Parameters)
{
	using namespace VisionConeVisibility;
	const TArray<float> Angles = BaseAngles(45.f, 1.5f, 320);
	TestEqual(TEXT("+-45 deg every 1.5 deg: 61 rays"), Angles.Num(), 61);
	TestEqual(TEXT("first ray on the left edge"), Angles[0], -45.f, 1.e-4f);
	TestEqual(TEXT("last ray on the right edge"), Angles.Last(), 45.f, 1.e-4f);
	bool bEven = true;
	for (int32 Index = 1; Index < Angles.Num(); ++Index)
	{
		bEven &= FMath::IsNearlyEqual(Angles[Index] - Angles[Index - 1], 1.5f, 1.e-3f);
	}
	TestTrue(TEXT("evenly spaced"), bEven);
	TestEqual(TEXT("step that does not divide the angle: ceil, still both edges"), BaseAngles(35.f, 2.f, 320).Num(), 36);
	TestEqual(TEXT("ray cap"), BaseAngles(90.f, 0.25f, 50).Num(), 50);
	TestEqual(TEXT("tiny cone still 2 rays"), BaseAngles(0.5f, 5.f, 320).Num(), 2);
	const TArray<float> Round = BaseAngles(180.f, 2.f, 320);
	TestEqual(TEXT("all round: -180 .. +180"), Round.Num(), 181);
	TestEqual(TEXT("all round starts behind"), Round[0], -180.f, 1.e-4f);

	// The spec's trace heights: the sight lines run between the eye and the profiles.
	const FVisionConeSightSpec Spec = VisionConeOcclusionTest::EnemySpec();
	TestEqual(TEXT("high trace at the eye (above both profiles)"), Spec.GetHighTraceHeight(), 160.f);
	TestEqual(TEXT("low trace at the crouched profile"), Spec.GetLowTraceHeight(), 90.f);
	TestEqual(TEXT("max range = standing range"), Spec.GetMaxRange(), 2000.f);
	FVisionConeSightSpec Hound = Spec;
	Hound.EyeHeightCm = 70.f;
	TestEqual(TEXT("low eye (hound): high trace at the standing profile"), Hound.GetHighTraceHeight(), 150.f);
	TestEqual(TEXT("low eye (hound): low trace at the eye"), Hound.GetLowTraceHeight(), 70.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisionConeOcclusionFaceTest, "CodexTactics.Combat.VisionCone.Occlusion.SightRuleAlongRay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVisionConeOcclusionFaceTest::RunTest(const FString& Parameters)
{
	using namespace VisionConeVisibility;
	// The face rule is the game's cover rule (SightRules::ClearsCover) for every sampled case.
	int32 Mismatches = 0;
	for (const float Eye : { 25.f, 70.f, 95.f, 160.f })
	{
		for (const float Profile : { 25.f, 90.f, 150.f })
		{
			for (const float Top : { 40.f, 60.f, 100.f, 120.f, 155.f, 200.f })
			{
				for (const float Target : { 300.f, 650.f, 900.f, 1500.f, 2500.f })
				{
					const bool bBlocked = IsBlockedByFace(Eye, Profile, 600.f, Top, Target);
					const bool bRule = Target > 600.f && !SightRules::ClearsCover(Eye, Profile, 600.f, Target, Top);
					Mismatches += bBlocked != bRule ? 1 : 0;
				}
			}
		}
	}
	TestEqual(TEXT("face rule = SightRules::ClearsCover"), Mismatches, 0);
	TestFalse(TEXT("a target in front of the face is never blocked by it"), IsBlockedByFace(160.f, 90.f, 600.f, FullHeightCm, 599.f));
	TestTrue(TEXT("a full wall hides everything behind"), IsBlockedByFace(160.f, 150.f, 600.f, FullHeightCm, 5000.f));

	// Breakpoints: descending line (standing eye 160 -> crouched 90) over a 120 cm wall 600 cm away: hidden up to 600 x 70 / 40.
	TArray<float, TInlineAllocator<16>> Breaks;
	AddFaceBreaks(160.f, 90.f, 600.f, 120.f, Breaks);
	TestEqual(TEXT("two breaks (face, crossing)"), Breaks.Num(), 2);
	if (Breaks.Num() == 2)
	{
		TestEqual(TEXT("crossing 1050 cm"), Breaks[1], 1050.f, 0.01f);
		TestTrue(TEXT("hidden just before the crossing"), IsBlockedByFace(160.f, 90.f, 600.f, 120.f, 1049.f));
		TestFalse(TEXT("seen just after the crossing"), IsBlockedByFace(160.f, 90.f, 600.f, 120.f, 1051.f));
	}
	// Ascending line (hound eye 70 -> standing 150) over a 100 cm wall 600 cm away: seen close behind it, hidden beyond 600 x 80 / 30.
	Breaks.Reset();
	AddFaceBreaks(70.f, 150.f, 600.f, 100.f, Breaks);
	TestEqual(TEXT("ascending: two breaks"), Breaks.Num(), 2);
	if (Breaks.Num() == 2)
	{
		TestEqual(TEXT("ascending crossing 1600 cm"), Breaks[1], 1600.f, 0.01f);
		TestFalse(TEXT("ascending: seen close behind the wall"), IsBlockedByFace(70.f, 150.f, 600.f, 100.f, 1500.f));
		TestTrue(TEXT("ascending: hidden far behind"), IsBlockedByFace(70.f, 150.f, 600.f, 100.f, 1700.f));
	}
	// A wall below both lines adds no crossing; a full wall none either.
	Breaks.Reset();
	AddFaceBreaks(160.f, 150.f, 600.f, 60.f, Breaks);
	AddFaceBreaks(160.f, 150.f, 600.f, FullHeightCm, Breaks);
	TestEqual(TEXT("no crossing for a low or a full face"), Breaks.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisionConeOcclusionSpansTest, "CodexTactics.Combat.VisionCone.Occlusion.ClassifySpans",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVisionConeOcclusionSpansTest::RunTest(const FString& Parameters)
{
	using namespace VisionConeVisibility;
	const FVisionConeSightSpec Spec = VisionConeOcclusionTest::EnemySpec();
	auto Describe = [](const FVisionConeRay& Ray)
	{
		FString Text;
		for (const FVisionConeSpan& Span : Ray.Spans)
		{
			Text += FString::Printf(TEXT("[%.0f-%.0f L%d]"), Span.StartCm, Span.EndCm, Span.Level);
		}
		return Text;
	};

	// Open ground: crouched seen to 14 m (full tone), standing only beyond it to 20 m (light tone).
	FVisionConeRay Open;
	ClassifyRay(Spec, Open);
	TestEqual(TEXT("open ray"), Describe(Open), FString(TEXT("[0-1400 L2][1400-2000 L1]")));

	// A wall taller than the sight lines 800 cm out: everything behind it cut.
	FVisionConeRay Wall;
	Wall.Obstacles.Add({ 800.f, 800.f, FullHeightCm });
	ClassifyRay(Spec, Wall);
	TestEqual(TEXT("high wall"), Describe(Wall), FString(TEXT("[0-800 L2]")));
	TestEqual(TEXT("high wall: nothing past it"), LevelAlong(Wall, 801.f), static_cast<uint8>(0));

	// A 120 cm wall 600-640 cm out: a crouched operative is hidden up to 640 x 70 / 40 = 1120 cm, a standing one never.
	FVisionConeRay Low;
	Low.Obstacles.Add({ 600.f, 640.f, 120.f });
	ClassifyRay(Spec, Low);
	TestEqual(TEXT("low wall: standing-only band behind it"), Describe(Low), FString(TEXT("[0-600 L2][600-1120 L1][1120-1400 L2][1400-2000 L1]")));

	// A 155 cm wall (between the standing profile and the eye): standing hidden right behind it, then seen again.
	FVisionConeRay Tall;
	Tall.Obstacles.Add({ 500.f, 520.f, 155.f });
	ClassifyRay(Spec, Tall);
	// Standing: hidden up to 520 x 10 / 5 = 1040; crouched: up to 520 x 70 / 5 = 7280 (beyond its 14 m range).
	TestEqual(TEXT("155 cm wall"), Describe(Tall), FString(TEXT("[0-500 L2][1040-2000 L1]")));

	// A wall below the crouched profile changes nothing; obstacles in any order.
	FVisionConeRay Knee;
	Knee.Obstacles.Add({ 900.f, 950.f, 120.f });
	Knee.Obstacles.Add({ 300.f, 320.f, 60.f });
	ClassifyRay(Spec, Knee);
	TestEqual(TEXT("knee-high wall ignored, order free"), Describe(Knee), FString(TEXT("[0-900 L2][900-2000 L1]")));

	// One level (an operative's cone): one tone, cut by the wall.
	FVisionConeSightSpec One;
	One.EyeHeightCm = 160.f;
	One.Levels.Add({ 150.f, 1400.f });
	FVisionConeRay Single;
	Single.Obstacles.Add({ 700.f, 700.f, FullHeightCm });
	ClassifyRay(One, Single);
	TestEqual(TEXT("one level"), Describe(Single), FString(TEXT("[0-700 L1]")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisionConeOcclusionRefineTest, "CodexTactics.Combat.VisionCone.Occlusion.EdgeRays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVisionConeOcclusionRefineTest::RunTest(const FString& Parameters)
{
	using namespace VisionConeVisibility;
	using namespace VisionConeOcclusionTest;
	FVisionConeSightSpec Spec = EnemySpec();
	// A 3 m tall wall 6-6.4 m ahead, from 0 to 4 m to the right: its left corner (6 m, 0) at 0 deg, the near right corner
	// (6 m, 4 m) at atan(4/6) = 33.69 deg.
	TArray<FBoxObstacle> Boxes;
	Boxes.Add({ FBox2D(FVector2D(600.f, 0.f), FVector2D(640.f, 400.f)), 300.f });
	int32 Samples = 0;
	const TArray<FVisionConeRay> Rays = CastRays(Spec, [&Spec, &Boxes](float Angle, FVisionConeRay& Ray) { SampleBoxes(Spec, Boxes, Angle, Ray); }, &Samples);
	TestTrue(FString::Printf(TEXT("sorted rays, %d samples within the cap"), Samples), Rays.Num() == Samples && Samples <= Spec.MaxRays);
	bool bSorted = true;
	for (int32 Index = 1; Index < Rays.Num(); ++Index)
	{
		bSorted &= Rays[Index].AngleDeg > Rays[Index - 1].AngleDeg;
	}
	TestTrue(TEXT("strictly increasing angles"), bSorted);
	// Each shadow edge: the last ray that passes the wall and the first that hits it are a fraction of a degree apart.
	auto EdgeGap = [&Rays](float NearAngle, float& OutEdge)
	{
		float Best = TNumericLimits<float>::Max();
		for (int32 Index = 1; Index < Rays.Num(); ++Index)
		{
			const bool bA = Rays[Index - 1].BlockerKey != 0;
			const bool bB = Rays[Index].BlockerKey != 0;
			const float Mid = (Rays[Index - 1].AngleDeg + Rays[Index].AngleDeg) * 0.5f;
			if (bA != bB && FMath::Abs(Mid - NearAngle) < 2.f)
			{
				Best = Rays[Index].AngleDeg - Rays[Index - 1].AngleDeg;
				OutEdge = Mid;
			}
		}
		return Best;
	};
	float LeftEdge = 0.f;
	float RightEdge = 0.f;
	const float LeftGap = EdgeGap(0.f, LeftEdge);
	const float RightGap = EdgeGap(33.69f, RightEdge);
	TestTrue(FString::Printf(TEXT("left shadow edge rays %.3f deg apart"), LeftGap), LeftGap <= 0.05f);
	TestTrue(FString::Printf(TEXT("right shadow edge rays %.3f deg apart"), RightGap), RightGap <= 0.05f);
	TestEqual(TEXT("left edge at the corner"), LeftEdge, 0.f, 0.05f);
	TestEqual(TEXT("right edge at the corner"), RightEdge, FMath::RadiansToDegrees(FMath::Atan2(400.f, 600.f)), 0.05f);
	// The open stretches are not refined (only the base 1.5 deg rays there).
	int32 OpenRays = 0;
	for (const FVisionConeRay& Ray : Rays)
	{
		OpenRays += Ray.AngleDeg < -2.f ? 1 : 0;
	}
	TestEqual(TEXT("no extra rays in the open"), OpenRays, 29);
	TestTrue(FString::Printf(TEXT("refinement is cheap: %d rays (61 base)"), Samples), Samples < 61 + 40);

	// NeedsRefine: same structure and close ends -> no; another blocker / structure / big jump / corner normal -> yes.
	FVisionConeRay A;
	FVisionConeRay B;
	A.Spans.Add({ 0.f, 1000.f, 2 });
	B.Spans.Add({ 0.f, 1010.f, 2 });
	TestFalse(TEXT("smooth neighbours"), NeedsRefine(Spec, A, B));
	B.Spans[0].EndCm = 1300.f;
	TestTrue(TEXT("a jump"), NeedsRefine(Spec, A, B));
	B.Spans[0].EndCm = 1010.f;
	B.BlockerKey = 7;
	TestTrue(TEXT("another blocker"), NeedsRefine(Spec, A, B));
	A.BlockerKey = 7;
	A.BlockerNormal = FVector2D(-1.f, 0.f);
	B.BlockerNormal = FVector2D(0.f, -1.f);
	TestTrue(TEXT("a corner of one mesh (normals 90 deg apart)"), NeedsRefine(Spec, A, B));
	B.BlockerNormal = FVector2D(-1.f, 0.f);
	B.Spans.Add({ 1200.f, 1400.f, 1 });
	TestTrue(TEXT("another structure"), NeedsRefine(Spec, A, B));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisionConeOcclusionShapeTest, "CodexTactics.Combat.VisionCone.Occlusion.ShapeMatchesSight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVisionConeOcclusionShapeTest::RunTest(const FString& Parameters)
{
	using namespace VisionConeVisibility;
	using namespace VisionConeOcclusionTest;
	const FVisionConeSightSpec Spec = EnemySpec();
	// The designer's shot: a tall wall left of the axis, a 1.2 m wall right of it, a 1.55 m crate on the axis (a standing
	// operative is hidden right behind it, seen again further out), a 60 cm barricade (below every drawn line).
	TArray<FBoxObstacle> Boxes;
	Boxes.Add({ FBox2D(FVector2D(500.f, -450.f), FVector2D(540.f, -100.f)), 300.f });
	Boxes.Add({ FBox2D(FVector2D(600.f, 100.f), FVector2D(640.f, 500.f)), 120.f });
	Boxes.Add({ FBox2D(FVector2D(350.f, -60.f), FVector2D(380.f, 20.f)), 155.f });
	Boxes.Add({ FBox2D(FVector2D(900.f, -60.f), FVector2D(930.f, 60.f)), 60.f });
	const TArray<FVisionConeRay> Rays = CastRays(Spec, [&Spec, &Boxes](float Angle, FVisionConeRay& Ray) { SampleBoxes(Spec, Boxes, Angle, Ray); });

	// Every grid point inside the cone: the drawn level = how many levels the exact 3D line test sees (points within 3 cm of
	// a box or of a level boundary are skipped - the boundary itself is a chord between rays).
	auto ExactLevel = [&Spec, &Boxes](const FVector2D& Point)
	{
		uint8 Level = 0;
		for (int32 Index = 0; Index < Spec.Levels.Num(); ++Index)
		{
			Level += SeesExactly(Spec, Boxes, Index, Point) ? 1 : 0;
		}
		return Level;
	};
	int32 Checked = 0;
	int32 Mismatches = 0;
	int32 Cut = 0;
	int32 Light = 0;
	int32 Full = 0;
	FString FirstMismatch;
	for (float X = 25.f; X < 2000.f; X += 37.f)
	{
		for (float Y = -1900.f; Y < 1900.f; Y += 41.f)
		{
			const FVector2D Point(X, Y);
			const float Angle = FMath::RadiansToDegrees(FMath::Atan2(Y, X));
			if (FMath::Abs(Angle) > Spec.HalfAngleDeg - 0.1f || Point.Size() > Spec.GetMaxRange() - 3.f)
			{
				continue;
			}
			bool bNearBox = false;
			for (const FBoxObstacle& Box : Boxes)
			{
				bNearBox |= Box.Box.ExpandBy(3.f).IsInside(Point);
			}
			const uint8 Exact = ExactLevel(Point);
			bool bStable = !bNearBox;
			for (const FVector2D& Offset : { FVector2D(3.f, 0.f), FVector2D(-3.f, 0.f), FVector2D(0.f, 3.f), FVector2D(0.f, -3.f) })
			{
				bStable &= ExactLevel(Point + Offset) == Exact;
			}
			if (!bStable)
			{
				continue;
			}
			++Checked;
			const uint8 Drawn = LevelAtPoint(Rays, Spec.HalfAngleDeg, Point);
			if (Drawn != Exact)
			{
				++Mismatches;
				if (FirstMismatch.IsEmpty())
				{
					FirstMismatch = FString::Printf(TEXT("(%.0f, %.0f) drawn %d exact %d"), X, Y, Drawn, Exact);
				}
			}
			Cut += Exact == 0 ? 1 : 0;
			Light += Exact == 1 ? 1 : 0;
			Full += Exact == 2 ? 1 : 0;
		}
	}
	TestTrue(FString::Printf(TEXT("enough points: %d (cut %d, light %d, full %d)"), Checked, Cut, Light, Full), Checked > 800 && Cut > 50 && Light > 50 && Full > 200);
	TestEqual(FString::Printf(TEXT("drawn level = exact sight test (first mismatch %s)"), *FirstMismatch), Mismatches, 0);
	// Spot checks of the designer's case.
	TestEqual(TEXT("behind the tall wall: cut"), LevelAtPoint(Rays, Spec.HalfAngleDeg, FVector2D(900.f, -500.f)), static_cast<uint8>(0));
	TestEqual(TEXT("hugging the 1.2 m wall: standing only (light)"), LevelAtPoint(Rays, Spec.HalfAngleDeg, FVector2D(700.f, 300.f)), static_cast<uint8>(1));
	TestEqual(TEXT("open ground near: full"), LevelAtPoint(Rays, Spec.HalfAngleDeg, FVector2D(800.f, 60.f)), static_cast<uint8>(2));
	TestEqual(TEXT("right behind the 1.55 m crate: cut"), LevelAtPoint(Rays, Spec.HalfAngleDeg, FVector2D(500.f, 0.f)), static_cast<uint8>(0));
	TestEqual(TEXT("far behind the 1.55 m crate: standing seen again (light)"), LevelAtPoint(Rays, Spec.HalfAngleDeg, FVector2D(1000.f, 0.f)), static_cast<uint8>(1));
	TestEqual(TEXT("open ground beyond the crouched range: light"), LevelAtPoint(Rays, Spec.HalfAngleDeg, FVector2D(1700.f, 0.f)), static_cast<uint8>(1));
	TestEqual(TEXT("outside the field of view: nothing"), LevelAtPoint(Rays, Spec.HalfAngleDeg, FVector2D(100.f, 800.f)), static_cast<uint8>(0));

	// The mesh: full and light tones and an outline; nothing drawn behind the tall wall or outside the sector.
	const FVisionConeShape Shape = BuildShape(Rays, Spec.Levels.Num(), Spec.HalfAngleDeg, 8.f, 0.f, 0.5f);
	TestTrue(TEXT("full tone"), Shape.Full.Vertices.Num() > 0 && Shape.Full.Triangles.Num() % 3 == 0);
	TestTrue(TEXT("light tone"), Shape.Light.Vertices.Num() > 0 && Shape.Light.Triangles.Num() % 3 == 0);
	TestTrue(TEXT("outline"), Shape.Outline.Vertices.Num() > 0);
	bool bInside = true;
	for (const FVisionConeMesh* Mesh : { &Shape.Full, &Shape.Light })
	{
		for (const FVector& Vertex : Mesh->Vertices)
		{
			const float Angle = FMath::RadiansToDegrees(FMath::Atan2(Vertex.Y, Vertex.X));
			bInside &= FVector2D(Vertex.X, Vertex.Y).Size() <= Spec.GetMaxRange() + 0.5f && (Vertex.IsNearlyZero(0.01f) || FMath::Abs(Angle) <= Spec.HalfAngleDeg + 0.01f);
			// The tall wall's shadow (between its corners' angles, behind its front face x = 500): no vertex there.
			bInside &= !(Angle > -40.f && Angle < -13.f && Vertex.X > 500.5f);
		}
	}
	TestTrue(TEXT("fill vertices inside the sector, none in the tall wall's shadow"), bInside);
	bool bIndices = true;
	for (const int32 Index : Shape.Full.Triangles)
	{
		bIndices &= Shape.Full.Vertices.IsValidIndex(Index);
	}
	TestTrue(TEXT("valid triangle indices"), bIndices);

	// No obstacle: one triangle per ray pair and level, the plain sector's area.
	const TArray<FVisionConeRay> OpenRays = CastRays(Spec, [&Spec](float Angle, FVisionConeRay& Ray) { SampleBoxes(Spec, {}, Angle, Ray); });
	const FVisionConeShape OpenShape = BuildShape(OpenRays, Spec.Levels.Num(), Spec.HalfAngleDeg, 8.f);
	TestEqual(TEXT("open cone: base rays only"), OpenRays.Num(), 61);
	TestEqual(TEXT("open cone: one fan triangle per ray pair (both windings)"), OpenShape.Full.Triangles.Num(), 60 * 6);
	TestEqual(TEXT("open cone: one light quad per ray pair (both windings)"), OpenShape.Light.Triangles.Num(), 60 * 12);
	return true;
}
