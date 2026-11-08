#include "Combat/VisionConeVisibility.h"

#include "Combat/SightRules.h"

namespace
{
	FVector2D Direction(float AngleDeg)
	{
		const float Radians = FMath::DegreesToRadians(AngleDeg);
		return FVector2D(FMath::Cos(Radians), FMath::Sin(Radians));
	}

	FVector PointAt(float AngleDeg, float DistanceCm, float HeightCm)
	{
		const FVector2D Dir = Direction(AngleDeg) * DistanceCm;
		return FVector(Dir.X, Dir.Y, HeightCm);
	}

	void AddVisibilityDoubleSided(TArray<int32>& Triangles, int32 A, int32 B, int32 C)
	{
		Triangles.Append({ A, B, C, A, C, B });
	}

	/**
	 * Quad (or a triangle when both starts sit on the apex) between two angles: [Start, End] on each side, side A at HeightA, side B
	 * at HeightB (each ray's ground: an elevated observer's spans lie on the ground below him).
	 */
	void AddSpanQuad(FVisionConeMesh& Mesh, float AngleA, float StartA, float EndA, float AngleB, float StartB, float EndB, float HeightA, float HeightB)
	{
		const int32 Base = Mesh.Vertices.Num();
		if (StartA < 0.01f && StartB < 0.01f)
		{
			Mesh.Vertices.Append({ FVector(0.f, 0.f, 0.5f * (HeightA + HeightB)), PointAt(AngleA, EndA, HeightA), PointAt(AngleB, EndB, HeightB) });
			AddVisibilityDoubleSided(Mesh.Triangles, Base, Base + 1, Base + 2);
			return;
		}
		Mesh.Vertices.Append({ PointAt(AngleA, StartA, HeightA), PointAt(AngleA, EndA, HeightA), PointAt(AngleB, EndB, HeightB),
			PointAt(AngleB, StartB, HeightB) });
		AddVisibilityDoubleSided(Mesh.Triangles, Base, Base + 1, Base + 2);
		AddVisibilityDoubleSided(Mesh.Triangles, Base, Base + 2, Base + 3);
	}

	float FarEnd(const FVisionConeRay& Ray)
	{
		return Ray.Spans.Num() > 0 ? Ray.Spans.Last().EndCm : 0.f;
	}

	float Cross(const FVector2D& A, const FVector2D& B)
	{
		return A.X * B.Y - A.Y * B.X;
	}

