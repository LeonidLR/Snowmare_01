// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tactics/GorkyGridManager.h"

UGorkyGridManager::UGorkyGridManager()
	: GridSize(14, 14)
	, CellSize(150.f)
	, DiagonalAPCost(2)
	, OriginWorld(FVector::ZeroVector)
	, GroundZ(0.f)
{
}

void UGorkyGridManager::Setup(
	const FVector& CenterWorld,
	const FIntPoint& InGridSize,
	float InCellSize,
	float InGroundZ)
{
	GridSize = InGridSize;
	CellSize = InCellSize;
	GroundZ = InGroundZ;

	// Center the grid so CenterWorld is in the middle
	OriginWorld = CenterWorld - FVector(float(GridSize.X) * CellSize * 0.5f, float(GridSize.Y) * CellSize * 0.5f, 0.f);
	OriginWorld.Z = GroundZ;

	Cells.Empty();
	Cells.Reserve(GridSize.X * GridSize.Y);

	for (int32 X = 0; X < GridSize.X; ++X)
	{
		for (int32 Y = 0; Y < GridSize.Y; ++Y)
		{
			FIntPoint Pos(X, Y);
			FGorkyCellData Data;
			Data.bWalkable = true;
			Data.Occupant = nullptr;
			Data.OccupantType = EGorkyOccupantType::None;
			Cells.Add(Pos, Data);
		}
	}
}

bool UGorkyGridManager::IsValidCell(const FIntPoint& Pos) const
{
	return Pos.X >= 0 && Pos.X < GridSize.X && Pos.Y >= 0 && Pos.Y < GridSize.Y;
}

FIntPoint UGorkyGridManager::WorldToGrid(const FVector& WorldPos) const
{
	FVector Diff = WorldPos - OriginWorld;
	int32 Gx = FMath::Clamp(FMath::FloorToInt(Diff.X / CellSize), 0, GridSize.X - 1);
	int32 Gy = FMath::Clamp(FMath::FloorToInt(Diff.Y / CellSize), 0, GridSize.Y - 1);
	return FIntPoint(Gx, Gy);
}

FVector UGorkyGridManager::GridToWorld(const FIntPoint& GridPos) const
{
	float Wx = OriginWorld.X + (float(GridPos.X) + 0.5f) * CellSize;
	float Wy = OriginWorld.Y + (float(GridPos.Y) + 0.5f) * CellSize;
	return FVector(Wx, Wy, GroundZ);
}

void UGorkyGridManager::SetOccupant(const FIntPoint& Pos, AActor* Occupant, EGorkyOccupantType Type)
{
	if (!IsValidCell(Pos))
	{
		return;
	}

	FGorkyCellData* Data = Cells.Find(Pos);
	if (Data)
	{
		Data->Occupant = Occupant;
		Data->OccupantType = Type;
		// Mines do not block movement
		Data->bWalkable = (Type == EGorkyOccupantType::None || Type == EGorkyOccupantType::Mine);
	}
}

void UGorkyGridManager::ClearOccupant(const FIntPoint& Pos)
{
	if (!IsValidCell(Pos))
	{
		return;
	}

	FGorkyCellData* Data = Cells.Find(Pos);
	if (Data)
	{
		Data->Occupant = nullptr;
		Data->OccupantType = EGorkyOccupantType::None;
		Data->bWalkable = true;
	}
}

AActor* UGorkyGridManager::GetOccupant(const FIntPoint& Pos) const
{
	if (!IsValidCell(Pos))
	{
		return nullptr;
	}

	const FGorkyCellData* Data = Cells.Find(Pos);
	if (!Data)
	{
		return nullptr;
	}

	if (Data->Occupant.IsStale())
	{
		const_cast<UGorkyGridManager*>(this)->ClearOccupant(Pos);
		return nullptr;
	}

	return Data->Occupant.Get();
}

EGorkyOccupantType UGorkyGridManager::GetOccupantType(const FIntPoint& Pos) const
{
	if (!IsValidCell(Pos))
	{
		return EGorkyOccupantType::None;
	}

	const FGorkyCellData* Data = Cells.Find(Pos);
	if (!Data)
	{
		return EGorkyOccupantType::None;
	}

	if (Data->Occupant.IsStale())
	{
		const_cast<UGorkyGridManager*>(this)->ClearOccupant(Pos);
		return EGorkyOccupantType::None;
	}

	return Data->OccupantType;
}

