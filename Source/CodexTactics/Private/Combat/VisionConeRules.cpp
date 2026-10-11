#include "Combat/VisionConeRules.h"

namespace
{
	/** Adds triangle A B C in both windings. */
	void AddDoubleSided(TArray<int32>& Triangles, int32 A, int32 B, int32 C)
	{
		Triangles.Append({ A, B, C, A, C, B });
	}

	/** Quad A B C D (A-B one edge, D-C the opposite edge) as two double-sided triangles. */
	void AddQuad(FVisionConeMesh& Mesh, const FVector& A, const FVector& B, const FVector& C, const FVector& D)
	{
		const int32 Base = Mesh.Vertices.Num();
		Mesh.Vertices.Append({ A, B, C, D });
		AddDoubleSided(Mesh.Triangles, Base, Base + 1, Base + 2);
		AddDoubleSided(Mesh.Triangles, Base, Base + 2, Base + 3);
	}
}

int32 VisionConeRules::ClampSegments(int32 Segments)
{
	return FMath::Clamp(Segments, 3, 128);
}

float VisionConeRules::ClampHalfAngle(float HalfAngleDeg)
{
	return FMath::Clamp(HalfAngleDeg, 1.f, 180.f);
}

FVector VisionConeRules::ArcPoint(float RangeCm, float AngleDeg, float HeightCm)
{
	const float Radians = FMath::DegreesToRadians(AngleDeg);
	return FVector(FMath::Cos(Radians) * RangeCm, FMath::Sin(Radians) * RangeCm, HeightCm);
}

FVisionConeMesh VisionConeRules::BuildFill(float RangeCm, float HalfAngleDeg, int32 Segments, float HeightCm)
{
	FVisionConeMesh Mesh;
	if (RangeCm <= 0.f)
	{
		return Mesh;
	}
	const int32 Count = ClampSegments(Segments);
	const float Half = ClampHalfAngle(HalfAngleDeg);
	Mesh.Vertices.Reserve(Count + 2);
	Mesh.Vertices.Add(FVector(0.f, 0.f, HeightCm));
	for (int32 Index = 0; Index <= Count; ++Index)
	{
		Mesh.Vertices.Add(ArcPoint(RangeCm, -Half + 2.f * Half * Index / Count, HeightCm));
	}
	Mesh.Triangles.Reserve(Count * 6);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		AddDoubleSided(Mesh.Triangles, 0, Index + 1, Index + 2);
	}
	return Mesh;
}

FVisionConeMesh VisionConeRules::BuildOutline(float RangeCm, float HalfAngleDeg, int32 Segments, float WidthCm, float HeightCm)
{
	FVisionConeMesh Mesh;
	if (RangeCm <= 0.f)
	{
		return Mesh;
	}
	const int32 Count = ClampSegments(Segments);
	const float Half = ClampHalfAngle(HalfAngleDeg);
	const float Width = FMath::Clamp(WidthCm, 0.5f, RangeCm * 0.5f);
	const float Inner = RangeCm - Width;
	// The arc: a band between the inner and the outer radius.
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float A0 = -Half + 2.f * Half * Index / Count;
		const float A1 = -Half + 2.f * Half * (Index + 1) / Count;
		AddQuad(Mesh, ArcPoint(Inner, A0, HeightCm), ArcPoint(RangeCm, A0, HeightCm), ArcPoint(RangeCm, A1, HeightCm), ArcPoint(Inner, A1, HeightCm));
	}
	if (Half >= 180.f)
	{
		return Mesh; // all round: no side edges
	}
	// The side edges: apex -> arc end, the band on the inner side. Its inner corners stay inside the sector: at the apex
	// the mitre point on the forward axis WidthCm from both edges, at the arc the point of the inner arc WidthCm from the edge.
	const FVector InnerApex(Width / FMath::Max(FMath::Sin(FMath::DegreesToRadians(Half)), 0.05f), 0.f, HeightCm);
	const float InnerArcOffset = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Width / FMath::Max(Inner, 1.f), 0.f, 1.f)));
	for (const float Sign : { -1.f, 1.f })
	{
		AddQuad(Mesh, FVector(0.f, 0.f, HeightCm), ArcPoint(RangeCm, Sign * Half, HeightCm),
			ArcPoint(Inner, Sign * FMath::Max(Half - InnerArcOffset, 0.f), HeightCm), InnerApex);
	}
	return Mesh;
}

bool VisionConeRules::IsInsideCone(const FVector& Origin, const FVector& Forward, const FVector& Target, float RangeCm, float HalfAngleDeg)
{
	const FVector To = FVector(Target.X - Origin.X, Target.Y - Origin.Y, 0.f);
	const float Distance = To.Size();
	if (Distance > RangeCm)
	{
		return false;
	}
	if (Distance < KINDA_SMALL_NUMBER)
	{
		return true;
	}
	const FVector Facing = FVector(Forward.X, Forward.Y, 0.f).GetSafeNormal();
	if (Facing.IsNearlyZero())
	{
		return false;
	}
	return FVector::DotProduct(Facing, To / Distance) >= FMath::Cos(FMath::DegreesToRadians(ClampHalfAngle(HalfAngleDeg))) - KINDA_SMALL_NUMBER;
}

bool VisionConeRules::ResolveVisible(int32 GlobalMode, bool bActorSetting, bool bAlive)
{
	if (!bAlive || GlobalMode == 0)
	{
		return false;
	}
	return GlobalMode == 1 || bActorSetting;
}

EVisionConeState VisionConeRules::GetEnemyState(bool bOnPatrol, bool bSearching, float Suspicion)
{
	if (!bOnPatrol)
	{
		return EVisionConeState::Alert;
	}
	return bSearching || Suspicion > 0.f ? EVisionConeState::Suspicious : EVisionConeState::Idle;
}

FLinearColor VisionConeRules::GetStateColor(EVisionConeState State, float Suspicion)
{
	// Godot's operative colours used as given (the cone material is unlit); the enemy colours are the port's own.
	const FLinearColor Green(0.12f, 0.88f, 0.35f);
	const FLinearColor Red(0.95f, 0.18f, 0.18f);
	const FLinearColor Idle(0.35f, 0.65f, 1.f);
	const FLinearColor Amber(1.f, 0.75f, 0.2f);
	switch (State)
	{
	case EVisionConeState::Clear: return Green;
	case EVisionConeState::Spotted: return Red;
	case EVisionConeState::Idle: return Idle;
	case EVisionConeState::Suspicious: return FMath::Lerp(Idle, Amber, FMath::Clamp(Suspicion, 0.f, 1.f));
	case EVisionConeState::Alert: return Red;
	default: return Green;
	}
}

FLinearColor VisionConeRules::StepColor(const FLinearColor& Current, const FLinearColor& Target, float DeltaSeconds, float Speed)
{
	return FMath::Lerp(Current, Target, FMath::Clamp(DeltaSeconds * Speed, 0.f, 1.f));
}
