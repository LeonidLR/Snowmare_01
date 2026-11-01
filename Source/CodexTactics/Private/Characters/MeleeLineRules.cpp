// MeleeLineRules: melee blows and the Cutter's pounce never pass a wall (game designer bug 06, 2026-11-01). UE-only rule (no Godot reference;
// Godot enemy_base.gd can_melee_attack / enemy_cutter.gd start_jump_attack check the distance only).

#include "Characters/MeleeLineRules.h"

FVector MeleeLineRules::LinePoint(const FVector& Feet)
{
	return Feet + FVector(0.f, 0.f, LineHeightCm);
}

EMeleeLineHit MeleeLineRules::ClassifyHit(bool bIsTarget, bool bIsPawn, bool bBlocksBodies)
{
	if (bIsTarget)
	{
		return EMeleeLineHit::Target;
	}
	if (bIsPawn)
	{
		return EMeleeLineHit::Pawn;
	}
	return bBlocksBodies ? EMeleeLineHit::Solid : EMeleeLineHit::NonSolid;
}

bool MeleeLineRules::IsLineClear(TConstArrayView<EMeleeLineHit> HitsInOrder)
{
	for (const EMeleeLineHit Hit : HitsInOrder)
	{
		if (Hit == EMeleeLineHit::Target)
		{
			return true;
		}
		if (Hit == EMeleeLineHit::Solid)
		{
			return false;
		}
	}
	return true;
}

bool MeleeLineRules::MayStrike(bool bInReach, bool bLineClear)
{
	return bInReach && bLineClear;
}

ECutterLeapCheck MeleeLineRules::EvaluateLeap(float DistanceCm, float HeightDifferenceCm, float MinDistanceCm, float MaxDistanceCm, bool bLineClear,
	bool bArcClear)
{
	if (DistanceCm < MinDistanceCm || DistanceCm > MaxDistanceCm)
	{
		return ECutterLeapCheck::OutOfRange;
	}
	if (FMath::Abs(HeightDifferenceCm) > MaxLeapHeightCm)
	{
		return ECutterLeapCheck::TooHigh;
	}
	if (!bLineClear)
	{
		return ECutterLeapCheck::WallInLine;
	}
	return bArcClear ? ECutterLeapCheck::Leap : ECutterLeapCheck::PathBlocked;
}

const TCHAR* MeleeLineRules::LeapCheckName(ECutterLeapCheck Check)
{
	switch (Check)
	{
	case ECutterLeapCheck::Leap: return TEXT("leap");
	case ECutterLeapCheck::OutOfRange: return TEXT("out of range");
	case ECutterLeapCheck::TooHigh: return TEXT("too high");
	case ECutterLeapCheck::WallInLine: return TEXT("a wall between");
	case ECutterLeapCheck::PathBlocked: return TEXT("the flight path blocked");
	default: return TEXT("?");
	}
}

FVector MeleeLineRules::LeapVelocity(const FVector& Start, const FVector& Landing, float FlightSeconds, float Gravity, float MaxHorizontalCm)
{
	const float Flight = FMath::Max(0.05f, FlightSeconds);
	FVector Horizontal(Landing.X - Start.X, Landing.Y - Start.Y, 0.f);
	if (Horizontal.Size() > MaxHorizontalCm)
	{
		Horizontal = Horizontal.GetSafeNormal() * MaxHorizontalCm;
	}
	const float DeltaZ = Landing.Z - Start.Z;
	return Horizontal / Flight + FVector(0.f, 0.f, (DeltaZ + 0.5f * Gravity * Flight * Flight) / Flight);
}

FVector MeleeLineRules::LeapArcPoint(const FVector& Start, const FVector& Velocity, float Gravity, float Seconds)
{
	return Start + Velocity * Seconds - FVector(0.f, 0.f, 0.5f * Gravity * Seconds * Seconds);
}

TArray<FVector> MeleeLineRules::LeapArc(const FVector& Start, const FVector& Landing, float FlightSeconds, float Gravity, float MaxHorizontalCm,
	int32 Segments)
{
	const int32 Count = FMath::Max(1, Segments);
	const float Flight = FMath::Max(0.05f, FlightSeconds);
	const FVector Velocity = LeapVelocity(Start, Landing, Flight, Gravity, MaxHorizontalCm);
	TArray<FVector> Points;
	Points.Reserve(Count + 1);
	for (int32 Index = 0; Index <= Count; ++Index)
	{
		Points.Add(LeapArcPoint(Start, Velocity, Gravity, Flight * Index / Count));
	}
	return Points;
}

bool MeleeLineRules::PounceReaches(float DistanceCm, float HeightDifferenceCm, float RadiusCm, bool bLineClear)
{
	return DistanceCm <= RadiusCm && FMath::Abs(HeightDifferenceCm) <= 150.f && bLineClear;
}