	/**
	 * Closed polyline (planar X / Y, each point at its own Z) as a band WidthCm wide on its inner side (mitred corners, clamped). An edge
	 * whose ends lie more than MaxZJumpCm apart in height (a perch's ground jump: the band would stand up as a shard) or whose outside
	 * Hidden reports covered (a merged layer's inside) is left out; its neighbours end square.
	 */
	void BuildClosedBand(const TArray<FVector>& Points3, float WidthCm, FVisionConeMesh& Mesh, float MaxZJumpCm = TNumericLimits<float>::Max(),
		const TFunction<bool(const FVector2D&)>* Hidden = nullptr)
	{
		TArray<FVector2D> Points;
		Points.Reserve(Points3.Num());
		for (const FVector& Point : Points3)
		{
			Points.Add(FVector2D(Point.X, Point.Y));
		}
		const int32 Count = Points.Num();
		if (Count < 3 || WidthCm <= 0.f)
		{
			return;
		}
		float Area = 0.f;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Area += Cross(Points[Index], Points[(Index + 1) % Count]);
		}
		if (FMath::Abs(Area) < 1.f)
		{
			return;
		}
		// Counter-clockwise (positive area): the inside lies to the left of every edge.
		const float Side = Area > 0.f ? 1.f : -1.f;
		auto InwardNormal = [&Points, Count, Side](int32 From)
		{
			const FVector2D Edge = (Points[(From + 1) % Count] - Points[From]).GetSafeNormal();
			return FVector2D(-Edge.Y, Edge.X) * Side;
		};
		TArray<bool> Skip;
		Skip.SetNum(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const int32 Next = (Index + 1) % Count;
			Skip[Index] = FMath::Abs(Points3[Index].Z - Points3[Next].Z) > MaxZJumpCm || Points[Index].Equals(Points[Next], 0.01f);
			if (!Skip[Index] && Hidden && *Hidden)
			{
				const FVector2D Outside = 0.5f * (Points[Index] + Points[Next]) - InwardNormal(Index) * 30.f;
				Skip[Index] = (*Hidden)(Outside);
			}
		}
		TArray<FVector2D> Inner;
		Inner.SetNum(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const int32 Previous = (Index + Count - 1) % Count;
			const FVector2D N0 = InwardNormal(Previous);
			const FVector2D N1 = InwardNormal(Index);
			if (Skip[Previous] != Skip[Index])
			{
				Inner[Index] = Points[Index] + (Skip[Index] ? N0 : N1) * WidthCm; // one edge kept: a square end
				continue;
			}
			FVector2D Mitre = N0 + N1;
			Mitre = Mitre.SizeSquared() < 1.e-4f ? N1 : Mitre.GetSafeNormal();
			const float Length = FMath::Min(WidthCm / FMath::Max(FVector2D::DotProduct(Mitre, N1), 0.34f), WidthCm * 3.f);
			Inner[Index] = Points[Index] + Mitre * Length;
		}
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (Skip[Index])
			{
				continue;
			}
			const int32 Next = (Index + 1) % Count;
			const int32 Base = Mesh.Vertices.Num();
			Mesh.Vertices.Append({ FVector(Points[Index], Points3[Index].Z), FVector(Points[Next], Points3[Next].Z), FVector(Inner[Next], Points3[Next].Z),
				FVector(Inner[Index], Points3[Index].Z) });
			AddVisibilityDoubleSided(Mesh.Triangles, Base, Base + 1, Base + 2);
			AddVisibilityDoubleSided(Mesh.Triangles, Base, Base + 2, Base + 3);
		}
	}

	struct FCaster
	{
		const FVisionConeSightSpec& Spec;
		TFunctionRef<void(float, FVisionConeRay&)> Sample;
		int32 Samples = 0;
		/** This gap's share of the rays (CastRays: the rays left shared out among the gaps that need refining). */
		int32 GapLimit = TNumericLimits<int32>::Max();

		FVisionConeRay Cast(float AngleDeg)
		{
			FVisionConeRay Ray;
			Ray.AngleDeg = AngleDeg;
			Sample(AngleDeg, Ray);
			++Samples;
			VisionConeVisibility::ClassifyRay(Spec, Ray);
			return Ray;
		}

		/** Bisects between A and B while they disagree; the rays found go to Out in angle order. */
		void Refine(const FVisionConeRay& A, const FVisionConeRay& B, int32 Depth, TArray<FVisionConeRay>& Out)
		{
			if (Depth <= 0 || Samples >= Spec.MaxRays || Samples >= GapLimit || B.AngleDeg - A.AngleDeg < 2.f * FMath::Max(Spec.MinRefineGapDeg, 1.e-4f)
				|| !VisionConeVisibility::NeedsRefine(Spec, A, B))
			{
				return;
			}
			// The arc that matters: at the farther drawn end of the two rays - or, when another object blocks each of them, at the range: the
			// gap between two blockers may open a view out to the range that neither ray shows (VisionHeightSmoke's ruin roof, 2026-11-08).
			const float ArcAtCm = A.BlockerKey != B.BlockerKey ? Spec.GetMaxRange() : FMath::Max(FarEnd(A), FarEnd(B));
			if (Spec.MinRefineGapCm > 0.f && FMath::DegreesToRadians(B.AngleDeg - A.AngleDeg) * ArcAtCm < 2.f * Spec.MinRefineGapCm)
			{
				return;
			}
			const FVisionConeRay Mid = Cast((A.AngleDeg + B.AngleDeg) * 0.5f);
			Refine(A, Mid, Depth - 1, Out);
			Out.Add(Mid);
			Refine(Mid, B, Depth - 1, Out);
		}
	};

	/** The height a ray's spans are drawn at: its ground, never above the observer's feet (bug #38: nothing reaches upward). */
	float DrawGround(const FVisionConeRay& Ray)
	{
		return FMath::Min(Ray.GroundCm, 0.f);
	}

	/**
	 * Every drawn quad of a fan, as the mesh draws it: joinable neighbours span by span (quads between their hit points), others each up
	 * to the middle angle. Emit(Level, AngleA, StartA, EndA, AngleB, StartB, EndB, GroundA, GroundB).
	 */
	template <typename FEmit>
	void ForEachQuad(const TArray<FVisionConeRay>& Rays, FEmit&& Emit)
	{
		for (int32 Index = 0; Index + 1 < Rays.Num(); ++Index)
		{
			const FVisionConeRay& A = Rays[Index];
			const FVisionConeRay& B = Rays[Index + 1];
			if (B.AngleDeg - A.AngleDeg <= 1.e-5f)
			{
				continue;
			}
			const float GroundA = DrawGround(A);
			const float GroundB = DrawGround(B);
			if (VisionConeVisibility::IsJoinable(A, B))
			{
				for (int32 Span = 0; Span < A.Spans.Num(); ++Span)
				{
					Emit(A.Spans[Span].Level, A.AngleDeg, A.Spans[Span].StartCm, A.Spans[Span].EndCm, B.AngleDeg, B.Spans[Span].StartCm, B.Spans[Span].EndCm,
						GroundA, GroundB);
				}
				continue;
			}
			// A discontinuity the refinement narrowed down: each ray keeps its own spans up to the middle angle, on its own ground.
			const float Mid = (A.AngleDeg + B.AngleDeg) * 0.5f;
			for (const FVisionConeSpan& Span : A.Spans)
			{
				Emit(Span.Level, A.AngleDeg, Span.StartCm, Span.EndCm, Mid, Span.StartCm, Span.EndCm, GroundA, GroundA);
			}
			for (const FVisionConeSpan& Span : B.Spans)
			{
				Emit(Span.Level, Mid, Span.StartCm, Span.EndCm, B.AngleDeg, Span.StartCm, Span.EndCm, GroundB, GroundB);
			}
		}
	}
}

float FVisionConeSightSpec::GetBaseStepDeg() const
{
	const float Range = GetMaxRange();
	if (RaySpacingCm <= 0.f || Range <= 0.f)
	{
		return RayStepDeg;
	}
	return FMath::Clamp(FMath::RadiansToDegrees(RaySpacingCm / Range), FMath::Min(MinRayStepDeg, RayStepDeg), RayStepDeg);
}

uint8 FVisionConeLayerMember::LevelAt(const FVector2D& WorldPoint) const
{
	if (!Rays.IsValid())
	{
		return 0;
	}
	const FVector2D Offset = WorldPoint - Origin;
	if (Offset.SizeSquared() > FMath::Square(MaxRangeCm + 1.f))
	{
		return 0;
	}
	float Sin = 0.f;
	float Cos = 1.f;
	FMath::SinCos(&Sin, &Cos, FMath::DegreesToRadians(YawDeg));
	const FVector2D Local(Offset.X * Cos + Offset.Y * Sin, -Offset.X * Sin + Offset.Y * Cos);
	return VisionConeVisibility::LevelAtPoint(*Rays, HalfAngleDeg, Local);
}

float FVisionConeSightSpec::GetMaxRange() const
{
	float Range = 0.f;
	for (const FVisionConeLevel& Level : Levels)
	{
		Range = FMath::Max(Range, Level.RangeCm);
	}
	return Range;
}

float FVisionConeSightSpec::GetHighTraceHeight(float GroundCm) const
{
	float Height = EyeHeightCm;
	for (const FVisionConeLevel& Level : Levels)
	{
		Height = FMath::Max(Height, GroundCm + Level.ProfileHeightCm);
	}
	return Height;
}