bool UGorkyGridManager::IsCellOccupied(const FIntPoint& Pos) const
{
	return GetOccupant(Pos) != nullptr;
}

bool UGorkyGridManager::IsCellWalkable(const FIntPoint& Pos) const
{
	if (!IsValidCell(Pos))
	{
		return false;
	}

	const FGorkyCellData* Data = Cells.Find(Pos);
	if (!Data)
	{
		return false;
	}

	if (Data->Occupant.IsStale())
	{
		const_cast<UGorkyGridManager*>(this)->ClearOccupant(Pos);
		return true;
	}

	return Data->bWalkable;
}

TArray<FIntPoint> UGorkyGridManager::GetNeighbors(const FIntPoint& Pos, bool bAllowDiagonals) const
{
	TArray<FIntPoint> Result;
	Result.Reserve(8);

	// 1. Cardinal neighbors
	for (const FIntPoint& D : FGorky17Utils::GetCardinalDirections())
	{
		FIntPoint N = Pos + D;
		if (IsValidCell(N))
		{
			Result.Add(N);
		}
	}

	// 2. Diagonal neighbors with permissive corner-cutting
	if (bAllowDiagonals)
	{
		for (const FIntPoint& D : FGorky17Utils::GetDiagonalDirections())
		{
			FIntPoint N = Pos + D;
			if (IsValidCell(N))
			{
				// Allowed if at least one adjacent orthogonal cell is walkable
				FIntPoint Adj1(Pos.X + D.X, Pos.Y);
				FIntPoint Adj2(Pos.X, Pos.Y + D.Y);
				if (IsCellWalkable(Adj1) || IsCellWalkable(Adj2))
				{
					Result.Add(N);
				}
			}
		}
	}

	return Result;
}

TMap<FIntPoint, int32> UGorkyGridManager::GetReachableCells(
	const FIntPoint& StartPos,
	int32 APBudget,
	const TSet<FIntPoint>& ForbiddenCells) const
{
	TMap<FIntPoint, int32> CostMap;
	if (!IsValidCell(StartPos) || APBudget <= 0)
	{
		return CostMap;
	}

	CostMap.Add(StartPos, 0);

	TArray<FIntPoint> Queue;
	Queue.Add(StartPos);
	int32 HeadIndex = 0;

	while (HeadIndex < Queue.Num())
	{
		FIntPoint Current = Queue[HeadIndex++];
		int32 CurrentCost = CostMap[Current];

		if (CurrentCost >= APBudget)
		{
			continue;
		}

		for (const FIntPoint& NextPos : GetNeighbors(Current))
		{
			if (ForbiddenCells.Contains(NextPos) && NextPos != StartPos)
			{
				continue;
			}

			if (!IsCellWalkable(NextPos))
			{
				continue;
			}

			bool bIsDiag = (NextPos.X != Current.X) && (NextPos.Y != Current.Y);
			int32 StepCost = bIsDiag ? DiagonalAPCost : 1;
			int32 NewCost = CurrentCost + StepCost;

			if (NewCost <= APBudget)
			{
				int32* ExistingCost = CostMap.Find(NextPos);
				if (!ExistingCost || NewCost < *ExistingCost)
				{
					CostMap.Add(NextPos, NewCost);
					Queue.Add(NextPos);
				}
			}
		}
	}

	return CostMap;
}

int32 UGorkyGridManager::CalcHeuristic(const FIntPoint& From, const FIntPoint& To) const
{
	int32 Dx = FMath::Abs(To.X - From.X);
	int32 Dy = FMath::Abs(To.Y - From.Y);

	if (DiagonalAPCost <= 1)
	{
		return FMath::Max(Dx, Dy);
	}
	else
	{
		return FMath::Min(Dx, Dy) * DiagonalAPCost + (FMath::Max(Dx, Dy) - FMath::Min(Dx, Dy));
	}
}

