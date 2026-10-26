#include "Tactics/TurnArenaRules.h"

namespace TurnArenaRules
{
	ETurnArenaCell ClassifyCell(const FTurnArenaSample& Sample, float GroundZ, const FTurnArenaConfig& Config)
	{
		if (!Sample.bGround)
		{
			return ETurnArenaCell::NoGround;
		}
		if (Sample.bLadder)
		{
			return ETurnArenaCell::Ladder;
		}
		if (Sample.bRoofed || Sample.bInFootprint || Sample.EnclosureHits >= Config.EnclosedMinHits)
		{
			return ETurnArenaCell::Building;
		}
		if (Sample.bBlocked)
		{
			return ETurnArenaCell::Obstacle;
		}
		if (FMath::Abs(Sample.GroundZ - GroundZ) > Config.LevelToleranceCm)
		{
			return ETurnArenaCell::NotLevel;
		}
		if (!Sample.bOnNav)
		{
			return ETurnArenaCell::OffNavmesh;
		}
		return ETurnArenaCell::Walkable;
	}

	bool IsSubtracted(ETurnArenaCell Class)
	{
		return Class == ETurnArenaCell::Building || Class == ETurnArenaCell::Doorway || Class == ETurnArenaCell::Ladder
			|| Class == ETurnArenaCell::NoGround;
	}

	bool IsInsideFootprint(const FTurnArenaFootprint& Footprint, const FVector2D& Point, float MarginCm)
	{
		const FVector2D Local = (Point - Footprint.Centre).GetRotated(-Footprint.YawDegrees);
		return FMath::Abs(Local.X) <= Footprint.HalfExtent.X + MarginCm && FMath::Abs(Local.Y) <= Footprint.HalfExtent.Y + MarginCm;
	}

	bool FootprintsOverlap(const FTurnArenaFootprint& A, const FTurnArenaFootprint& B)
	{
		auto Axes = [](const FTurnArenaFootprint& F)
		{
			const FVector2D X = FVector2D(1.f, 0.f).GetRotated(F.YawDegrees);
			return TPair<FVector2D, FVector2D>(X, FVector2D(-X.Y, X.X));
		};
		auto Radius = [&Axes](const FTurnArenaFootprint& F, const FVector2D& Axis)
		{
			const TPair<FVector2D, FVector2D> Own = Axes(F);
			return FMath::Abs(FVector2D::DotProduct(Own.Key, Axis)) * F.HalfExtent.X + FMath::Abs(FVector2D::DotProduct(Own.Value, Axis)) * F.HalfExtent.Y;
		};
		const TPair<FVector2D, FVector2D> AxesA = Axes(A);
		const TPair<FVector2D, FVector2D> AxesB = Axes(B);
		const FVector2D Delta = B.Centre - A.Centre;
		for (const FVector2D& Axis : { AxesA.Key, AxesA.Value, AxesB.Key, AxesB.Value })
		{
			if (FMath::Abs(FVector2D::DotProduct(Delta, Axis)) > Radius(A, Axis) + Radius(B, Axis))
			{
				return false;
			}
		}
		return true;
	}

	TArray<FIntPoint> CellsNearPoint(const FVector2D& Point, const FVector2D& LatticeBase, float CellSize, float RadiusCm)
	{
		TArray<FIntPoint> Result;
		const FVector2D Local = (Point - LatticeBase) / CellSize;
		const float Reach = RadiusCm / CellSize;
		for (int32 X = FMath::FloorToInt(Local.X - Reach); X <= FMath::FloorToInt(Local.X + Reach); ++X)
		{
			for (int32 Y = FMath::FloorToInt(Local.Y - Reach); Y <= FMath::FloorToInt(Local.Y + Reach); ++Y)
			{
				// Distance from the point to the cell's square.
				const double DX = FMath::Max3(static_cast<double>(X) - Local.X, 0.0, Local.X - static_cast<double>(X + 1));
				const double DY = FMath::Max3(static_cast<double>(Y) - Local.Y, 0.0, Local.Y - static_cast<double>(Y + 1));
				if (DX * DX + DY * DY <= Reach * Reach)
				{
					Result.Add(FIntPoint(X, Y));
				}
			}
		}
		return Result;
	}