float FVisionConeSightSpec::GetLowTraceHeight(float GroundCm) const
{
	float Height = EyeHeightCm;
	for (const FVisionConeLevel& Level : Levels)
	{
		Height = FMath::Min(Height, GroundCm + Level.ProfileHeightCm);
	}
	return Height;
}

float VisionConeVisibility::ElevatedGroundCm(float FeetZ, float GroundZ, float MinDropCm)
{
	return FeetZ - GroundZ >= MinDropCm ? GroundZ - FeetZ : 0.f;
}

float VisionConeVisibility::WallShadowLengthCm(float EyeAboveGroundCm, float WallTopCm, float WallDistanceCm, float ProfileCm)
{
	if (ProfileCm >= WallTopCm)
	{
		return 0.f;
	}
	if (EyeAboveGroundCm <= WallTopCm)
	{
		return TNumericLimits<float>::Max();
	}
	// The line eye -> profile grazes the wall's top at the face: Face x (Eye - Profile) / (Eye - Top) from the eye.
	return WallDistanceCm * (WallTopCm - ProfileCm) / (EyeAboveGroundCm - WallTopCm);
}

float VisionConeVisibility::PerchDeadZoneCm(float EyeAboveRoofCm, float RoofAboveGroundCm, float EdgeCm, float ProfileCm)
{
	const float ProfileBelowRoof = RoofAboveGroundCm - ProfileCm;
	if (ProfileBelowRoof <= 0.f)
	{
		return EdgeCm;
	}
	if (EyeAboveRoofCm <= KINDA_SMALL_NUMBER)
	{
		return TNumericLimits<float>::Max();
	}
	return EdgeCm * (EyeAboveRoofCm + ProfileBelowRoof) / EyeAboveRoofCm;
}

FVisionConeSightSpec VisionConeVisibility::MakeOperativeSightSpec(float EyeHeightCm, float RangeCm, int32 LevelCount)
{
	FVisionConeSightSpec Spec;
	Spec.EyeHeightCm = EyeHeightCm;
	Spec.Levels.Add({ SightRules::ProfileHeight(EOperativeStance::Standing), RangeCm });
	if (LevelCount >= 2)
	{
		Spec.Levels.Add({ SightRules::ProfileHeight(EOperativeStance::Crouching), RangeCm });
	}
	return Spec;
}

bool VisionConeVisibility::IsBlockedByFace(float EyeCm, float ProfileCm, float FaceCm, float TopCm, float TargetCm)
{
	if (TargetCm <= FaceCm)
	{
		return false;
	}
	return !SightRules::ClearsCover(EyeCm, ProfileCm, FaceCm, TargetCm, TopCm);
}

bool VisionConeVisibility::IsSeenAt(const FVisionConeSightSpec& Spec, const FVisionConeRay& Ray, int32 Level, float TargetCm)
{
	if (!Spec.Levels.IsValidIndex(Level) || TargetCm > Spec.Levels[Level].RangeCm)
	{
		return false;
	}
	// 2.5D (bug 09): the target stands on the ray's ground (heights from the observer's feet).
	const float Profile = Ray.GroundCm + Spec.Levels[Level].ProfileHeightCm;
	for (const FVisionConeObstacle& Obstacle : Ray.Obstacles)
	{
		if (Obstacle.bHidesInside && TargetCm > Obstacle.NearCm && TargetCm < Obstacle.FarCm)
		{
			return false; // on the ground under the observer's own roof
		}
		if (IsBlockedByFace(Spec.EyeHeightCm, Profile, Obstacle.NearCm, Obstacle.TopCm, TargetCm)
			|| IsBlockedByFace(Spec.EyeHeightCm, Profile, Obstacle.FarCm, Obstacle.TopCm, TargetCm))
		{
			return false;
		}
	}
	return true;
}

void VisionConeVisibility::AddFaceBreaks(float EyeCm, float ProfileCm, float FaceCm, float TopCm, TArray<float, TInlineAllocator<16>>& OutBreaks)
{
	OutBreaks.Add(FaceCm);
	// Line height at the face for a target T away: Eye + (Profile - Eye) * Face / T; equal to Top at T = Face * (Profile - Eye) / (Top - Eye).
	if (FMath::Abs(TopCm - EyeCm) < KINDA_SMALL_NUMBER || TopCm >= FullHeightCm)
	{
		return;
	}
	const float Crossing = FaceCm * (ProfileCm - EyeCm) / (TopCm - EyeCm);
	if (Crossing > FaceCm && FMath::IsFinite(Crossing))
	{
		OutBreaks.Add(Crossing);
	}
}