TArray<FIntPoint> UGorkyGridManager::FindPath(
	const FIntPoint& StartPos,
	const FIntPoint& TargetPos,
	int32 APBudget,
	const TSet<FIntPoint>& ForbiddenCells) const
{
	TArray<FIntPoint> EmptyPath;
	if (!IsValidCell(StartPos) || !IsValidCell(TargetPos))
	{
		return EmptyPath;
	}

	if (ForbiddenCells.Contains(TargetPos) && TargetPos != StartPos)
	{
		return EmptyPath;
	}

	if (!IsCellWalkable(TargetPos) && TargetPos != StartPos)
	{
		return EmptyPath;
	}

	if (StartPos == TargetPos)
	{
		return EmptyPath; // Godot find_path: nothing to walk
	}

	TArray<FIntPoint> Frontier;
	Frontier.Add(StartPos);

	TMap<FIntPoint, FIntPoint> CameFrom;
	CameFrom.Add(StartPos, StartPos);

	TMap<FIntPoint, int32> CostSoFar;
	CostSoFar.Add(StartPos, 0);

	bool bFound = false;

	while (Frontier.Num() > 0)
	{
		// Find lowest estimated cost node
		int32 BestIndex = 0;
		int32 BestF = CostSoFar[Frontier[0]] + CalcHeuristic(Frontier[0], TargetPos);

		for (int32 i = 1; i < Frontier.Num(); ++i)
		{
			int32 F = CostSoFar[Frontier[i]] + CalcHeuristic(Frontier[i], TargetPos);
			if (F < BestF)
			{
				BestF = F;
				BestIndex = i;
			}
		}

		FIntPoint Current = Frontier[BestIndex];
		Frontier.RemoveAt(BestIndex);

		if (Current == TargetPos)
		{
			bFound = true;
			break;
		}

		int32 CurrentCost = CostSoFar[Current];

		for (const FIntPoint& NextPos : GetNeighbors(Current))
		{
			if (ForbiddenCells.Contains(NextPos) && NextPos != StartPos)
			{
				continue;
			}

			if (!IsCellWalkable(NextPos) && NextPos != TargetPos)
			{
				continue;
			}

			bool bIsDiag = (NextPos.X != Current.X) && (NextPos.Y != Current.Y);
			int32 StepCost = bIsDiag ? DiagonalAPCost : 1;
			int32 NewCost = CurrentCost + StepCost;

			if (NewCost <= APBudget)
			{
				int32* ExistingCost = CostSoFar.Find(NextPos);
				if (!ExistingCost || NewCost < *ExistingCost)
				{
					CostSoFar.Add(NextPos, NewCost);
					CameFrom.Add(NextPos, Current);
					Frontier.Add(NextPos);
				}
			}
		}
	}

	if (!bFound)
	{
		return EmptyPath;
	}

	// Reconstruct path
	TArray<FIntPoint> Path;
	FIntPoint Curr = TargetPos;
	while (Curr != StartPos)
	{
		Path.Add(Curr);
		Curr = CameFrom[Curr];
	}
	// Godot find_path: the steps only, without the start cell (a start cell here made every walk begin with an empty
	// step — half a second of walking on the spot — and cost enemies an extra AP).
	Algo::Reverse(Path);

	return Path;
}

FIntPoint UGorkyGridManager::FindNearestFreeCell(const FIntPoint& PreferredCell) const
{
	if (IsValidCell(PreferredCell) && IsCellWalkable(PreferredCell) && !IsCellOccupied(PreferredCell))
	{
		return PreferredCell;
	}

	// Search outward in rings
	for (int32 Radius = 1; Radius < FMath::Max(GridSize.X, GridSize.Y); ++Radius)
	{
		for (int32 Dx = -Radius; Dx <= Radius; ++Dx)
		{
			for (int32 Dy = -Radius; Dy <= Radius; ++Dy)
			{
				if (FMath::Abs(Dx) != Radius && FMath::Abs(Dy) != Radius)
				{
					continue;
				}

				FIntPoint Candidate = PreferredCell + FIntPoint(Dx, Dy);
				if (IsValidCell(Candidate) && IsCellWalkable(Candidate) && !IsCellOccupied(Candidate))
				{
					return Candidate;
				}
			}
		}
	}

	return PreferredCell;
}

