// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tactics/Gorky17Types.h"

const TArray<FIntPoint>& FGorky17Utils::GetCardinalDirections()
{
	static const TArray<FIntPoint> CardinalDirs = {
		FIntPoint(0, -1), // North
		FIntPoint(1, 0),  // East
		FIntPoint(0, 1),  // South
		FIntPoint(-1, 0)  // West
	};
	return CardinalDirs;
}

const TArray<FIntPoint>& FGorky17Utils::GetDiagonalDirections()
{
	static const TArray<FIntPoint> DiagonalDirs = {
		FIntPoint(1, -1),  // NorthEast
		FIntPoint(1, 1),   // SouthEast
		FIntPoint(-1, 1),  // SouthWest
		FIntPoint(-1, -1)  // NorthWest
	};
	return DiagonalDirs;
}

const TArray<FIntPoint>& FGorky17Utils::GetAllDirections()
{
	static const TArray<FIntPoint> AllDirs = {
		FIntPoint(0, -1),  // North
		FIntPoint(1, 0),   // East
		FIntPoint(0, 1),   // South
		FIntPoint(-1, 0),  // West
		FIntPoint(1, -1),  // NorthEast
		FIntPoint(1, 1),   // SouthEast
		FIntPoint(-1, 1),  // SouthWest
		FIntPoint(-1, -1)  // NorthWest
	};
	return AllDirs;
}

FIntPoint FGorky17Utils::FacingToVector(EGorkyFacing Facing)
{
	switch (Facing)
	{
	case EGorkyFacing::North:     return FIntPoint(0, -1);
	case EGorkyFacing::East:      return FIntPoint(1, 0);
	case EGorkyFacing::South:     return FIntPoint(0, 1);
	case EGorkyFacing::West:      return FIntPoint(-1, 0);
	case EGorkyFacing::NorthEast: return FIntPoint(1, -1);
	case EGorkyFacing::SouthEast: return FIntPoint(1, 1);
	case EGorkyFacing::SouthWest: return FIntPoint(-1, 1);
	case EGorkyFacing::NorthWest: return FIntPoint(-1, -1);
	default:                      return FIntPoint(0, 1);
	}
}

EGorkyFacing FGorky17Utils::VectorToFacing(const FIntPoint& Vector)
{
	int32 Sx = FMath::Clamp(Vector.X, -1, 1);
	int32 Sy = FMath::Clamp(Vector.Y, -1, 1);

	if (Sx != 0 && Sy != 0)
	{
		if (Sx > 0 && Sy < 0) return EGorkyFacing::NorthEast;
		if (Sx > 0 && Sy > 0) return EGorkyFacing::SouthEast;
		if (Sx < 0 && Sy > 0) return EGorkyFacing::SouthWest;
		if (Sx < 0 && Sy < 0) return EGorkyFacing::NorthWest;
	}
	else if (Sx > 0)
	{
		return EGorkyFacing::East;
	}
	else if (Sx < 0)
	{
		return EGorkyFacing::West;
	}
	else if (Sy > 0)
	{
		return EGorkyFacing::South;
	}
	else if (Sy < 0)
	{
		return EGorkyFacing::North;
	}

	return EGorkyFacing::South;
}

FGorkyArcResult FGorky17Utils::CalculateAttackArc(
	const FIntPoint& AttackerGrid,
	const FIntPoint& DefenderGrid,
	const FIntPoint& DefenderFacing)
{
	FGorkyArcResult Result;

	FVector2D Delta(AttackerGrid.X - DefenderGrid.X, AttackerGrid.Y - DefenderGrid.Y);
	if (Delta.SizeSquared() < 0.001f)
	{
		Result.Arc = EGorkyArcZone::Front;
		Result.DamageMultiplier = 1.0f;
		Result.EffectiveArmorMultiplier = 1.0f;
		return Result;
	}

	FVector2D AttackDir = Delta.GetSafeNormal();
	FVector2D DefVec = FVector2D(DefenderFacing.X, DefenderFacing.Y).GetSafeNormal();
	if (DefVec.IsNearlyZero())
	{
		DefVec = FVector2D(0.f, 1.f);
	}

	float Dot = FVector2D::DotProduct(AttackDir, DefVec);

	if (Dot >= 0.5f)
	{
		Result.Arc = EGorkyArcZone::Front;
		Result.DamageMultiplier = 1.0f;
		Result.EffectiveArmorMultiplier = 1.0f;
	}
	else if (Dot <= -0.5f)
	{
		// Rear strike: 1.75x damage, ignores armor (0.0x)
		Result.Arc = EGorkyArcZone::Rear;
		Result.DamageMultiplier = 1.75f;
		Result.EffectiveArmorMultiplier = 0.0f;
	}
	else
	{
		// Flank strike: 1.25x damage, 50% armor shred (0.5x)
		Result.Arc = EGorkyArcZone::Flank;
		Result.DamageMultiplier = 1.25f;
		Result.EffectiveArmorMultiplier = 0.5f;
	}

	return Result;
}