void VisionConeVisibility::ClassifyRay(const FVisionConeSightSpec& Spec, FVisionConeRay& Ray)
{
	Ray.Spans.Reset();
	Ray.Obstacles.Sort([](const FVisionConeObstacle& A, const FVisionConeObstacle& B) { return A.NearCm < B.NearCm; });
	const float MaxRange = Spec.GetMaxRange();
	if (MaxRange <= 0.f || Spec.Levels.Num() == 0)
	{
		return;
	}
	TArray<float, TInlineAllocator<16>> Breaks;
	Breaks.Add(0.f);
	for (const FVisionConeLevel& Level : Spec.Levels)
	{
		Breaks.Add(Level.RangeCm);
		const float Profile = Ray.GroundCm + Level.ProfileHeightCm;
		for (const FVisionConeObstacle& Obstacle : Ray.Obstacles)
		{
			AddFaceBreaks(Spec.EyeHeightCm, Profile, Obstacle.NearCm, Obstacle.TopCm, Breaks);
			AddFaceBreaks(Spec.EyeHeightCm, Profile, Obstacle.FarCm, Obstacle.TopCm, Breaks);
		}
	}
	for (float& Break : Breaks)
	{
		Break = FMath::Clamp(Break, 0.f, MaxRange);
	}
	Breaks.Sort();
	for (int32 Index = 0; Index + 1 < Breaks.Num(); ++Index)
	{
		const float From = Breaks[Index];
		const float To = Breaks[Index + 1];
		if (To - From < 0.05f)
		{
			continue;
		}
		const float Mid = (From + To) * 0.5f;
		int32 Seen = 0;
		for (int32 Level = 0; Level < Spec.Levels.Num(); ++Level)
		{
			Seen += IsSeenAt(Spec, Ray, Level, Mid) ? 1 : 0;
		}
		if (Seen == 0)
		{
			continue;
		}
		FVisionConeSpan* Last = Ray.Spans.Num() > 0 ? &Ray.Spans.Last() : nullptr;
		if (Last && Last->Level == Seen && From - Last->EndCm < MinSpanCm)
		{
			Last->EndCm = To;
		}
		else
		{
			Ray.Spans.Add({ From, To, static_cast<uint8>(Seen) });
		}
	}
	// Slivers out; neighbours of one level that now touch are merged.
	TArray<FVisionConeSpan, TInlineAllocator<4>> Clean;
	for (const FVisionConeSpan& Span : Ray.Spans)
	{
		if (Span.EndCm - Span.StartCm < MinSpanCm)
		{
			continue;
		}
		if (Clean.Num() > 0 && Clean.Last().Level == Span.Level && Span.StartCm - Clean.Last().EndCm < MinSpanCm)
		{
			Clean.Last().EndCm = Span.EndCm;
		}
		else
		{
			Clean.Add(Span);
		}
	}
	Ray.Spans = MoveTemp(Clean);
}

uint8 VisionConeVisibility::LevelAlong(const FVisionConeRay& Ray, float TargetCm)
{
	for (const FVisionConeSpan& Span : Ray.Spans)
	{
		if (TargetCm >= Span.StartCm && TargetCm <= Span.EndCm)
		{
			return Span.Level;
		}
	}
	return 0;
}

TArray<float> VisionConeVisibility::BaseAngles(float HalfAngleDeg, float StepDeg, int32 MaxRays)
{
	const float Half = VisionConeRules::ClampHalfAngle(HalfAngleDeg);
	int32 Intervals = FMath::Max(1, FMath::CeilToInt(2.f * Half / FMath::Max(StepDeg, 0.1f)));
	Intervals = FMath::Clamp(Intervals, 1, FMath::Max(MaxRays - 1, 1));
	TArray<float> Angles;
	Angles.Reserve(Intervals + 1);
	for (int32 Index = 0; Index <= Intervals; ++Index)
	{
		Angles.Add(-Half + 2.f * Half * Index / Intervals);
	}
	return Angles;
}

TArray<float> VisionConeVisibility::GridAngles(float HalfAngleDeg, float StepDeg, int32 MaxRays, float GridYawDeg)
{
	const float Half = VisionConeRules::ClampHalfAngle(HalfAngleDeg);
	double Step = FMath::Max(StepDeg, 0.05f);
	const int32 MaxInner = FMath::Max(MaxRays - 2, 1);
	if (2.0 * Half / Step > MaxInner)
	{
		Step = 2.0 * Half / MaxInner;
	}
	const double Margin = 0.2 * Step;
	TArray<float> Angles;
	Angles.Reserve(FMath::CeilToInt(2.0 * Half / Step) + 2);
	Angles.Add(-Half);
	for (int64 K = static_cast<int64>(FMath::CeilToDouble((GridYawDeg - Half + Margin) / Step));; ++K)
	{
		const double Angle = K * Step - GridYawDeg;
		if (Angle >= Half - Margin)
		{
			break;
		}
		Angles.Add(static_cast<float>(Angle));
	}
	Angles.Add(Half);
	return Angles;
}

bool VisionConeVisibility::IsJoinable(const FVisionConeRay& A, const FVisionConeRay& B)
{
	return SameStructure(A, B) && FMath::Abs(DrawGround(A) - DrawGround(B)) <= GroundToleranceCm;
}

bool VisionConeVisibility::SameStructure(const FVisionConeRay& A, const FVisionConeRay& B)
{
	if (A.Spans.Num() != B.Spans.Num())
	{
		return false;
	}
	for (int32 Index = 0; Index < A.Spans.Num(); ++Index)
	{
		if (A.Spans[Index].Level != B.Spans[Index].Level)
		{
			return false;
		}
	}
	return true;
}

bool VisionConeVisibility::NeedsRefine(const FVisionConeSightSpec& Spec, const FVisionConeRay& A, const FVisionConeRay& B)
{
	if (A.bNoGround || B.bNoGround)
	{
		return false; // dropped by CastRays: no ray is spent on finding its edge
	}
	if (!SameStructure(A, B) || A.BlockerKey != B.BlockerKey || A.Obstacles.Num() != B.Obstacles.Num()
		|| FMath::Abs(A.GroundCm - B.GroundCm) > GroundToleranceCm)
	{
		return true;
	}
	const float MinDot = FMath::Cos(FMath::DegreesToRadians(Spec.NormalToleranceDeg));
	auto Turns = [MinDot](const FVector2D& NormalA, const FVector2D& NormalB)
	{
		return !NormalA.IsNearlyZero() && !NormalB.IsNearlyZero() && FVector2D::DotProduct(NormalA.GetSafeNormal(), NormalB.GetSafeNormal()) < MinDot;
	};
	if (Turns(A.BlockerNormal, B.BlockerNormal))
	{
		return true;
	}
	auto Jumps = [&Spec](float DistanceA, float DistanceB)
	{
		const float Tolerance = FMath::Max(Spec.JumpToleranceCm, Spec.JumpToleranceFraction * FMath::Max(DistanceA, DistanceB));
		return FMath::Abs(DistanceA - DistanceB) > Tolerance;
	};
	for (int32 Index = 0; Index < A.Obstacles.Num(); ++Index)
	{
		const FVisionConeObstacle& OA = A.Obstacles[Index];
		const FVisionConeObstacle& OB = B.Obstacles[Index];
		if (OA.bHidesInside != OB.bHidesInside || Turns(OA.NearNormal, OB.NearNormal) || Turns(OA.FarNormal, OB.FarNormal)
			|| FMath::Abs(FMath::Min(OA.TopCm, 1.e4f) - FMath::Min(OB.TopCm, 1.e4f)) > 5.f || Jumps(OA.NearCm, OB.NearCm) || Jumps(OA.FarCm, OB.FarCm))
		{
			return true;
		}
	}
	for (int32 Index = 0; Index < A.Spans.Num(); ++Index)
	{
		if (Jumps(A.Spans[Index].StartCm, B.Spans[Index].StartCm) || Jumps(A.Spans[Index].EndCm, B.Spans[Index].EndCm))
		{
			return true;
		}
	}
	return false;
}

