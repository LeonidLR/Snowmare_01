#include "Tactics/GorkyLineOfSight.h"
#include "Tactics/Gorky17Types.h"
#include "Tactics/GorkyGridManager.h"

namespace
{
	bool GorkyLosBlocks(const UGorkyGridManager& Grid, const FIntPoint& Cell, const FIntPoint& Start, const FIntPoint& End,
		bool* OutThroughCover = nullptr)
	{
		if (Cell == Start || Cell == End)
		{
			return false;
		}
		const EGorkyOccupantType Type = Grid.GetOccupantType(Cell);
		if (OutThroughCover && Type == EGorkyOccupantType::Barricade)
		{
			const auto Chebyshev = [](const FIntPoint& A, const FIntPoint& B) { return FMath::Max(FMath::Abs(A.X - B.X), FMath::Abs(A.Y - B.Y)); };
			if (Chebyshev(Cell, Start) <= 1 || Chebyshev(Cell, End) <= 1)
			{
				*OutThroughCover = true; // the shooter's / the target's own cover: lower accuracy, no block
				return false;
			}
		}
		return Type != EGorkyOccupantType::None && Type != EGorkyOccupantType::Mine;
	}
}

static bool GorkyWalk(const FIntPoint& Start, const FIntPoint& End, const UGorkyGridManager& Grid, bool* OutThroughCover)
{
	if (!Grid.IsValidCell(Start) || !Grid.IsValidCell(End))
	{
		return false;
	}
	if (Start == End)
	{
		return true;
	}
	const int32 DX = FMath::Abs(End.X - Start.X);
	const int32 DY = FMath::Abs(End.Y - Start.Y);
	const int32 SX = Start.X < End.X ? 1 : -1;
	const int32 SY = Start.Y < End.Y ? 1 : -1;
	int32 X = Start.X;
	int32 Y = Start.Y;

	// Same stepping as Godot (float error term, major axis first).
	if (DX > DY)
	{
		float Error = DX / 2.f;
		while (X != End.X)
		{
			X += SX;
			Error -= DY;
			if (Error < 0.f)
			{
				Y += SY;
				Error += DX;
			}
			if (GorkyLosBlocks(Grid, FIntPoint(X, Y), Start, End, OutThroughCover))
			{
				return false;
			}
		}
	}
	else
	{
		float Error = DY / 2.f;
		while (Y != End.Y)
		{
			Y += SY;
			Error -= DX;
			if (Error < 0.f)
			{
				X += SX;
				Error += DY;
			}
			if (GorkyLosBlocks(Grid, FIntPoint(X, Y), Start, End, OutThroughCover))
			{
				return false;
			}
		}
	}
	return true;
}

bool GorkyLineOfSight::HasLineOfSight(const FIntPoint& Start, const FIntPoint& End, const UGorkyGridManager& Grid)
{
	return GorkyWalk(Start, End, Grid, nullptr);
}

bool GorkyLineOfSight::HasLineOfFireThroughCover(const FIntPoint& Start, const FIntPoint& End, const UGorkyGridManager& Grid,
	bool& bOutThroughCover)
{
	bOutThroughCover = false;
	return GorkyWalk(Start, End, Grid, &bOutThroughCover);
}
