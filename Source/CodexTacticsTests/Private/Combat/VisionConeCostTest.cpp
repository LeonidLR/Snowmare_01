// CodexTactics.Combat.VisionCone.Cost.* - fix/cone-rebuild-cost (2026-11-08): a single rebuild of a 120 m cone cost ~2x its 6 ms budget
// (SightConditionsSmoke). UE-only, no Godot reference. The cheaper rebuild must draw the same cone:
//  - ClipExact: the merged layer's cut (ClipToLayer, prepared members + skipped members that can never win) gives bit for bit the spans of the
//    2026-11-07 algorithm (kept below as the reference) on squads of three / four 120 m cones among walls;
//  - ShapeParity: the cheaper refinement (MinRefineGapCm: edges placed to ~20 cm where they are drawn instead of 0.03 deg everywhere) draws
//    the same visible area as the old fan (< 2 %, both tones) for fixed scenes - clear day 120 m, a street between walls (turned too), low
//    walls round a crouched man (the light tone), a roof 9 m up (210 m), a blizzard 60 m, a night 42 m - and stays as close to the exact cone
//    (the same rule traced ~8x finer) as before, with no new holes / shards (wrong points off every edge of the exact cone).

#include "Combat/VisionConeVisibility.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace VisionConeCostTest
{
	FVector2D Direction(float AngleDeg)
	{
		return FVector2D(FMath::Cos(FMath::DegreesToRadians(AngleDeg)), FMath::Sin(FMath::DegreesToRadians(AngleDeg)));
	}

	/** A box on the ground (world, planar), its top above the observer's feet. */
	struct FConeBox
	{
		FVector2D Min;
		FVector2D Max;
		float TopCm;
	};

	/** A synthetic scene sampled like UVisionConeComponent::SampleRay on flat ground: the high trace's blocker, up to MaxLow low obstacles. */
	struct FConeScene
	{
		TArray<FConeBox> Boxes;
		int32 MaxLow = 3;
		/** A roof under the observer (0 = none): his feet this high over the ground, the roof's edge this far out along every ray. */
		float PerchHeightCm = 0.f;
		float PerchEdgeCm = 0.f;

		/** Slab test of a planar ray against a box: entry / exit distances and the faces' normals. */
		static bool Hit(const FConeBox& Box, const FVector2D& Origin, const FVector2D& Dir, float& OutNear, float& OutFar, FVector2D& OutNearNormal, FVector2D& OutFarNormal)
		{
			float Near = -1.e9f;
			float Far = 1.e9f;
			for (int32 Axis = 0; Axis < 2; ++Axis)
			{
				const float O = Axis == 0 ? Origin.X : Origin.Y;
				const float D = Axis == 0 ? Dir.X : Dir.Y;
				const float Lo = Axis == 0 ? Box.Min.X : Box.Min.Y;
				const float Hi = Axis == 0 ? Box.Max.X : Box.Max.Y;
				if (FMath::Abs(D) < 1.e-7f)
				{
					if (O < Lo || O > Hi)
					{
						return false;
					}
					continue;
				}
				float T0 = (Lo - O) / D;
				float T1 = (Hi - O) / D;
				FVector2D N0 = Axis == 0 ? FVector2D(-1.f, 0.f) : FVector2D(0.f, -1.f);
				FVector2D N1 = -N0;
				if (T0 > T1)
				{
					Swap(T0, T1);
					Swap(N0, N1);
				}
				if (T0 > Near)
				{
					Near = T0;
					OutNearNormal = N0;
				}
				if (T1 < Far)
				{
					Far = T1;
					OutFarNormal = N1;
				}
			}
			if (Near > Far || Far <= 0.f || Near <= 0.f)
			{
				return false;
			}
			OutNear = Near;
			OutFar = Far;
			return true;
		}

		void Sample(const FVisionConeSightSpec& Spec, const FVector2D& Origin, float YawDeg, float AngleDeg, FVisionConeRay& Ray) const
		{
			const FVector2D Dir = Direction(YawDeg + AngleDeg);
			const float Range = Spec.GetMaxRange();
			if (PerchHeightCm > 0.f)
			{
				// A flat roof round him (SamplePerch): the ground PerchHeightCm below, the roof hides what stands under it.
				Ray.GroundCm = -PerchHeightCm;
				Ray.Obstacles.Add({ 0.f, PerchEdgeCm, 0.f, FVector2D::ZeroVector, FVector2D(1.f, 0.f), true });
			}
			const float High = Spec.GetHighTraceHeight(Ray.GroundCm);
			const float Low = Spec.GetLowTraceHeight(Ray.GroundCm);
			float Wall = Range;
			int32 WallIndex = INDEX_NONE;
			FVector2D WallNormal = FVector2D::ZeroVector;
			struct FLow
			{
				float Near;
				float Far;
				float Top;
				FVector2D NearNormal;
				FVector2D FarNormal;
			};
			TArray<FLow, TInlineAllocator<16>> Lows;
			for (int32 Index = 0; Index < Boxes.Num(); ++Index)
			{
				float Near = 0.f;
				float Far = 0.f;
				FVector2D NearNormal;
				FVector2D FarNormal;
				if (!Hit(Boxes[Index], Origin, Dir, Near, Far, NearNormal, FarNormal) || Near >= Range)
				{
					continue;
				}
				const float Top = Boxes[Index].TopCm - PerchHeightCm; // box heights are over the ground, the rule's over his feet
				// Normals in the cone frame (rotated by -yaw).
				const float Sin = FMath::Sin(FMath::DegreesToRadians(-YawDeg));
				const float Cos = FMath::Cos(FMath::DegreesToRadians(-YawDeg));
				auto Local = [Sin, Cos](const FVector2D& N) { return FVector2D(N.X * Cos - N.Y * Sin, N.X * Sin + N.Y * Cos); };
				if (Top >= High)
				{
					if (Near < Wall)
					{
						Wall = Near;
						WallIndex = Index;
						WallNormal = Local(NearNormal);
					}
				}
				else if (Top >= Low)
				{
					Lows.Add({ Near, Far, Top, Local(NearNormal), Local(FarNormal) });
				}
			}
			if (WallIndex != INDEX_NONE)
			{
				Ray.BlockerKey = static_cast<uint64>(WallIndex) + 1;
				Ray.BlockerNormal = WallNormal;
				Ray.Obstacles.Add({ Wall, Wall, VisionConeVisibility::FullHeightCm, WallNormal, WallNormal });
			}
			Lows.Sort([](const FLow& A, const FLow& B) { return A.Near < B.Near; });
			int32 Count = 0;
			for (const FLow& Obstacle : Lows)
			{
				if (Count >= MaxLow || Obstacle.Near >= Wall - 1.f)
				{
					break;
				}
				Ray.Obstacles.Add({ Obstacle.Near, FMath::Clamp(Obstacle.Far, Obstacle.Near, Wall), Obstacle.Top, Obstacle.NearNormal, Obstacle.FarNormal });
				++Count;
			}
		}
	};

	/** The component's spec of a squad cone (UVisionConeComponent defaults): two levels, adaptive world-grid rays, slivers out. */
	FVisionConeSightSpec ConeSpec(float RangeCm, float HalfAngleDeg, float EyeCm = 160.f)
	{
		FVisionConeSightSpec Spec = VisionConeVisibility::MakeOperativeSightSpec(EyeCm, RangeCm, 2);
		Spec.HalfAngleDeg = HalfAngleDeg;
		Spec.RayStepDeg = 1.5f;
		Spec.MaxRefineDepth = 6;
		Spec.MaxRays = 320;
		Spec.RaySpacingCm = 200.f;
		Spec.MinRayStepDeg = 0.5f;
		Spec.bWorldGrid = true;
		Spec.SliverMinWidthCm = 60.f;
		Spec.SliverFromCm = 1500.f;
		return Spec;
	}

	/** The merged-layer cut of 2026-11-07 (bug #38), verbatim: the reference ClipToLayer must reproduce. */
	int32 ReferenceClipToLayer(TArray<FVisionConeRay>& Rays, const FVector2D& Origin, float YawDeg, int32 Priority,
		TArrayView<const FVisionConeLayerMember> Others, float StepCm)
	{
		int32 Cut = 0;
		if (Others.Num() == 0)
		{
			return 0;
		}
		StepCm = FMath::Max(StepCm, 10.f);
		TArray<const FVisionConeLayerMember*, TInlineAllocator<4>> Near;
		for (FVisionConeRay& Ray : Rays)
		{
			const float End = Ray.Spans.Num() > 0 ? Ray.Spans.Last().EndCm : 0.f;
			if (End <= 0.f)
			{
				continue;
			}
			const FVector2D Dir = Direction(YawDeg + Ray.AngleDeg);
			Near.Reset();
			for (const FVisionConeLayerMember& Other : Others)
			{
				if (!Other.Rays.IsValid())
				{
					continue;
				}
				const float Along = FMath::Clamp(static_cast<float>(FVector2D::DotProduct(Other.Origin - Origin, Dir)), 0.f, End);
				if (FVector2D::DistSquared(Origin + Dir * Along, Other.Origin) <= FMath::Square(Other.MaxRangeCm))
				{
					Near.Add(&Other);
				}
			}
			if (Near.Num() == 0)
			{
				continue;
			}
			TArray<FVisionConeSpan, TInlineAllocator<4>> Kept;
			for (const FVisionConeSpan& Span : Ray.Spans)
			{
				auto Keeps = [&Near, &Origin, &Dir, &Span, Priority](float Distance)
				{
					const FVector2D Point = Origin + Dir * Distance;
					for (const FVisionConeLayerMember* Other : Near)
					{
						const uint8 Level = Other->LevelAt(Point);
						if (Level > Span.Level || (Level == Span.Level && Other->Priority < Priority))
						{
							return false;
						}
					}
					return true;
				};
				const float First = FMath::Min(Span.StartCm + 0.5f, Span.EndCm);
				bool bIn = Keeps(First);
				float RunStart = Span.StartCm;
				for (float Distance = Span.StartCm; Distance < Span.EndCm;)
				{
					const float Next = FMath::Min(Distance + StepCm, Span.EndCm);
					const bool bNext = Keeps(FMath::Max(Next - 0.5f, Distance));
					if (bNext != bIn)
					{
						float Low = Distance;
						float High = Next;
						for (int32 Step = 0; Step < 7; ++Step)
						{
							const float Mid = 0.5f * (Low + High);
							(Keeps(Mid) == bIn ? Low : High) = Mid;
						}
						const float Crossing = 0.5f * (Low + High);
						if (bIn)
						{
							Kept.Add({ RunStart, Crossing, Span.Level });
						}
						else
						{
							RunStart = Crossing;
						}
						bIn = bNext;
					}
					Distance = Next;
				}
				if (bIn)
				{
					Kept.Add({ RunStart, Span.EndCm, Span.Level });
				}
			}
			Kept.RemoveAll([](const FVisionConeSpan& Span) { return Span.EndCm - Span.StartCm < VisionConeVisibility::MinSpanCm; });
			bool bSame = Kept.Num() == Ray.Spans.Num();
			for (int32 Index = 0; bSame && Index < Kept.Num(); ++Index)
			{
				bSame = FMath::IsNearlyEqual(Kept[Index].StartCm, Ray.Spans[Index].StartCm, 0.1f) && FMath::IsNearlyEqual(Kept[Index].EndCm, Ray.Spans[Index].EndCm, 0.1f);
			}
			if (!bSame)
			{
				++Cut;
				Ray.Spans = MoveTemp(Kept);
			}
		}
		return Cut;
	}

	/** A street of walls and low barricades round the squad (the Depot's yard in miniature, 120 m long). */
	FConeScene StreetScene()
	{
		FConeScene Scene;
		// Two building rows along the street (3 - 9 m tall: full walls), with gaps (side alleys).
		for (int32 Block = 0; Block < 8; ++Block)
		{
			const float X0 = 600.f + Block * 1500.f;
			Scene.Boxes.Add({ FVector2D(X0, 1200.f), FVector2D(X0 + 1100.f, 2400.f), 600.f + 100.f * (Block % 3) });
			Scene.Boxes.Add({ FVector2D(X0 + 300.f, -2600.f), FVector2D(X0 + 1300.f, -1400.f), 900.f });
		}
		// Low barricades (the light tone behind them) and crates in the street.
		Scene.Boxes.Add({ FVector2D(1500.f, -300.f), FVector2D(1560.f, 400.f), 120.f });
		Scene.Boxes.Add({ FVector2D(3000.f, 200.f), FVector2D(3080.f, 900.f), 110.f });
		Scene.Boxes.Add({ FVector2D(5200.f, -800.f), FVector2D(5320.f, -650.f), 155.f });
		Scene.Boxes.Add({ FVector2D(7000.f, -200.f), FVector2D(7400.f, 200.f), 300.f });
		// A picket fence across the far street (posts).
		for (int32 Post = 0; Post < 12; ++Post)
		{
			const float Y = -1200.f + Post * 220.f;
			Scene.Boxes.Add({ FVector2D(9000.f, Y), FVector2D(9040.f, Y + 40.f), 250.f });
		}
		return Scene;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisionConeCostClipExactTest, "CodexTactics.Combat.VisionCone.Cost.ClipExact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVisionConeCostClipExactTest::RunTest(const FString&)
{
	using namespace VisionConeCostTest;
	const FConeScene Street = StreetScene();
	FConeScene Open;
	struct FCone
	{
		FVector2D Origin;
		float Yaw;
		int32 Priority;
		float RangeCm;
		float HalfAngleDeg;
		float EyeCm;
	};
	struct FCase
	{
		const TCHAR* Name;
		const FConeScene* Scene;
		TArray<FCone> Cones;
	};
	const FCase Cases[] = {
		{ TEXT("street, three standing 120 m"), &Street, { { FVector2D(0.f, 0.f), 0.f, 11, 12000.f, 70.f, 160.f }, { FVector2D(-200.f, 250.f), 12.f, 7, 12000.f, 70.f, 160.f },
			{ FVector2D(-250.f, -300.f), -20.f, 19, 12000.f, 70.f, 160.f } } },
		{ TEXT("street, crouched + standing + a peek"), &Street, { { FVector2D(100.f, 0.f), 5.f, 3, 12000.f, 55.f, 95.f }, { FVector2D(-300.f, 100.f), -8.f, 4, 12000.f, 70.f, 160.f },
			{ FVector2D(-100.f, -500.f), 40.f, 2, 12000.f, 45.f, 160.f }, { FVector2D(-600.f, 0.f), 180.f, 9, 6000.f, 70.f, 160.f } } },
		{ TEXT("open, blizzard 60 m"), &Open, { { FVector2D(0.f, 0.f), 0.f, 5, 6000.f, 70.f, 160.f }, { FVector2D(300.f, 300.f), 30.f, 6, 6000.f, 70.f, 160.f },
			{ FVector2D(-300.f, 200.f), -60.f, 1, 6000.f, 70.f, 160.f } } },
	};
	int32 Rays = 0;
	int32 Differ = 0;
	int32 CutRef = 0;
	int32 CutNew = 0;
	for (const FCase& Case : Cases)
	{
		TArray<TSharedPtr<TArray<FVisionConeRay>, ESPMode::ThreadSafe>> Sources;
		TArray<FVisionConeSightSpec> Specs;
		for (const FCone& Cone : Case.Cones)
		{
			FVisionConeSightSpec Spec = ConeSpec(Cone.RangeCm, Cone.HalfAngleDeg, Cone.EyeCm);
			Spec.GridYawDeg = Cone.Yaw;
			const FConeScene* Scene = Case.Scene;
			Sources.Add(MakeShared<TArray<FVisionConeRay>, ESPMode::ThreadSafe>(VisionConeVisibility::CastRays(Spec, [Scene, &Spec, &Cone](float Angle, FVisionConeRay& Ray)
			{
				Scene->Sample(Spec, Cone.Origin, Cone.Yaw, Angle, Ray);
			})));
			Specs.Add(Spec);
		}
		for (int32 Index = 0; Index < Case.Cones.Num(); ++Index)
		{
			TArray<FVisionConeLayerMember> Others;
			for (int32 Other = 0; Other < Case.Cones.Num(); ++Other)
			{
				if (Other != Index)
				{
					FVisionConeLayerMember& Member = Others.AddDefaulted_GetRef();
					Member.Rays = Sources[Other];
					Member.HalfAngleDeg = Specs[Other].HalfAngleDeg;
					Member.MaxRangeCm = Specs[Other].GetMaxRange();
					Member.Origin = Case.Cones[Other].Origin;
					Member.YawDeg = Case.Cones[Other].Yaw;
					Member.Priority = Case.Cones[Other].Priority;
				}
			}
			TArray<FVisionConeRay> Reference = *Sources[Index];
			TArray<FVisionConeRay> Fast = *Sources[Index];
			CutRef += ReferenceClipToLayer(Reference, Case.Cones[Index].Origin, Case.Cones[Index].Yaw, Case.Cones[Index].Priority, Others, 150.f);
			CutNew += VisionConeVisibility::ClipToLayer(Fast, Case.Cones[Index].Origin, Case.Cones[Index].Yaw, Case.Cones[Index].Priority, Others, 150.f);
			for (int32 Ray = 0; Ray < Reference.Num(); ++Ray)
			{
				++Rays;
				bool bSame = Reference[Ray].Spans.Num() == Fast[Ray].Spans.Num();
				for (int32 Span = 0; bSame && Span < Reference[Ray].Spans.Num(); ++Span)
				{
					const FVisionConeSpan& A = Reference[Ray].Spans[Span];
					const FVisionConeSpan& B = Fast[Ray].Spans[Span];
					bSame = A.StartCm == B.StartCm && A.EndCm == B.EndCm && A.Level == B.Level;
				}
				if (!bSame)
				{
					++Differ;
					if (Differ <= 3)
					{
						AddInfo(FString::Printf(TEXT("%s, cone %d, ray %d (%.3f deg): the spans differ"), Case.Name, Index, Ray, Reference[Ray].AngleDeg));
					}
				}
			}
		}
	}
	TestTrue(*FString::Printf(TEXT("the layers were cut (%d / %d rays)"), CutRef, CutNew), CutRef > 50 && CutRef == CutNew);
	TestEqual(*FString::Printf(TEXT("the fast cut = the 2026-11-07 cut, bit for bit (%d rays)"), Rays), Differ, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisionConeCostShapeParityTest, "CodexTactics.Combat.VisionCone.Cost.ShapeParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVisionConeCostShapeParityTest::RunTest(const FString&)
{
	using namespace VisionConeCostTest;
	const FConeScene Street = StreetScene();
	FConeScene Open;
	FConeScene Roof = StreetScene();
	Roof.PerchHeightCm = 900.f;
	Roof.PerchEdgeCm = 400.f;
	Roof.MaxLow = 12;
	// Low walls right round a crouched man (the light tone behind them) and a few tall ones.
	FConeScene LowWalls;
	for (int32 Index = 0; Index < 10; ++Index)
	{
		const float Angle = -60.f + Index * 13.f;
		const FVector2D At = Direction(Angle) * (300.f + 250.f * (Index % 4));
		LowWalls.Boxes.Add({ At - FVector2D(40.f, 120.f), At + FVector2D(40.f, 120.f), Index % 4 == 0 ? 400.f : 70.f + 6.f * Index });
	}
	struct FCase
	{
		const TCHAR* Name;
		const FConeScene* Scene;
		float RangeCm;
		float HalfAngleDeg;
		float EyeCm;
		float YawDeg;
	};
	const FCase Cases[] = {
		{ TEXT("clear day 120 m, open"), &Open, 12000.f, 70.f, 160.f, 3.3f },
		{ TEXT("clear day 120 m, a street with walls"), &Street, 12000.f, 70.f, 160.f, 1.7f },
		{ TEXT("clear day 120 m, the street turned"), &Street, 12000.f, 70.f, 160.f, 17.f },
		{ TEXT("crouched behind low walls, 120 m"), &LowWalls, 12000.f, 55.f, 95.f, 0.4f },
		{ TEXT("a roof 9 m over the street, 210 m"), &Roof, 21000.f, 55.f, 95.f, 2.2f },
		{ TEXT("blizzard 60 m, the street"), &Street, 6000.f, 70.f, 160.f, -4.f },
		{ TEXT("night 42 m, the street"), &Street, 4200.f, 70.f, 160.f, 8.f },
	};
	for (const FCase& Case : Cases)
	{
		FVisionConeSightSpec Old = ConeSpec(Case.RangeCm, Case.HalfAngleDeg, Case.EyeCm);
		Old.GridYawDeg = Case.YawDeg;
		FVisionConeSightSpec New = Old;
		New.MinRefineGapCm = VisionConeVisibility::DefaultMinRefineGapCm;
		// The truth: the same rule traced ~8x finer, refined far deeper, no ray cap.
		FVisionConeSightSpec Truth = Old;
		Truth.RaySpacingCm = 25.f;
		Truth.MinRayStepDeg = 0.05f;
		Truth.MaxRays = 20000;
		Truth.MaxRefineDepth = 8;
		Truth.MinRefineGapDeg = 0.002f;
		const FConeScene* Scene = Case.Scene;
		auto Cast = [Scene, &Case](const FVisionConeSightSpec& Spec, int32& OutSamples)
		{
			return VisionConeVisibility::CastRays(Spec, [Scene, &Spec, &Case](float Angle, FVisionConeRay& Ray)
			{
				Scene->Sample(Spec, FVector2D::ZeroVector, Case.YawDeg, Angle, Ray);
			}, &OutSamples);
		};
		int32 OldRays = 0;
		int32 NewRays = 0;
		int32 TruthRays = 0;
		const TArray<FVisionConeRay> A = Cast(Old, OldRays);
		const TArray<FVisionConeRay> B = Cast(New, NewRays);
		const TArray<FVisionConeRay> T = Cast(Truth, TruthRays);
		// A 30 cm grid over the cone: the area seen at each level; the points each fan draws otherwise than the truth, and how many of those
		// lie off every edge of the truth (no other level within 60 cm) - a hole / shard rather than an edge placed a little off.
		const float Cell = 30.f;
		const float Reach = Case.RangeCm + Cell;
		int64 AreaOld[3] = { 0, 0, 0 };
		int64 AreaNew[3] = { 0, 0, 0 };
		int64 AreaTruth[3] = { 0, 0, 0 };
		int64 OldWrong = 0;
		int64 NewWrong = 0;
		int64 OldOffEdge = 0;
		int64 NewOffEdge = 0;
		int64 Apart = 0;
		auto TruthAt = [&T, &Truth](const FVector2D& Point) { return VisionConeVisibility::LevelAtPoint(T, Truth.HalfAngleDeg, Point); };
		auto OnTruthEdge = [&TruthAt, Cell](const FVector2D& Point, uint8 Level)
		{
			for (int32 DX = -2; DX <= 2; ++DX)
			{
				for (int32 DY = -2; DY <= 2; ++DY)
				{
					if (TruthAt(Point + FVector2D(DX * Cell, DY * Cell)) != Level)
					{
						return true;
					}
				}
			}
			return false;
		};
		for (float X = -Reach; X <= Reach; X += Cell)
		{
			for (float Y = -Reach; Y <= Reach; Y += Cell)
			{
				const FVector2D Point(X, Y);
				if (Point.SizeSquared() > FMath::Square(Reach))
				{
					continue;
				}
				const uint8 LevelOld = VisionConeVisibility::LevelAtPoint(A, Old.HalfAngleDeg, Point);
				const uint8 LevelNew = VisionConeVisibility::LevelAtPoint(B, New.HalfAngleDeg, Point);
				const uint8 LevelTruth = TruthAt(Point);
				for (uint8 Level = 1; Level <= 2; ++Level)
				{
					AreaOld[Level] += LevelOld >= Level ? 1 : 0;
					AreaNew[Level] += LevelNew >= Level ? 1 : 0;
					AreaTruth[Level] += LevelTruth >= Level ? 1 : 0;
				}
				Apart += LevelOld != LevelNew ? 1 : 0;
				if (LevelOld == LevelTruth && LevelNew == LevelTruth)
				{
					continue;
				}
				const bool bEdge = OnTruthEdge(Point, LevelTruth);
				if (LevelOld != LevelTruth)
				{
					++OldWrong;
					OldOffEdge += bEdge ? 0 : 1;
				}
				if (LevelNew != LevelTruth)
				{
					++NewWrong;
					NewOffEdge += bEdge ? 0 : 1;
				}
			}
		}
		auto Gap = [](int64 X, int64 Y) { return FMath::Abs(static_cast<double>(X - Y)) / FMath::Max<double>(1.0, static_cast<double>(Y)); };
		const double Seen = FMath::Max<double>(1.0, static_cast<double>(AreaTruth[1]));
		const FString Summary = FString::Printf(TEXT("%s: rays %d -> %d (truth %d); new vs old: seen area %.2f%% / full tone %.2f%% apart, %.2f%% of the points; ")
			TEXT("vs the truth: seen area old %.2f%% new %.2f%%, points drawn wrong old %.2f%% new %.2f%%, off every edge old %lld new %lld"),
			Case.Name, OldRays, NewRays, TruthRays, 100.0 * Gap(AreaNew[1], AreaOld[1]), 100.0 * Gap(AreaNew[2], AreaOld[2]), 100.0 * Apart / Seen,
			100.0 * Gap(AreaOld[1], AreaTruth[1]), 100.0 * Gap(AreaNew[1], AreaTruth[1]), 100.0 * OldWrong / Seen, 100.0 * NewWrong / Seen, OldOffEdge, NewOffEdge);
		AddInfo(Summary);
		TestTrue(*FString::Printf(TEXT("the same visible area as before (< 2 %%) - %s"), *Summary),
			Gap(AreaNew[1], AreaOld[1]) < 0.02 && Gap(AreaNew[2], AreaOld[2]) < 0.02);
		// Within the 2-3 % band of the exact cone (the old fan was 2.5 % off it on the street: its 320 rays ran out there).
		TestTrue(*FString::Printf(TEXT("within 3 %% of the exact cone (or no worse than before) - %s"), *Summary),
			Gap(AreaNew[1], AreaTruth[1]) < FMath::Max(0.03, Gap(AreaOld[1], AreaTruth[1])) && NewWrong <= FMath::Max<int64>(OldWrong, static_cast<int64>(Seen * 0.03)));
		TestTrue(*FString::Printf(TEXT("no new holes / shards - %s"), *Summary), NewOffEdge <= OldOffEdge + 2);
		TestTrue(*FString::Printf(TEXT("never more rays - %s"), *Summary), NewRays <= OldRays);
	}
	return true;
}

#endif