TArray<FVisionConeRay> VisionConeVisibility::CastRays(const FVisionConeSightSpec& Spec, TFunctionRef<void(float, FVisionConeRay&)> Sample,
	int32* OutSamples)
{
	TArray<FVisionConeRay> Rays;
	if (Spec.Levels.Num() == 0 || Spec.GetMaxRange() <= 0.f)
	{
		if (OutSamples)
		{
			*OutSamples = 0;
		}
		return Rays;
	}
	const float Step = Spec.GetBaseStepDeg();
	const TArray<float> Angles = Spec.bWorldGrid ? GridAngles(Spec.HalfAngleDeg, Step, Spec.MaxRays, Spec.GridYawDeg)
		: BaseAngles(Spec.HalfAngleDeg, Step, Spec.MaxRays);
	FCaster Caster{ Spec, Sample };
	TArray<FVisionConeRay> Base;
	Base.Reserve(Angles.Num());
	for (const float Angle : Angles)
	{
		Base.Add(Caster.Cast(Angle));
	}
	// Bug #38: the rays left are shared out among the gaps that need refining, in order (a gap that needs fewer leaves more to the next) -
	// the old first-come budget refined the first shadow edges finely and left the rest of the fan stair-stepped.
	TArray<int32> Needy;
	for (int32 Index = 1; Index < Base.Num(); ++Index)
	{
		if (NeedsRefine(Spec, Base[Index - 1], Base[Index]))
		{
			Needy.Add(Index);
		}
	}
	Rays.Reserve(Base.Num() * 2);
	int32 NextNeedy = 0;
	for (int32 Index = 0; Index < Base.Num(); ++Index)
	{
		if (NextNeedy < Needy.Num() && Needy[NextNeedy] == Index)
		{
			const int32 GapsLeft = Needy.Num() - NextNeedy;
			Caster.GapLimit = Caster.Samples + FMath::Max(1, (Spec.MaxRays - Caster.Samples) / GapsLeft);
			Caster.Refine(Base[Index - 1], Base[Index], Spec.MaxRefineDepth, Rays);
			++NextNeedy;
		}
		Rays.Add(Base[Index]);
	}
	if (OutSamples)
	{
		*OutSamples = Caster.Samples;
	}
	// Elevated rays without a measured ground are left out (drawn at the roof's level for the whole range they were the needles).
	const int32 Grounded = Rays.FilterByPredicate([](const FVisionConeRay& Ray) { return !Ray.bNoGround; }).Num();
	if (Grounded >= 2 && Grounded < Rays.Num())
	{
		Rays.RemoveAll([](const FVisionConeRay& Ray) { return Ray.bNoGround; });
	}
	else if (Grounded < 2)
	{
		for (FVisionConeRay& Ray : Rays)
		{
			if (Ray.bNoGround)
			{
				Ray.Spans.Reset(); // too few to draw anything between: nothing is drawn rather than the roof's level to the range
			}
		}
	}
	// Slivers out (a removed one can leave its neighbour's stretch a sliver in turn: a few passes).
	TArray<TArray<bool, TInlineAllocator<4>>> Slivers;
	for (int32 Pass = 0; Pass < 16 && Spec.SliverMinWidthCm > 0.f; ++Pass)
	{
		if (FindSlivers(Rays, Spec.SliverMinWidthCm, Spec.SliverFromCm, &Slivers) == 0)
		{
			break;
		}
		{
			for (int32 Index = 0; Index < Rays.Num(); ++Index)
			{
				for (int32 Span = Rays[Index].Spans.Num() - 1; Span >= 0; --Span)
				{
					if (Slivers[Index][Span])
					{
						Rays[Index].Spans.RemoveAt(Span);
					}
				}
			}
		}
	}
	return Rays;
}

FVisionConeShape VisionConeVisibility::BuildShape(const TArray<FVisionConeRay>& Rays, int32 LevelCount, float HalfAngleDeg,
	float OutlineWidthCm, float HeightCm, float OutlineHeightCm)
{
	FVisionConeShapeOptions Options;
	Options.OutlineWidthCm = OutlineWidthCm;
	Options.HeightCm = HeightCm;
	Options.OutlineHeightCm = OutlineHeightCm;
	return BuildShape(Rays, LevelCount, HalfAngleDeg, Options);
}