	TArray<int32> FindRegions(const TArray<bool>& Open, int32 Cells)
	{
		TArray<int32> Region;
		Region.Init(-1, Cells * Cells);
		auto IsOpen = [&Open, Cells](int32 X, int32 Y)
		{
			return X >= 0 && Y >= 0 && X < Cells && Y < Cells && Open.IsValidIndex(X * Cells + Y) && Open[X * Cells + Y];
		};
		int32 Next = 0;
		TArray<FIntPoint> Stack;
		for (int32 X = 0; X < Cells; ++X)
		{
			for (int32 Y = 0; Y < Cells; ++Y)
			{
				if (!IsOpen(X, Y) || Region[X * Cells + Y] >= 0)
				{
					continue;
				}
				Region[X * Cells + Y] = Next;
				Stack.Reset();
				Stack.Add(FIntPoint(X, Y));
				while (!Stack.IsEmpty())
				{
					const FIntPoint Cell = Stack.Pop(EAllowShrinking::No);
					for (int32 DX = -1; DX <= 1; ++DX)
					{
						for (int32 DY = -1; DY <= 1; ++DY)
						{
							const FIntPoint N(Cell.X + DX, Cell.Y + DY);
							if ((DX == 0 && DY == 0) || !IsOpen(N.X, N.Y) || Region[N.X * Cells + N.Y] >= 0)
							{
								continue;
							}
							// A diagonal step needs one of the two orthogonal cells it passes open (UGorkyGridManager::GetNeighbors).
							if (DX != 0 && DY != 0 && !IsOpen(Cell.X + DX, Cell.Y) && !IsOpen(Cell.X, Cell.Y + DY))
							{
								continue;
							}
							Region[N.X * Cells + N.Y] = Next;
							Stack.Add(N);
						}
					}
				}
				++Next;
			}
		}
		return Region;
	}

	bool FindOpenRect(const TArray<bool>& Open, int32 Cells, int32 Long, int32 Short, FIntPoint& OutOrigin, FIntPoint& OutSize)
	{
		if (Cells <= 0 || Open.Num() != Cells * Cells || Long <= 0 || Short <= 0)
		{
			return false;
		}
		// Summed-area table: Sum(X, Y) = open cells in [0, X) x [0, Y).
		const int32 Stride = Cells + 1;
		TArray<int32> Sum;
		Sum.Init(0, Stride * Stride);
		for (int32 X = 0; X < Cells; ++X)
		{
			for (int32 Y = 0; Y < Cells; ++Y)
			{
				Sum[(X + 1) * Stride + Y + 1] = (Open[X * Cells + Y] ? 1 : 0) + Sum[X * Stride + Y + 1] + Sum[(X + 1) * Stride + Y] - Sum[X * Stride + Y];
			}
		}
		const FIntPoint Sizes[] = { FIntPoint(Long, Short), FIntPoint(Short, Long) };
		for (const FIntPoint& Size : Sizes)
		{
			for (int32 X = 0; X + Size.X <= Cells; ++X)
			{
				for (int32 Y = 0; Y + Size.Y <= Cells; ++Y)
				{
					const int32 Count = Sum[(X + Size.X) * Stride + Y + Size.Y] - Sum[X * Stride + Y + Size.Y] - Sum[(X + Size.X) * Stride + Y] + Sum[X * Stride + Y];
					if (Count == Size.X * Size.Y)
					{
						OutOrigin = FIntPoint(X, Y);
						OutSize = Size;
						return true;
					}
				}
			}
		}
		return false;
	}

