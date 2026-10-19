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

	void AddDoubleSided(TArray<int32>& Triangles, int32 A, int32 B, int32 C)
	{
		Triangles.Append({ A, B, C, A, C, B });
	}

	/** Quad (or a triangle when both starts sit on the apex) between two angles: [Start, End] on each side. */
	void AddSpanQuad(FVisionConeMesh& Mesh, float AngleA, float StartA, float EndA, float AngleB, float StartB, float EndB, float HeightCm)
	{
		const int32 Base = Mesh.Vertices.Num();
		if (StartA < 0.01f && StartB < 0.01f)
		{
			Mesh.Vertices.Append({ FVector(0.f, 0.f, HeightCm), PointAt(AngleA, EndA, HeightCm), PointAt(AngleB, EndB, HeightCm) });
			AddDoubleSided(Mesh.Triangles, Base, Base + 1, Base + 2);
			return;
		}
		Mesh.Vertices.Append({ PointAt(AngleA, StartA, HeightCm), PointAt(AngleA, EndA, HeightCm), PointAt(AngleB, EndB, HeightCm),
			PointAt(AngleB, StartB, HeightCm) });
		AddDoubleSided(Mesh.Triangles, Base, Base + 1, Base + 2);
		AddDoubleSided(Mesh.Triangles, Base, Base + 2, Base + 3);
	}

	float FarEnd(const FVisionConeRay& Ray)
	{
		return Ray.Spans.Num() > 0 ? Ray.Spans.Last().EndCm : 0.f;
	}

	float Cross(const FVector2D& A, const FVector2D& B)
	{
		return A.X * B.Y - A.Y * B.X;
	}

	/** Closed polyline as a band WidthCm wide on its inner side (mitred corners, clamped). */
	void BuildClosedBand(const TArray<FVector2D>& Points, float WidthCm, float HeightCm, FVisionConeMesh& Mesh)
	{
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
		TArray<FVector2D> Inner;
		Inner.SetNum(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FVector2D N0 = InwardNormal((Index + Count - 1) % Count);
			const FVector2D N1 = InwardNormal(Index);
			FVector2D Mitre = N0 + N1;
			Mitre = Mitre.SizeSquared() < 1.e-4f ? N1 : Mitre.GetSafeNormal();
			const float Length = FMath::Min(WidthCm / FMath::Max(FVector2D::DotProduct(Mitre, N1), 0.34f), WidthCm * 3.f);
			Inner[Index] = Points[Index] + Mitre * Length;
		}
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const int32 Next = (Index + 1) % Count;
			const int32 Base = Mesh.Vertices.Num();
			Mesh.Vertices.Append({ FVector(Points[Index], HeightCm), FVector(Points[Next], HeightCm), FVector(Inner[Next], HeightCm),
				FVector(Inner[Index], HeightCm) });
			AddDoubleSided(Mesh.Triangles, Base, Base + 1, Base + 2);
			AddDoubleSided(Mesh.Triangles, Base, Base + 2, Base + 3);
		}
	}

	struct FCaster
	{
		const FVisionConeSightSpec& Spec;
		TFunctionRef<void(float, FVisionConeRay&)> Sample;
		int32 Samples = 0;

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
			if (Depth <= 0 || Samples >= Spec.MaxRays || B.AngleDeg - A.AngleDeg < 2.f * FMath::Max(Spec.MinRefineGapDeg, 1.e-4f)
				|| !VisionConeVisibility::NeedsRefine(Spec, A, B))
			{
				return;
			}
			const FVisionConeRay Mid = Cast((A.AngleDeg + B.AngleDeg) * 0.5f);
			Refine(A, Mid, Depth - 1, Out);
			Out.Add(Mid);
			Refine(Mid, B, Depth - 1, Out);
		}
	};
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

float FVisionConeSightSpec::GetHighTraceHeight() const
{
	float Height = EyeHeightCm;
	for (const FVisionConeLevel& Level : Levels)
	{
		Height = FMath::Max(Height, Level.ProfileHeightCm);
	}
	return Height;
}