FVisionConeShape VisionConeVisibility::BuildShape(const TArray<FVisionConeRay>& Rays, int32 LevelCount, float HalfAngleDeg, const FVisionConeShapeOptions& Options)
{
	FVisionConeShape Shape;
	if (Rays.Num() < 2)
	{
		return Shape;
	}
	const float HeightCm = Options.HeightCm;
	auto MeshFor = [&Shape, LevelCount](uint8 Level) -> FVisionConeMesh&
	{
		return Level >= LevelCount ? Shape.Full : Shape.Light;
	};
	ForEachQuad(Rays, [&MeshFor, HeightCm](uint8 Level, float AngleA, float StartA, float EndA, float AngleB, float StartB, float EndB, float GroundA, float GroundB)
	{
		AddSpanQuad(MeshFor(Level), AngleA, StartA, EndA, AngleB, StartB, EndB, HeightCm + GroundA, HeightCm + GroundB);
	});
	// The weapon zone (bug #39): the seen ground from the effective range to the max reach, and a line along the effective range.
	if (Options.Weapon.IsSet())
	{
		const float Effective = FMath::Min(Options.Weapon.EffectiveCm, Options.Weapon.MaxCm);
		const float HalfEdge = 0.5f * FMath::Max(Options.Weapon.EdgeWidthCm, 1.f);
		ForEachQuad(Options.WeaponRays ? *Options.WeaponRays : Rays, [&Shape, &Options, Effective, HalfEdge, HeightCm](uint8, float AngleA, float StartA, float EndA,
			float AngleB, float StartB, float EndB, float GroundA, float GroundB)
		{
			auto Cut = [&](float Low, float High, FVisionConeMesh& Mesh, float Lift)
			{
				const float FromA = FMath::Max(StartA, Low);
				const float ToA = FMath::Min(EndA, High);
				const float FromB = FMath::Max(StartB, Low);
				const float ToB = FMath::Min(EndB, High);
				if (ToA - FromA > 1.f && ToB - FromB > 1.f)
				{
					AddSpanQuad(Mesh, AngleA, FromA, ToA, AngleB, FromB, ToB, HeightCm + GroundA + Lift, HeightCm + GroundB + Lift);
				}
			};
			if (Options.Weapon.MaxCm > Effective + 1.f)
			{
				Cut(Effective, Options.Weapon.MaxCm, Shape.WeaponBand, 0.3f);
			}
			Cut(Effective - HalfEdge, Effective + HalfEdge, Shape.WeaponEdge, 0.4f);
		});
	}
	// The outline along the outer boundary of what the owner sees (OutlineRays: before a merged layer clipped it).
	const TArray<FVisionConeRay>& Outer = Options.OutlineRays && Options.OutlineRays->Num() >= 2 ? *Options.OutlineRays : Rays;
	const bool bAllRound = VisionConeRules::ClampHalfAngle(HalfAngleDeg) >= 180.f;
	const float OutlineLift = Options.OutlineHeightCm - HeightCm;
	TArray<FVector> Envelope;
	if (!bAllRound)
	{
		Envelope.Add(FVector(0.f, 0.f, HeightCm + DrawGround(Outer[0]) + OutlineLift));
	}
	auto AddEnvelope = [&Envelope, HeightCm, OutlineLift](float AngleDeg, float DistanceCm, float GroundCm)
	{
		const FVector2D Point2 = Direction(AngleDeg) * DistanceCm;
		const FVector Point(Point2, HeightCm + GroundCm + OutlineLift);
		if (Envelope.Num() == 0 || FVector2D::Distance(FVector2D(Envelope.Last().X, Envelope.Last().Y), Point2) > 0.5f)
		{
			Envelope.Add(Point);
		}
	};
	for (int32 Index = 0; Index < Outer.Num(); ++Index)
	{
		const FVisionConeRay& A = Outer[Index];
		AddEnvelope(A.AngleDeg, FarEnd(A), DrawGround(A));
		if (Index + 1 >= Outer.Num())
		{
			break;
		}
		const FVisionConeRay& B = Outer[Index + 1];
		if (B.AngleDeg - A.AngleDeg <= 1.e-5f || IsJoinable(A, B))
		{
			continue;
		}
		const float Mid = (A.AngleDeg + B.AngleDeg) * 0.5f;
		AddEnvelope(Mid, FarEnd(A), DrawGround(A));
		AddEnvelope(Mid, FarEnd(B), DrawGround(B));
	}
	if (Envelope.Num() > 2 && FVector2D::Distance(FVector2D(Envelope[0].X, Envelope[0].Y), FVector2D(Envelope.Last().X, Envelope.Last().Y)) <= 0.5f)
	{
		Envelope.Pop(); // all round: the last ray is the first one
	}
	BuildClosedBand(Envelope, Options.OutlineWidthCm, Shape.Outline, 30.f, Options.OutlineHidden ? &Options.OutlineHidden : nullptr);
	return Shape;
}

namespace
{
	/**
	 * A layer member prepared for many point queries (fix/cone-rebuild-cost 2026-11-08: ClipToLayer asks the members' level at ~50 000 points
	 * per 120 m cone - a third of a rebuild): its yaw's sin / cos once, its rays' angles and directions in flat arrays (the binary search no longer
	 * walks 300-byte rays), which neighbours are joinable, its best level. LevelAt gives bit for bit FVisionConeLayerMember::LevelAt.
	 */
	struct FPreparedMember
	{
		const FVisionConeLayerMember* Member = nullptr;
		const TArray<FVisionConeRay>* Rays = nullptr;
		float Sin = 0.f;
		float Cos = 1.f;
		float MaxRangeSq = 0.f;
		TArray<float> Angles;
		TArray<FVector2D> Directions;
		TArray<bool> Joinable;
		uint8 MaxLevel = 0;
		/** The last bracket found (LevelAt walks from it). */
		mutable int32 Hint = 0;
		static constexpr int32 MaxWalk = 8;

		void Prepare(const FVisionConeLayerMember& InMember)
		{
			Member = &InMember;
			Rays = InMember.Rays.Get();
			FMath::SinCos(&Sin, &Cos, FMath::DegreesToRadians(InMember.YawDeg));
			MaxRangeSq = FMath::Square(InMember.MaxRangeCm + 1.f);
			const int32 Count = Rays->Num();
			Angles.SetNumUninitialized(Count);
			Directions.SetNumUninitialized(Count);
			Joinable.SetNumUninitialized(FMath::Max(Count - 1, 0));
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const FVisionConeRay& Ray = (*Rays)[Index];
				Angles[Index] = Ray.AngleDeg;
				Directions[Index] = Direction(Ray.AngleDeg);
				for (const FVisionConeSpan& Span : Ray.Spans)
				{
					MaxLevel = FMath::Max(MaxLevel, Span.Level);
				}
				if (Index + 1 < Count)
				{
					Joinable[Index] = VisionConeVisibility::IsJoinable(Ray, (*Rays)[Index + 1]);
				}
			}
		}

