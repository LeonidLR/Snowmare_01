// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Gorky17Types.generated.h"

UENUM(BlueprintType)
enum class EGorkyFacing : uint8
{
	North = 0 UMETA(DisplayName = "North"),
	East = 1 UMETA(DisplayName = "East"),
	South = 2 UMETA(DisplayName = "South"),
	West = 3 UMETA(DisplayName = "West"),
	NorthEast = 4 UMETA(DisplayName = "NorthEast"),
	SouthEast = 5 UMETA(DisplayName = "SouthEast"),
	SouthWest = 6 UMETA(DisplayName = "SouthWest"),
	NorthWest = 7 UMETA(DisplayName = "NorthWest")
};

UENUM(BlueprintType)
enum class EGorkyArcZone : uint8
{
	Front UMETA(DisplayName = "Front"),
	Flank UMETA(DisplayName = "Flank"),
	Rear UMETA(DisplayName = "Rear")
};

UENUM(BlueprintType)
enum class EGorkyOccupantType : uint8
{
	None UMETA(DisplayName = "None"),
	Squad UMETA(DisplayName = "Squad"),
	Enemy UMETA(DisplayName = "Enemy"),
	Turret UMETA(DisplayName = "Turret"),
	Barrel UMETA(DisplayName = "Barrel"),
	Barricade UMETA(DisplayName = "Barricade"),
	Mine UMETA(DisplayName = "Mine"),
	Obstacle UMETA(DisplayName = "Obstacle")
};

UENUM(BlueprintType)
enum class EGorkyActionType : uint8
{
	Move UMETA(DisplayName = "Move"),
	Attack UMETA(DisplayName = "Attack"),
	TurnFacing UMETA(DisplayName = "Turn Facing"),
	UseItem UMETA(DisplayName = "Use Item"),
	PushBarrel UMETA(DisplayName = "Push Barrel"),
	PassTurn UMETA(DisplayName = "Pass Turn")
};

USTRUCT(BlueprintType)
struct CODEXTACTICS_API FGorkyArcResult
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics")
	EGorkyArcZone Arc = EGorkyArcZone::Front;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics")
	float DamageMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics")
	float EffectiveArmorMultiplier = 1.0f;
};

class CODEXTACTICS_API FGorky17Utils
{
public:
	static const TArray<FIntPoint>& GetCardinalDirections();
	static const TArray<FIntPoint>& GetDiagonalDirections();
	static const TArray<FIntPoint>& GetAllDirections();

	static FIntPoint FacingToVector(EGorkyFacing Facing);
	static EGorkyFacing VectorToFacing(const FIntPoint& Vector);

	static FGorkyArcResult CalculateAttackArc(
		const FIntPoint& AttackerGrid,
		const FIntPoint& DefenderGrid,
		const FIntPoint& DefenderFacing);

	static FGorkyArcResult CalculateAttackArc(
		const FIntPoint& AttackerGrid,
		const FIntPoint& DefenderGrid,
		EGorkyFacing DefenderFacing)
	{
		return CalculateAttackArc(AttackerGrid, DefenderGrid, FacingToVector(DefenderFacing));
	}
};
