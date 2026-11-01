// Where a combat-lab ambush hides: wall ends, doorways, authored points (game designer answer 1, 2026-11-01, docs/design/COMBAT_LAB.md).
// UE-only, no Godot reference. Tests: CodexTactics.Combat.AmbushSpots.*.

#include "Combat/AmbushSpotRules.h"

#include "Combat/CombatPressureRules.h"

namespace AmbushSpotLocal
{
	int32 Priority(EAmbushSpotKind Kind)
	{
		switch (Kind)
		{
		case EAmbushSpotKind::Authored: return 3;
		case EAmbushSpotKind::Doorway: return 2;
		case EAmbushSpotKind::WallEnd: return 1;
		default: return 0;
		}
	}

	/** Adds Spot unless one within MergeCm is already there (a higher kind replaces it). */
	void AddMerged(TArray<FAmbushSpot>& Out, const FAmbushSpot& Spot, float MergeCm)
	{
		for (FAmbushSpot& Each : Out)
		{
			if (FVector2D::Distance(Each.Position, Spot.Position) <= MergeCm)
			{
				if (Priority(Spot.Kind) > Priority(Each.Kind))
				{
					Each = Spot;
				}
				return;
			}
		}
		Out.Add(Spot);
	}
}

float AmbushSpotRules::HideClearanceCm(float WallClearanceCm, const FAmbushSpotParams& Params)
{
	return FMath::Max(0.f, FMath::Min(WallClearanceCm, Params.WallStandoffCm - 10.f));
}

void AmbushSpotRules::LongAxis(const FPressureBox& Box, FVector2D& OutAxis, float& OutHalfLength, float& OutHalfThickness)
{
	const FVector2D AxisX = Box.AxisX.GetSafeNormal().IsNearlyZero() ? FVector2D(1.0, 0.0) : Box.AxisX.GetSafeNormal();
	if (Box.HalfSize.Y > Box.HalfSize.X)
	{
		OutAxis = FVector2D(-AxisX.Y, AxisX.X);
		OutHalfLength = static_cast<float>(Box.HalfSize.Y);
		OutHalfThickness = static_cast<float>(Box.HalfSize.X);
		return;
	}
	OutAxis = AxisX;
	OutHalfLength = static_cast<float>(Box.HalfSize.X);
	OutHalfThickness = static_cast<float>(Box.HalfSize.Y);
}

FVector2D AmbushSpotRules::BoxPoint(const FPressureBox& Box, float Along, float Across)
{
	FVector2D Axis;
	float HalfLength = 0.f;
	float HalfThickness = 0.f;
	LongAxis(Box, Axis, HalfLength, HalfThickness);
	const FVector2D Side(-Axis.Y, Axis.X);
	return Box.Center + Axis * Along + Side * Across;
}

TArray<FAmbushWallEnd> AmbushSpotRules::FreeEnds(const TArray<FPressureBox>& Boxes, const FAmbushSpotParams& Params)
{
	TArray<FAmbushWallEnd> Out;
	for (int32 Index = 0; Index < Boxes.Num(); ++Index)
	{
		FVector2D Axis;
		float HalfLength = 0.f;
		float HalfThickness = 0.f;
		LongAxis(Boxes[Index], Axis, HalfLength, HalfThickness);
		if (2.f * HalfLength < Params.MinWallLengthCm)
		{
			continue;
		}
		for (const int32 Sign : { 1, -1 })
		{
			const FVector2D Beyond = BoxPoint(Boxes[Index], Sign * (HalfLength + Params.JoinedGapCm), 0.f);
			bool bJoined = false;
			for (int32 Other = 0; Other < Boxes.Num() && !bJoined; ++Other)
			{
				bJoined = Other != Index && CombatPressureRules::DistanceToBox(Beyond, Boxes[Other]) < Params.JoinedGapCm;
			}
			if (!bJoined)
			{
				Out.Add({ Index, Sign, BoxPoint(Boxes[Index], Sign * HalfLength, 0.f), Axis * static_cast<double>(Sign) });
			}
		}
	}
	return Out;
}