		/** VisionConeVisibility::ChordDistance with the rays' directions precomputed (the same operations, the same result). */
		static float Chord(const FVector2D& DirA, float AngleA, float DistanceA, const FVector2D& DirB, float AngleB, float DistanceB, const FVector2D& Dir, float AngleDeg)
		{
			const FVector2D P1 = DirA * DistanceA;
			const FVector2D P2 = DirB * DistanceB;
			const FVector2D Along = P2 - P1;
			const float Denominator = Cross(Dir, Along);
			if (FMath::Abs(Denominator) < 1.e-4f)
			{
				const float Span = AngleB - AngleA;
				const float Alpha = FMath::Abs(Span) < 1.e-6f ? 0.f : FMath::Clamp((AngleDeg - AngleA) / Span, 0.f, 1.f);
				return FMath::Lerp(DistanceA, DistanceB, Alpha);
			}
			return Cross(P1, Along) / Denominator;
		}

		/** = Member->LevelAt(WorldPoint) (FVisionConeLayerMember::LevelAt + VisionConeVisibility::LevelAtPoint). */
		uint8 LevelAt(const FVector2D& WorldPoint) const
		{
			const FVector2D Offset = WorldPoint - Member->Origin;
			if (Offset.SizeSquared() > MaxRangeSq)
			{
				return 0;
			}
			const int32 Count = Angles.Num();
			if (Count < 2)
			{
				return 0;
			}
			const FVector2D Point(Offset.X * Cos + Offset.Y * Sin, -Offset.X * Sin + Offset.Y * Cos);
			const float Angle = FMath::RadiansToDegrees(FMath::Atan2(Point.Y, Point.X));
			if (Angle < Angles[0] - 1.e-3f || Angle > Angles[Count - 1] + 1.e-3f)
			{
				return 0;
			}
			const float Distance = Point.Size();
			// The bracket = the last ray at or before the angle among [0, Count - 2] (else 0) - what the binary search finds. The points of one ray
			// of ours come in order along a straight line, whose bearing from this member turns monotonically: walk from the last bracket first.
			int32 Low = FMath::Clamp(Hint, 0, Count - 2);
			int32 Steps = 0;
			if (Angles[Low] <= Angle)
			{
				while (Low + 1 <= Count - 2 && Angles[Low + 1] <= Angle && ++Steps <= MaxWalk)
				{
					++Low;
				}
			}
			else
			{
				while (Low > 0 && Angles[Low] > Angle && ++Steps <= MaxWalk)
				{
					--Low;
				}
			}
			if (Steps > MaxWalk)
			{
				Low = 0;
				int32 Top = Count - 1;
				while (Top - Low > 1)
				{
					const int32 Mid = (Low + Top) / 2;
					(Angles[Mid] <= Angle ? Low : Top) = Mid;
				}
			}
			Hint = Low;
			const int32 High = Low + 1;
			const FVisionConeRay& A = (*Rays)[Low];
			const FVisionConeRay& B = (*Rays)[High];
			if (Joinable[Low] && B.AngleDeg - A.AngleDeg > 1.e-5f)
			{
				const FVector2D Dir = Direction(Angle);
				for (int32 Span = 0; Span < A.Spans.Num(); ++Span)
				{
					const float Start = Chord(Directions[Low], A.AngleDeg, A.Spans[Span].StartCm, Directions[High], B.AngleDeg, B.Spans[Span].StartCm, Dir, Angle);
					const float End = Chord(Directions[Low], A.AngleDeg, A.Spans[Span].EndCm, Directions[High], B.AngleDeg, B.Spans[Span].EndCm, Dir, Angle);
					if (Distance >= Start && Distance <= End)
					{
						return A.Spans[Span].Level;
					}
				}
				return 0;
			}
			return VisionConeVisibility::LevelAlong(Angle <= (A.AngleDeg + B.AngleDeg) * 0.5f ? A : B, Distance);
		}
	};
}