	FIntPoint SnapToArena(const FIntPoint& From, int32 Cells, int32 MaxSteps, TFunctionRef<bool(const FIntPoint&)> IsTarget,
		TFunctionRef<bool(const FIntPoint&)> IsPassable)
	{
		auto Inside = [Cells](const FIntPoint& C) { return C.X >= 0 && C.Y >= 0 && C.X < Cells && C.Y < Cells; };
		if (Inside(From) && IsTarget(From))
		{
			return From;
		}
		// 1. Walking out: breadth-first through passable cells (a building's floor, its door), 8 directions, nearest step count first.
		if (Inside(From))
		{
			TMap<FIntPoint, int32> Steps;
			Steps.Add(From, 0);
			TArray<FIntPoint> Frontier = { From };
			for (int32 Step = 1; Step <= MaxSteps && !Frontier.IsEmpty(); ++Step)
			{
				TArray<FIntPoint> NextFrontier;
				FIntPoint Best(-1, -1);
				float BestDistance = TNumericLimits<float>::Max();
				for (const FIntPoint& Cell : Frontier)
				{
					for (int32 DX = -1; DX <= 1; ++DX)
					{
						for (int32 DY = -1; DY <= 1; ++DY)
						{
							const FIntPoint N(Cell.X + DX, Cell.Y + DY);
							if ((DX == 0 && DY == 0) || !Inside(N) || Steps.Contains(N))
							{
								continue;
							}
							if (IsTarget(N))
							{
								const float Distance = static_cast<float>((N - From).SizeSquared());
								if (Distance < BestDistance)
								{
									BestDistance = Distance;
									Best = N;
								}
								continue;
							}
							if (IsPassable(N))
							{
								Steps.Add(N, Step);
								NextFrontier.Add(N);
							}
						}
					}
				}
				if (Best.X >= 0)
				{
					return Best;
				}
				Frontier = MoveTemp(NextFrontier);
			}
		}
		// 2. The nearest target in a straight line (walled in, or From outside the grid).
		FIntPoint Best(-1, -1);
		int32 BestDistance = TNumericLimits<int32>::Max();
		for (int32 X = 0; X < Cells; ++X)
		{
			for (int32 Y = 0; Y < Cells; ++Y)
			{
				const FIntPoint Cell(X, Y);
				const int32 Distance = (Cell - From).SizeSquared();
				if (Distance < BestDistance && Distance <= MaxSteps * MaxSteps && IsTarget(Cell))
				{
					BestDistance = Distance;
					Best = Cell;
				}
			}
		}
		return Best;
	}