TArray<FIntPoint> UGorkyGridManager::FindPathToAdjacent(const FIntPoint& StartPos, const FIntPoint& TargetPos, int32 APBudget, bool bOrthogonalOnly,
	const TSet<FIntPoint>& ForbiddenCells) const
{
	TArray<FIntPoint> BestPath;
	if (!IsValidCell(StartPos) || !IsValidCell(TargetPos))
	{
		return BestPath;
	}
	const int32 Manhattan = FMath::Abs(TargetPos.X - StartPos.X) + FMath::Abs(TargetPos.Y - StartPos.Y);
	const int32 Chebyshev = FMath::Max(FMath::Abs(TargetPos.X - StartPos.X), FMath::Abs(TargetPos.Y - StartPos.Y));
	if ((bOrthogonalOnly ? Manhattan : Chebyshev) <= 1)
	{
		return BestPath;
	}

	TArray<FIntPoint> Neighbours;
	if (bOrthogonalOnly)
	{
		for (const FIntPoint& D : FGorky17Utils::GetCardinalDirections())
		{
			if (IsValidCell(TargetPos + D))
			{
				Neighbours.Add(TargetPos + D);
			}
		}
	}
	else
	{
		Neighbours = GetNeighbors(TargetPos);
	}
	auto DistanceToStart = [&StartPos, bOrthogonalOnly](const FIntPoint& Cell)
	{
		const int32 DX = FMath::Abs(Cell.X - StartPos.X);
		const int32 DY = FMath::Abs(Cell.Y - StartPos.Y);
		return bOrthogonalOnly ? DX + DY : FMath::Max(DX, DY);
	};
	Neighbours.StableSort([&DistanceToStart](const FIntPoint& A, const FIntPoint& B) { return DistanceToStart(A) < DistanceToStart(B); });

	for (const FIntPoint& Cell : Neighbours)
	{
		if (ForbiddenCells.Contains(Cell) || !IsCellWalkable(Cell))
		{
			continue;
		}
		const TArray<FIntPoint> Path = FindPath(StartPos, Cell, APBudget, ForbiddenCells);
		if (!Path.IsEmpty() && (BestPath.IsEmpty() || Path.Num() < BestPath.Num()))
		{
			BestPath = Path;
			if (BestPath.Num() <= APBudget)
			{
				break;
			}
		}
	}
	if (BestPath.IsEmpty())
	{
		for (const int32 Budget : { APBudget + 4, APBudget + 8, 20 })
		{
			for (const FIntPoint& Cell : Neighbours)
			{
				if (ForbiddenCells.Contains(Cell) || !IsCellWalkable(Cell))
				{
					continue;
				}
				const TArray<FIntPoint> Path = FindPath(StartPos, Cell, Budget, ForbiddenCells);
				if (!Path.IsEmpty() && (BestPath.IsEmpty() || Path.Num() < BestPath.Num()))
				{
					BestPath = Path;
				}
			}
			if (!BestPath.IsEmpty())
			{
				break;
			}
		}
	}
	return BestPath;
}

TArray<FIntPoint> UGorkyGridManager::FindPathClosestOutsideForbidden(const FIntPoint& StartPos, const FIntPoint& TargetPos, int32 APBudget,
	const TSet<FIntPoint>& ForbiddenCells) const
{
	FIntPoint BestCell = StartPos;
	int32 BestDistance = TNumericLimits<int32>::Max();
	for (const TPair<FIntPoint, int32>& Entry : GetReachableCells(StartPos, APBudget))
	{
		if (ForbiddenCells.Contains(Entry.Key))
		{
			continue;
		}
		const int32 Distance = FMath::Max(FMath::Abs(Entry.Key.X - TargetPos.X), FMath::Abs(Entry.Key.Y - TargetPos.Y));
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			BestCell = Entry.Key;
		}
	}
	return BestCell == StartPos ? TArray<FIntPoint>() : FindPath(StartPos, BestCell, APBudget, ForbiddenCells);
}
