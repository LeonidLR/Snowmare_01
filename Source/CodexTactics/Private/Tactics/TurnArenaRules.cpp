#include "Tactics/TurnArenaRules.h"

namespace TurnArenaRules
{
	bool IsCoreCell(const FIntPoint& Cell, const FTurnArenaConfig& Config)
	{
		const int32 Border = FMath::Clamp(Config.BorderCells, 0, Config.Cells / 2);
		return Cell.X >= Border && Cell.Y >= Border && Cell.X < Config.Cells - Border && Cell.Y < Config.Cells - Border;
	}

	ETurnArenaFault CellFault(const FTurnArenaSample& Sample, float GroundZ, const FTurnArenaConfig& Config)
	{
		if (!Sample.bGround)
		{
			return ETurnArenaFault::NoGround;
		}
		if (Sample.bRoofed)
		{
			return ETurnArenaFault::Roof;
		}
		if (Sample.bBlocked)
		{
			return ETurnArenaFault::Obstacle;
		}
		if (FMath::Abs(Sample.GroundZ - GroundZ) > Config.FlatToleranceCm)
		{
			return ETurnArenaFault::NotFlat;
		}
		if (!Sample.bOnNav)
		{
			return ETurnArenaFault::OffNavmesh;
		}
		return ETurnArenaFault::None;
	}

	FTurnArenaVerdict Evaluate(TFunctionRef<FTurnArenaSample(const FIntPoint&)> SampleAt, const TArray<FIntPoint>& UnitCells,
		const FTurnArenaConfig& Config)
	{
		FTurnArenaVerdict Verdict;
		TArray<float> Heights;
		for (int32 X = 0; X < Config.Cells; ++X)
		{
			for (int32 Y = 0; Y < Config.Cells; ++Y)
			{
				const FIntPoint Cell(X, Y);
				const FTurnArenaSample Sample = SampleAt(Cell);
				if (IsCoreCell(Cell, Config) && Sample.bGround && !Sample.bRoofed)
				{
					Heights.Add(Sample.GroundZ);
				}
			}
		}
		if (Heights.IsEmpty())
		{
			Verdict.Fault = ETurnArenaFault::NoGround;
			return Verdict;
		}
		Heights.Sort();
		Verdict.GroundZ = Heights[Heights.Num() / 2];
		for (int32 X = 0; X < Config.Cells; ++X)
		{
			for (int32 Y = 0; Y < Config.Cells; ++Y)
			{
				const FIntPoint Cell(X, Y);
				const ETurnArenaFault Fault = CellFault(SampleAt(Cell), Verdict.GroundZ, Config);
				if (Fault == ETurnArenaFault::None)
				{
					continue;
				}
				if (IsCoreCell(Cell, Config))
				{
					Verdict.Fault = Fault;
					Verdict.Cell = Cell;
					return Verdict;
				}
				Verdict.BlockedBorder.Add(Cell);
			}
		}
		for (const FIntPoint& Unit : UnitCells)
		{
			if (Unit.X < 1 || Unit.Y < 1 || Unit.X > Config.Cells - 2 || Unit.Y > Config.Cells - 2)
			{
				Verdict.Fault = ETurnArenaFault::SquadOutside;
				Verdict.Cell = Unit;
				return Verdict;
			}
			bool bNearUsable = false;
			for (int32 DX = -1; DX <= 1 && !bNearUsable; ++DX)
			{
				for (int32 DY = -1; DY <= 1 && !bNearUsable; ++DY)
				{
					const FIntPoint Near = Unit + FIntPoint(DX, DY);
					bNearUsable = Near.X >= 0 && Near.Y >= 0 && Near.X < Config.Cells && Near.Y < Config.Cells && !Verdict.BlockedBorder.Contains(Near);
				}
			}
			if (!bNearUsable)
			{
				Verdict.Fault = ETurnArenaFault::SquadOnBadCell;
				Verdict.Cell = Unit;
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
		case ETurnArenaFault::NotFlat: return TEXT("not flat");
		case ETurnArenaFault::Obstacle: return TEXT("obstacle");
		case ETurnArenaFault::Roof: return TEXT("roof overhead");
		case ETurnArenaFault::OffNavmesh: return TEXT("off the navmesh");
		case ETurnArenaFault::SquadOutside: return TEXT("an operative outside the grid");
		case ETurnArenaFault::SquadOnBadCell: return TEXT("an operative on an unusable cell");
		default: return TEXT("?");
		}
	}
}