	FTurnArenaVerdict Evaluate(TFunctionRef<FTurnArenaSample(const FIntPoint&)> SampleAt, const TArray<FTurnArenaUnit>& Units,
		const FTurnArenaConfig& Config)
	{
		FTurnArenaVerdict Verdict;
		const int32 N = FMath::Max(1, Config.Cells);
		Verdict.Cells = N;
		TArray<FTurnArenaSample> Samples;
		Samples.Reserve(N * N);
		TArray<float> Heights;
		for (int32 X = 0; X < N; ++X)
		{
			for (int32 Y = 0; Y < N; ++Y)
			{
				const FTurnArenaSample& Sample = Samples.Add_GetRef(SampleAt(FIntPoint(X, Y)));
				if (Sample.bGround && !Sample.bBlocked && !Sample.bRoofed && !Sample.bInFootprint && !Sample.bLadder)
				{
					Heights.Add(Sample.GroundZ);
				}
			}
		}
		if (Heights.IsEmpty())
		{
			Verdict.Fault = ETurnArenaFault::NoGround;
			Verdict.CellClass.Init(ETurnArenaCell::NoGround, N * N);
			Verdict.Arena.Init(false, N * N);
			return Verdict;
		}
		Heights.Sort();
		Verdict.GroundZ = Heights[Heights.Num() / 2];
		Verdict.bHasGround = true;

		// 1. Classes; a squeezed cell next to a building is its door / opening.
		Verdict.CellClass.SetNum(N * N);
		for (int32 Index = 0; Index < N * N; ++Index)
		{
			Verdict.CellClass[Index] = ClassifyCell(Samples[Index], Verdict.GroundZ, Config);
		}
		for (int32 X = 0; X < N; ++X)
		{
			for (int32 Y = 0; Y < N; ++Y)
			{
				const int32 Index = X * N + Y;
				const ETurnArenaCell Class = Verdict.CellClass[Index];
				if (!Samples[Index].bSqueezed || Class == ETurnArenaCell::Building || Class == ETurnArenaCell::Ladder || Class == ETurnArenaCell::NoGround)
				{
					continue;
				}
				bool bNextToBuilding = false;
				for (int32 DX = -1; DX <= 1 && !bNextToBuilding; ++DX)
				{
					for (int32 DY = -1; DY <= 1 && !bNextToBuilding; ++DY)
					{
						bNextToBuilding = Verdict.ClassOf(FIntPoint(X + DX, Y + DY)) == ETurnArenaCell::Building && Verdict.IsInside(FIntPoint(X + DX, Y + DY));
					}
				}
				if (bNextToBuilding)
				{
					Verdict.CellClass[Index] = ETurnArenaCell::Doorway;
				}
			}
		}

		// 2. Open areas holding the minimum rectangle.
		TArray<bool> Open;
		Open.SetNum(N * N);
		for (int32 Index = 0; Index < N * N; ++Index)
		{
			Open[Index] = Verdict.CellClass[Index] == ETurnArenaCell::Walkable;
		}
		const TArray<int32> Region = FindRegions(Open, N);
		int32 RegionCount = 0;
		for (const int32 Id : Region)
		{
			RegionCount = FMath::Max(RegionCount, Id + 1);
		}
		const FIntPoint Leader = Units.IsEmpty() ? FIntPoint(N / 2, N / 2) : Units[0].Cell;
		auto Passable = [&Verdict](const FIntPoint& C)
		{
			const ETurnArenaCell Class = Verdict.ClassOf(C);
			return Class == ETurnArenaCell::Building || Class == ETurnArenaCell::Doorway || Class == ETurnArenaCell::Walkable;
		};
		int32 BestRegion = -1;
		int32 BestMissing = TNumericLimits<int32>::Max();
		int32 BestDistance = TNumericLimits<int32>::Max();
		FIntPoint BestOrigin(-1, -1);
		FIntPoint BestSize = FIntPoint::ZeroValue;
		TArray<FIntPoint> BestUnitCells;
		for (int32 Id = 0; Id < RegionCount; ++Id)
		{
			TArray<bool> Mask;
			Mask.SetNum(N * N);
			for (int32 Index = 0; Index < N * N; ++Index)
			{
				Mask[Index] = Region[Index] == Id;
			}
			FIntPoint Origin, Size;
			if (!FindOpenRect(Mask, N, Config.MinLongCells, Config.MinShortCells, Origin, Size))
			{
				continue;
			}
			// The squad onto this area: who stands off it, how far everyone has to go.
			int32 Missing = 0;
			int32 Distance = 0;
			TArray<FIntPoint> UnitCells;
			for (const FTurnArenaUnit& Unit : Units)
			{
				const FIntPoint Snap = SnapToArena(Unit.Cell, N, Config.MaxSnapCells,
					[&Mask, N](const FIntPoint& C) { return Mask[C.X * N + C.Y]; }, Passable);
				UnitCells.Add(Snap);
				Missing += Snap.X < 0 ? 1 : 0;
				Distance += Snap.X < 0 ? 0 : (Snap - Unit.Cell).SizeSquared();
			}
			if (Missing < BestMissing || (Missing == BestMissing && Distance < BestDistance))
			{
				BestRegion = Id;
				BestMissing = Missing;
				BestDistance = Distance;
				BestOrigin = Origin;
				BestSize = Size;
				BestUnitCells = MoveTemp(UnitCells);
			}
		}
		Verdict.Arena.Init(false, N * N);
		if (BestRegion < 0)
		{
			// Inside a building the open area is small because of it: say so ("lead the squad out"), not "no room".
			const ETurnArenaCell AtLeader = Verdict.ClassOf(Leader);
			Verdict.Fault = Verdict.IsInside(Leader) && (AtLeader == ETurnArenaCell::Building || AtLeader == ETurnArenaCell::Doorway)
				? ETurnArenaFault::InsideBuilding : ETurnArenaFault::TooSmall;
			Verdict.Cell = Leader;
			return Verdict;
		}
		for (int32 Index = 0; Index < N * N; ++Index)
		{
			Verdict.Arena[Index] = Region[Index] == BestRegion;
			Verdict.ArenaCells += Verdict.Arena[Index] ? 1 : 0;
		}
		Verdict.RectOrigin = BestOrigin;
		Verdict.RectSize = BestSize;
		Verdict.UnitCells = BestUnitCells;

		// 3. The squad: level feet (2026-09-30), everyone on the arena or close enough to be moved onto it.
		for (const FTurnArenaUnit& Unit : Units)
		{
			if (FMath::Abs(Unit.FeetZ - Verdict.GroundZ) > Config.SquadFeetToleranceCm)
			{
				Verdict.Fault = ETurnArenaFault::NotFlat;
				Verdict.Cell = Unit.Cell;
				return Verdict;
			}
		}
		for (int32 Index = 0; Index < Units.Num(); ++Index)
		{
			if (Verdict.UnitCells[Index].X < 0)
			{
				const ETurnArenaCell Class = Verdict.ClassOf(Units[Index].Cell);
				Verdict.Fault = Verdict.IsInside(Units[Index].Cell) && (Class == ETurnArenaCell::Building || Class == ETurnArenaCell::Doorway)
					? ETurnArenaFault::InsideBuilding : ETurnArenaFault::SquadOutside;
				Verdict.Cell = Units[Index].Cell;
				return Verdict;
			}
		}
		return Verdict;
	}