bool AmbushSpotRules::IsDoorwayPair(const FAmbushWallEnd& A, const FAmbushWallEnd& B, const FAmbushSpotParams& Params, float* OutGapCm)
{
	if (A.Box == B.Box)
	{
		return false;
	}
	const FVector2D Delta = B.Point - A.Point;
	const float Gap = static_cast<float>(Delta.Size());
	if (OutGapCm)
	{
		*OutGapCm = Gap;
	}
	if (Gap < Params.DoorwayMinCm || Gap > Params.DoorwayMaxCm)
	{
		return false;
	}
	const float Parallel = FMath::Abs(static_cast<float>(FVector2D::DotProduct(A.Outward, B.Outward)));
	if (Parallel < FMath::Cos(FMath::DegreesToRadians(20.f)))
	{
		return false;
	}
	// Facing each other along the wall line: B ahead of A's end, A ahead of B's.
	return FVector2D::DotProduct(Delta, A.Outward) >= 0.8 * Gap && FVector2D::DotProduct(-Delta, B.Outward) >= 0.8 * Gap;
}

TArray<FAmbushSpot> AmbushSpotRules::WallSpots(const TArray<FPressureBox>& Boxes, const FAmbushSpotParams& Params)
{
	const TArray<FAmbushWallEnd> Ends = FreeEnds(Boxes, Params);
	TArray<FAmbushSpot> Out;
	for (const FAmbushWallEnd& End : Ends)
	{
		bool bDoorway = false;
		for (const FAmbushWallEnd& Other : Ends)
		{
			bDoorway |= IsDoorwayPair(End, Other, Params);
		}
		FVector2D Axis;
		float HalfLength = 0.f;
		float HalfThickness = 0.f;
		LongAxis(Boxes[End.Box], Axis, HalfLength, HalfThickness);
		const float Along = End.Sign * FMath::Max(0.f, HalfLength - Params.EndInsetCm);
		const FVector2D Side(-Axis.Y, Axis.X);
		for (const float Face : { 1.f, -1.f })
		{
			FAmbushSpot Spot;
			Spot.Position = BoxPoint(Boxes[End.Box], Along, Face * (HalfThickness + Params.WallStandoffCm));
			Spot.Kind = bDoorway ? EAmbushSpotKind::Doorway : EAmbushSpotKind::WallEnd;
			Spot.Box = End.Box;
			Spot.Normal = Side * static_cast<double>(Face);
			AmbushSpotLocal::AddMerged(Out, Spot, Params.MergeCm);
		}
	}
	return Out;
}

TArray<FAmbushSpot> AmbushSpotRules::Candidates(const TArray<FPressureBox>& Boxes, const TArray<FVector2D>& Authored, const FAmbushSpotParams& Params)
{
	TArray<FAmbushSpot> Out;
	for (const FVector2D& Point : Authored)
	{
		FAmbushSpot Spot;
		Spot.Position = Point;
		Spot.Kind = EAmbushSpotKind::Authored;
		AmbushSpotLocal::AddMerged(Out, Spot, Params.MergeCm);
	}
	for (const FAmbushSpot& Spot : WallSpots(Boxes, Params))
	{
		AmbushSpotLocal::AddMerged(Out, Spot, Params.MergeCm);
	}
	return Out;
}

float AmbushSpotRules::KindBonus(EAmbushSpotKind Kind, const FAmbushSpotParams& Params)
{
	switch (Kind)
	{
	case EAmbushSpotKind::Authored: return Params.AuthoredBonus;
	case EAmbushSpotKind::Doorway: return Params.DoorwayBonus;
	default: return 0.f;
	}
}

const TCHAR* AmbushSpotRules::KindName(EAmbushSpotKind Kind)
{
	switch (Kind)
	{
	case EAmbushSpotKind::WallEnd: return TEXT("wall end");
	case EAmbushSpotKind::Doorway: return TEXT("doorway");
	case EAmbushSpotKind::Authored: return TEXT("authored");
	default: return TEXT("none");
	}
}