float FVisionConeSightSpec::GetLowTraceHeight() const
{
	float Height = EyeHeightCm;
	for (const FVisionConeLevel& Level : Levels)
	{
		Height = FMath::Min(Height, Level.ProfileHeightCm);
	}
	return Height;
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
	const float Profile = Spec.Levels[Level].ProfileHeightCm;
	for (const FVisionConeObstacle& Obstacle : Ray.Obstacles)
	{
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
		for (const FVisionConeObstacle& Obstacle : Ray.Obstacles)
		{
			AddFaceBreaks(Spec.EyeHeightCm, Level.ProfileHeightCm, Obstacle.NearCm, Obstacle.TopCm, Breaks);
			AddFaceBreaks(Spec.EyeHeightCm, Level.ProfileHeightCm, Obstacle.FarCm, Obstacle.TopCm, Breaks);
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
	if (!SameStructure(A, B) || A.BlockerKey != B.BlockerKey || A.Obstacles.Num() != B.Obstacles.Num())
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
		if (Turns(OA.NearNormal, OB.NearNormal) || Turns(OA.FarNormal, OB.FarNormal)
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
	const TArray<float> Angles = BaseAngles(Spec.HalfAngleDeg, Spec.RayStepDeg, Spec.MaxRays);
	FCaster Caster{ Spec, Sample };
	TArray<FVisionConeRay> Base;
	Base.Reserve(Angles.Num());
	for (const float Angle : Angles)
	{
		Base.Add(Caster.Cast(Angle));
	}
	Rays.Reserve(Base.Num() * 2);
	for (int32 Index = 0; Index < Base.Num(); ++Index)
	{
		if (Index > 0)
		{
			Caster.Refine(Base[Index - 1], Base[Index], Spec.MaxRefineDepth, Rays);
		}
		Rays.Add(Base[Index]);
	}
	if (OutSamples)
	{
		*OutSamples = Caster.Samples;
	}
	return Rays;
}

FVisionConeShape VisionConeVisibility::BuildShape(const TArray<FVisionConeRay>& Rays, int32 LevelCount, float HalfAngleDeg,
	float OutlineWidthCm, float HeightCm, float OutlineHeightCm)
{
	FVisionConeShape Shape;
	if (Rays.Num() < 2)
	{
		return Shape;
	}
	auto MeshFor = [&Shape, LevelCount](uint8 Level) -> FVisionConeMesh&
	{
		return Level >= LevelCount ? Shape.Full : Shape.Light;
	};
	const bool bAllRound = VisionConeRules::ClampHalfAngle(HalfAngleDeg) >= 180.f;
	TArray<FVector2D> Envelope;
	if (!bAllRound)
	{
		Envelope.Add(FVector2D::ZeroVector);
	}
	auto AddEnvelope = [&Envelope](float AngleDeg, float DistanceCm)
	{
		const FVector2D Point = Direction(AngleDeg) * DistanceCm;
		if (Envelope.Num() == 0 || FVector2D::Distance(Envelope.Last(), Point) > 0.5f)
		{
			Envelope.Add(Point);
		}
	};
	for (int32 Index = 0; Index < Rays.Num(); ++Index)
	{
		const FVisionConeRay& A = Rays[Index];
		AddEnvelope(A.AngleDeg, FarEnd(A));
		if (Index + 1 >= Rays.Num())
		{
			break;
		}
		const FVisionConeRay& B = Rays[Index + 1];
		if (B.AngleDeg - A.AngleDeg <= 1.e-5f)
		{
			continue;
		}
		if (SameStructure(A, B))
		{
			for (int32 Span = 0; Span < A.Spans.Num(); ++Span)
			{
				AddSpanQuad(MeshFor(A.Spans[Span].Level), A.AngleDeg, A.Spans[Span].StartCm, A.Spans[Span].EndCm,
					B.AngleDeg, B.Spans[Span].StartCm, B.Spans[Span].EndCm, HeightCm);
			}
			continue;
		}
		// A discontinuity the refinement narrowed down: each ray keeps its own spans up to the middle angle.
		const float Mid = (A.AngleDeg + B.AngleDeg) * 0.5f;
		for (const FVisionConeSpan& Span : A.Spans)
		{
			AddSpanQuad(MeshFor(Span.Level), A.AngleDeg, Span.StartCm, Span.EndCm, Mid, Span.StartCm, Span.EndCm, HeightCm);
		}
		for (const FVisionConeSpan& Span : B.Spans)
		{
			AddSpanQuad(MeshFor(Span.Level), Mid, Span.StartCm, Span.EndCm, B.AngleDeg, Span.StartCm, Span.EndCm, HeightCm);
		}
		AddEnvelope(Mid, FarEnd(A));
		AddEnvelope(Mid, FarEnd(B));
	}
	if (Envelope.Num() > 2 && FVector2D::Distance(Envelope[0], Envelope.Last()) <= 0.5f)
	{
		Envelope.Pop(); // all round: the last ray is the first one
	}
	BuildClosedBand(Envelope, OutlineWidthCm, OutlineHeightCm, Shape.Outline);
	return Shape;
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
	if (SameStructure(A, B) && B.AngleDeg - A.AngleDeg > 1.e-5f)
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