	TArray<FIntPoint> CandidateOffsets(const FTurnArenaConfig& Config)
	{
		TArray<FIntPoint> Offsets;
		const int32 Radius = FMath::Max(0, Config.SearchRadiusCells);
		for (int32 X = -Radius; X <= Radius; ++X)
		{
			for (int32 Y = -Radius; Y <= Radius; ++Y)
			{
				Offsets.Add(FIntPoint(X, Y));
			}
		}
		Offsets.StableSort([](const FIntPoint& A, const FIntPoint& B) { return A.SizeSquared() < B.SizeSquared(); });
		return Offsets;
	}

	float CandidateScore(const FIntPoint& Offset, int32 EnemiesInside, const FTurnArenaConfig& Config)
	{
		return FMath::Sqrt(static_cast<float>(Offset.SizeSquared())) * Config.CellSize - EnemiesInside * Config.EnemyBonusCm;
	}

	const TCHAR* FaultName(ETurnArenaFault Fault)
	{
		switch (Fault)
		{
		case ETurnArenaFault::None: return TEXT("ok");
		case ETurnArenaFault::NoGround: return TEXT("no ground");
		case ETurnArenaFault::TooSmall: return TEXT("no open area of the minimum size");
		case ETurnArenaFault::NotFlat: return TEXT("an operative off the level ground");
		case ETurnArenaFault::InsideBuilding: return TEXT("an operative deep inside a building");
		case ETurnArenaFault::SquadOutside: return TEXT("an operative cut off / outside the grid");
		default: return TEXT("?");
		}
	}

	TCHAR CellChar(ETurnArenaCell Class)
	{
		switch (Class)
		{
		case ETurnArenaCell::Walkable: return TEXT('.');
		case ETurnArenaCell::Building: return TEXT('B');
		case ETurnArenaCell::Doorway: return TEXT('D');
		case ETurnArenaCell::Ladder: return TEXT('L');
		case ETurnArenaCell::Obstacle: return TEXT('#');
		case ETurnArenaCell::NotLevel: return TEXT('~');
		case ETurnArenaCell::OffNavmesh: return TEXT('N');
		default: return TEXT('_');
		}
	}
}