int32 VisionConeVisibility::ClipToLayer(TArray<FVisionConeRay>& Rays, const FVector2D& Origin, float YawDeg, int32 Priority,
	TArrayView<const FVisionConeLayerMember> Others, float StepCm)
{
	int32 Cut = 0;
	if (Others.Num() == 0)
	{
		return 0;
	}
	StepCm = FMath::Max(StepCm, 10.f);
	TArray<FPreparedMember, TInlineAllocator<4>> Prepared;
	for (const FVisionConeLayerMember& Other : Others)
	{
		if (Other.Rays.IsValid())
		{
			Prepared.AddDefaulted_GetRef().Prepare(Other);
		}
	}
	TArray<const FPreparedMember*, TInlineAllocator<4>> Near;
	TArray<const FPreparedMember*, TInlineAllocator<4>> Rivals;
	for (FVisionConeRay& Ray : Rays)
	{
		const float End = FarEnd(Ray);
		if (End <= 0.f)
		{
			continue;
		}
		const FVector2D Dir = Direction(YawDeg + Ray.AngleDeg);
		// Only the members whose reach meets this ray at all.
		Near.Reset();
		for (const FPreparedMember& Other : Prepared)
		{
			const float Along = FMath::Clamp(static_cast<float>(FVector2D::DotProduct(Other.Member->Origin - Origin, Dir)), 0.f, End);
			if (FVector2D::DistSquared(Origin + Dir * Along, Other.Member->Origin) <= FMath::Square(Other.Member->MaxRangeCm))
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
			// A member that never shows more than this span's level and loses the ties (a higher Priority) never takes a point of it: not asked.
			Rivals.Reset();
			for (const FPreparedMember* Other : Near)
			{
				if (Other->MaxLevel > Span.Level || (Other->MaxLevel == Span.Level && Other->Member->Priority < Priority))
				{
					Rivals.Add(Other);
				}
			}
			if (Rivals.Num() == 0)
			{
				Kept.Add(Span);
				continue;
			}
			auto Keeps = [&Rivals, &Origin, &Dir, &Span, Priority](float Distance)
			{
				const FVector2D Point = Origin + Dir * Distance;
				for (const FPreparedMember* Other : Rivals)
				{
					const uint8 Level = Other->LevelAt(Point);
					if (Level > Span.Level || (Level == Span.Level && Other->Member->Priority < Priority))
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
		Kept.RemoveAll([](const FVisionConeSpan& Span) { return Span.EndCm - Span.StartCm < MinSpanCm; });
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

float VisionConeVisibility::ChordDistance(float AngleA, float DistanceA, float AngleB, float DistanceB, float AngleDeg)
{
	const FVector2D P1 = Direction(AngleA) * DistanceA;
	const FVector2D P2 = Direction(AngleB) * DistanceB;
	const FVector2D Along = P2 - P1;
	const FVector2D Dir = Direction(AngleDeg);
	const float Denominator = Cross(Dir, Along);
	if (FMath::Abs(Denominator) < 1.e-4f)
	{
		const float Span = AngleB - AngleA;
		const float Alpha = FMath::Abs(Span) < 1.e-6f ? 0.f : FMath::Clamp((AngleDeg - AngleA) / Span, 0.f, 1.f);
		return FMath::Lerp(DistanceA, DistanceB, Alpha);
	}
	return Cross(P1, Along) / Denominator;
}

uint8 VisionConeVisibility::LevelAtPoint(const TArray<FVisionConeRay>& Rays, float HalfAngleDeg, const FVector2D& Point)
{
	if (Rays.Num() < 2)
	{
		return 0;
	}
	const float Angle = FMath::RadiansToDegrees(FMath::Atan2(Point.Y, Point.X));
	const float Distance = Point.Size();
	if (Angle < Rays[0].AngleDeg - 1.e-3f || Angle > Rays.Last().AngleDeg + 1.e-3f)
	{
		return 0;
	}
	int32 Low = 0;
	int32 High = Rays.Num() - 1;
	while (High - Low > 1)
	{
		const int32 Mid = (Low + High) / 2;
		(Rays[Mid].AngleDeg <= Angle ? Low : High) = Mid;
	}
	const FVisionConeRay& A = Rays[Low];
	const FVisionConeRay& B = Rays[High];
	if (IsJoinable(A, B) && B.AngleDeg - A.AngleDeg > 1.e-5f)
	{
		for (int32 Span = 0; Span < A.Spans.Num(); ++Span)
		{
			const float Start = ChordDistance(A.AngleDeg, A.Spans[Span].StartCm, B.AngleDeg, B.Spans[Span].StartCm, Angle);
			const float End = ChordDistance(A.AngleDeg, A.Spans[Span].EndCm, B.AngleDeg, B.Spans[Span].EndCm, Angle);
			if (Distance >= Start && Distance <= End)
			{
				return A.Spans[Span].Level;
			}
		}
		return 0;
	}
	return LevelAlong(Angle <= (A.AngleDeg + B.AngleDeg) * 0.5f ? A : B, Distance);
}

int32 VisionConeVisibility::FindSlivers(const TArray<FVisionConeRay>& Rays, float MinWidthCm, float FromCm, TArray<TArray<bool, TInlineAllocator<4>>>* OutSlivers)
{
	int32 Count = 0;
	if (OutSlivers)
	{
		OutSlivers->SetNum(Rays.Num());
	}
	for (int32 Index = 0; Index < Rays.Num(); ++Index)
	{
		const FVisionConeRay& Ray = Rays[Index];
		if (OutSlivers)
		{
			(*OutSlivers)[Index].Init(false, Ray.Spans.Num());
		}
		for (int32 Span = 0; Span < Ray.Spans.Num(); ++Span)
		{
			const FVisionConeSpan& Stretch = Ray.Spans[Span];
			if (Stretch.EndCm <= FromCm)
			{
				continue;
			}
			const float Distance = FMath::Max(0.5f * (Stretch.StartCm + Stretch.EndCm), FromCm);
			const float Needed = MinWidthCm / FMath::Max(Distance, 1.f); // radians
			// Walk out both ways while the neighbours see that distance too (stop once the run is wide enough).
			int32 Left = Index;
			int32 Right = Index;
			auto Width = [&Rays, &Left, &Right]()
			{
				const float LeftGap = Left > 0 ? 0.5f * (Rays[Left].AngleDeg - Rays[Left - 1].AngleDeg) : 0.f;
				const float RightGap = Right + 1 < Rays.Num() ? 0.5f * (Rays[Right + 1].AngleDeg - Rays[Right].AngleDeg) : 0.f;
				return FMath::DegreesToRadians(Rays[Right].AngleDeg - Rays[Left].AngleDeg + LeftGap + RightGap);
			};
			while (Width() < Needed)
			{
				const bool bGrowLeft = Left > 0 && LevelAlong(Rays[Left - 1], Distance) > 0;
				const bool bGrowRight = Right + 1 < Rays.Num() && LevelAlong(Rays[Right + 1], Distance) > 0;
				if (!bGrowLeft && !bGrowRight)
				{
					break;
				}
				Left -= bGrowLeft ? 1 : 0;
				Right += bGrowRight ? 1 : 0;
			}
			if (Width() < Needed)
			{
				++Count;
				if (OutSlivers)
				{
					(*OutSlivers)[Index][Span] = true;
				}
			}
		}
	}
	return Count;
}
