// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Tactics/Gorky17Types.h"
#include "GorkyGridManager.generated.h"

USTRUCT(BlueprintType)
struct CODEXTACTICS_API FGorkyCellData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics")
	bool bWalkable = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics")
	TWeakObjectPtr<AActor> Occupant = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics")
	EGorkyOccupantType OccupantType = EGorkyOccupantType::None;
};

/**
 * Orthogonal tactical grid manager for Gorky 17 turn-based combat.
 * Handles cell conversion, occupancy, AP-budgeted reachable zone BFS, and A* pathfinding.
 */
UCLASS(BlueprintType)
class CODEXTACTICS_API UGorkyGridManager : public UObject
{
	GENERATED_BODY()

public:
	UGorkyGridManager();

	UFUNCTION(BlueprintCallable, Category = "Tactics|Grid")
	void Setup(
		const FVector& CenterWorld,
		const FIntPoint& InGridSize,
		float InCellSize = 150.f,
		float InGroundZ = 0.f);

	void SetupDefault(const FVector& CenterWorld, float InGroundZ = 0.f)
	{
		Setup(CenterWorld, FIntPoint(14, 14), 150.f, InGroundZ);
	}

	UFUNCTION(BlueprintPure, Category = "Tactics|Grid")
	bool IsValidCell(const FIntPoint& Pos) const;

	UFUNCTION(BlueprintPure, Category = "Tactics|Grid")
	FIntPoint WorldToGrid(const FVector& WorldPos) const;

	UFUNCTION(BlueprintPure, Category = "Tactics|Grid")
	FVector GridToWorld(const FIntPoint& GridPos) const;

	UFUNCTION(BlueprintCallable, Category = "Tactics|Grid")
	void SetOccupant(const FIntPoint& Pos, AActor* Occupant, EGorkyOccupantType Type);

	UFUNCTION(BlueprintCallable, Category = "Tactics|Grid")
	void ClearOccupant(const FIntPoint& Pos);

	UFUNCTION(BlueprintPure, Category = "Tactics|Grid")
	AActor* GetOccupant(const FIntPoint& Pos) const;

	UFUNCTION(BlueprintPure, Category = "Tactics|Grid")
	EGorkyOccupantType GetOccupantType(const FIntPoint& Pos) const;

	UFUNCTION(BlueprintPure, Category = "Tactics|Grid")
	bool IsCellOccupied(const FIntPoint& Pos) const;

	UFUNCTION(BlueprintPure, Category = "Tactics|Grid")
	bool IsCellWalkable(const FIntPoint& Pos) const;

	UFUNCTION(BlueprintPure, Category = "Tactics|Grid")
	TArray<FIntPoint> GetNeighbors(const FIntPoint& Pos, bool bAllowDiagonals = true) const;

	TMap<FIntPoint, int32> GetReachableCells(
		const FIntPoint& StartPos,
		int32 APBudget,
		const TSet<FIntPoint>& ForbiddenCells = TSet<FIntPoint>()) const;

	TArray<FIntPoint> FindPath(
		const FIntPoint& StartPos,
		const FIntPoint& TargetPos,
		int32 APBudget,
		const TSet<FIntPoint>& ForbiddenCells = TSet<FIntPoint>()) const;

	UFUNCTION(BlueprintPure, Category = "Tactics|Grid")
	FIntPoint FindNearestFreeCell(const FIntPoint& PreferredCell) const;

	/**
	 * Path to the free neighbour of TargetPos closest to StartPos (orthogonal neighbours only when bOrthogonalOnly);
	 * empty when already adjacent. Retries with budgets +4, +8 and 20 AP when nothing fits (Godot find_path_to_adjacent).
	 */
	TArray<FIntPoint> FindPathToAdjacent(const FIntPoint& StartPos, const FIntPoint& TargetPos, int32 APBudget, bool bOrthogonalOnly = false,
		const TSet<FIntPoint>& ForbiddenCells = TSet<FIntPoint>()) const;

	/** Path to the reachable cell outside ForbiddenCells closest to TargetPos (Godot find_path_closest_to_target_outside_forbidden). */
	TArray<FIntPoint> FindPathClosestOutsideForbidden(const FIntPoint& StartPos, const FIntPoint& TargetPos, int32 APBudget,
		const TSet<FIntPoint>& ForbiddenCells) const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactics|Grid")
	FIntPoint GridSize = FIntPoint(14, 14);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactics|Grid")
	float CellSize = 150.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactics|Grid")
	int32 DiagonalAPCost = 2;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactics|Grid")
	FVector OriginWorld = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactics|Grid")
	float GroundZ = 0.f;

private:
	int32 CalcHeuristic(const FIntPoint& From, const FIntPoint& To) const;

	TMap<FIntPoint, FGorkyCellData> Cells;
};
