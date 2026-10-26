#pragma once

#include "CoreMinimal.h"

/**
 * Where a turn-based fight may be laid out and which cells of its grid are open (UE-only; no Godot reference - Godot built its
 * grid round the leader wherever he stood: Scenes/movements/main.gd _enter_turn_based_combat).
 *
 * History: 2026-09-30 (user) only the squad's feet had to be level; 2026-10-22 (game designer, commit 40782ae) the whole middle of
 * the grid had to be open, level ground - too harsh: a snowy yard with barrels and small closed buildings was refused. Game designer
 * decision 2026-10-26: "as before" - the grid is laid round the leader again - but a building's footprint is SUBTRACTED from it
 * (nobody can enter a building during the fight, doors / openings included), ladders with their foot / top cells too, and the fight
 * may start when a connected open area of at least 8 x 8 or 8 x 4 cells (either orientation) is left round the squad.
 *
 * The grid (Cells x Cells of CellSize) is sampled cell by cell (UTurnBasedCombatSubsystem::FindTurnArena): ground height, a solid
 * obstacle at body height, a building roof overhead, inside a building footprint (a roof's / an indoor camera zone's outline + a
 * margin), walled in on most sides (a roofless shed), squeezed between two jambs (a door), a ladder, the navmesh. Every cell gets a
 * class (ETurnArenaCell); only Walkable cells are open, the rest become grid obstacles. The arena is the connected open area holding
 * the minimum rectangle (and the squad, if several qualify); operatives off it are moved onto it (out through the door when they
 * stand inside a building) up to MaxSnapCells; farther -> no fight (a feed line says why).
 */
struct FTurnArenaConfig
{
	int32 Cells = 14;
	float CellSize = 150.f;
	/** The open area must hold an all-open rectangle of MinLongCells x MinShortCells, either orientation (8 x 8 holds one too). */
	int32 MinLongCells = 8;
	int32 MinShortCells = 4;
	/** A cell whose ground is farther than this from the grid's median ground is no open cell (a platform, a pit, a roof), cm. */
	float LevelToleranceCm = 100.f;
	/** User decision 2026-09-30 (kept): every operative's feet within this of the median ground, else no fight ("level ground"), cm. */
	float SquadFeetToleranceCm = 50.f;
	/** An operative off the arena (inside a building, cut off, just outside the grid) is moved onto it up to this far, cells. */
	int32 MaxSnapCells = 3;
	/** Of a cell's 8 horizontal wall probes, this many hitting within the enclosure range = a walled interior (roofless shed). */
	int32 EnclosedMinHits = 7;
	/** When the grid round the leader fails: grid centres tried up to this many cells from him each way, nearest first. */
	int32 SearchRadiusCells = 4;
	/** An engaged enemy inside a shifted grid is worth this much distance of its centre from the leader, cm. */
	float EnemyBonusCm = 150.f;
};

/** One cell as sampled in the world. */
struct FTurnArenaSample
{
	bool bGround = false;
	float GroundZ = 0.f;
	/** Something solid at body height in the cell (wall, vehicle, fence, crate, closed shed) - barrels / barricades / turrets excluded. */
	bool bBlocked = false;
	/** A building roof / ceiling overhead (indoors). */
	bool bRoofed = false;
	/** The cell centre lies inside a building's footprint (its roof or indoor camera zone outline + margin). */
	bool bInFootprint = false;
	/** Of the 8 horizontal wall probes, how many hit a wall within the enclosure range. */
	int32 EnclosureHits = 0;
	/** Walls close on two opposite sides (a door / an opening in a wall line). */
	bool bSqueezed = false;
	/** A ladder, its foot or its top. */
	bool bLadder = false;
	bool bOnNav = true;
};

/** What a grid cell is for the fight. Only Walkable cells are open; the rest become grid obstacles. */
enum class ETurnArenaCell : uint8
{
	Walkable,
	NoGround,
	/** Inside a building footprint / under its roof / a walled interior. */
	Building,
	/** An opening (door) of a building's wall. */
	Doorway,
	Ladder,
	Obstacle,
	NotLevel,
	OffNavmesh
};

enum class ETurnArenaFault : uint8
{
	None,
	NoGround,
	/** No connected open area holds the minimum rectangle. */
	TooSmall,
	/** An operative's feet off the median ground (on high ground / in a pit). */
	NotFlat,
	/** An operative deep inside a building (no open cell within MaxSnapCells). */
	InsideBuilding,
	/** An operative cut off from the arena / too far outside the grid. */
	SquadOutside
};

/** A building's outline on the ground: an oriented rectangle. */
struct FTurnArenaFootprint
{
	FVector2D Centre = FVector2D::ZeroVector;
	FVector2D HalfExtent = FVector2D::ZeroVector;
	float YawDegrees = 0.f;
};

/** A living operative taking part (grid coordinates, may lie outside the grid) and his feet height. */
struct FTurnArenaUnit
{
	FIntPoint Cell = FIntPoint::ZeroValue;
	float FeetZ = 0.f;
};

struct FTurnArenaVerdict
{
	ETurnArenaFault Fault = ETurnArenaFault::None;
	/** The offending cell (grid coordinates). */
	FIntPoint Cell = FIntPoint(-1, -1);
	/** Median ground height of the grid. */
	float GroundZ = 0.f;
	bool bHasGround = false;
	int32 Cells = 0;
	/** Class of every cell, index X * Cells + Y. */
	TArray<ETurnArenaCell> CellClass;
	/** The arena: the connected open area the fight uses (same index). */
	TArray<bool> Arena;
	int32 ArenaCells = 0;
	/** The minimum rectangle found in the arena (grid cells), size (0, 0) = none. */
	FIntPoint RectOrigin = FIntPoint(-1, -1);
	FIntPoint RectSize = FIntPoint::ZeroValue;
	/** Per input unit: its cell on the arena (moved onto it when needed); (-1, -1) = none within reach. */
	TArray<FIntPoint> UnitCells;

	bool IsValid() const { return Fault == ETurnArenaFault::None; }
	bool IsInside(const FIntPoint& C) const { return C.X >= 0 && C.Y >= 0 && C.X < Cells && C.Y < Cells; }
	ETurnArenaCell ClassOf(const FIntPoint& C) const
	{
		return IsInside(C) && CellClass.Num() == Cells * Cells ? CellClass[C.X * Cells + C.Y] : ETurnArenaCell::NoGround;
	}
	bool IsArena(const FIntPoint& C) const { return IsInside(C) && Arena.Num() == Cells * Cells && Arena[C.X * Cells + C.Y]; }
};

namespace TurnArenaRules
{
	/** The class of one sampled cell against the grid's median ground (doorways are found by Evaluate: they need neighbours). */
	CODEXTACTICS_API ETurnArenaCell ClassifyCell(const FTurnArenaSample& Sample, float GroundZ, const FTurnArenaConfig& Config);

	/** Subtracted from the grid (buildings, doorways, ladders, no ground): never drawn, never entered. */
	CODEXTACTICS_API bool IsSubtracted(ETurnArenaCell Class);

	/** Point lies inside Footprint grown by MarginCm on every side. */
	CODEXTACTICS_API bool IsInsideFootprint(const FTurnArenaFootprint& Footprint, const FVector2D& Point, float MarginCm);

	/** Footprints overlap (separating axis test on the two rectangles). */
	CODEXTACTICS_API bool FootprintsOverlap(const FTurnArenaFootprint& A, const FTurnArenaFootprint& B);

	/**
	 * Cells of a lattice (cell L covers Base + L * CellSize .. + CellSize) whose square comes within RadiusCm of Point (a ladder's
	 * foot / top / rungs).
	 */
	CODEXTACTICS_API TArray<FIntPoint> CellsNearPoint(const FVector2D& Point, const FVector2D& LatticeBase, float CellSize, float RadiusCm);

	/**
	 * Connected open areas: region id per cell (index X * Cells + Y), -1 for a closed cell. Moves as the grid's (UGorkyGridManager
	 * GetNeighbors): orthogonal, diagonal when one of the two orthogonal cells it passes is open.
	 */
	CODEXTACTICS_API TArray<int32> FindRegions(const TArray<bool>& Open, int32 Cells);

	/** An all-open rectangle of Long x Short or Short x Long cells inside Open (the first found, scanning X then Y). */
	CODEXTACTICS_API bool FindOpenRect(const TArray<bool>& Open, int32 Cells, int32 Long, int32 Short, FIntPoint& OutOrigin, FIntPoint& OutSize);

	/**
	 * Where a unit at From goes onto the arena: From itself when IsTarget; else the nearest target reached walking through Passable
	 * cells (out of a building through its door) within MaxSteps; else the nearest target in a straight line within MaxSteps cells.
	 * (-1, -1) = none. From may lie outside the grid (then only the straight line).
	 */
	CODEXTACTICS_API FIntPoint SnapToArena(const FIntPoint& From, int32 Cells, int32 MaxSteps, TFunctionRef<bool(const FIntPoint&)> IsTarget,
		TFunctionRef<bool(const FIntPoint&)> IsPassable);

	/**
	 * The arena verdict. SampleAt(Cell) gives the sample of a grid cell (0..Cells-1); Units are the living operatives (grid
	 * coordinates, any value).
	 */
	CODEXTACTICS_API FTurnArenaVerdict Evaluate(TFunctionRef<FTurnArenaSample(const FIntPoint&)> SampleAt, const TArray<FTurnArenaUnit>& Units,
		const FTurnArenaConfig& Config);

	/** Candidate centre offsets (cells) within SearchRadiusCells, nearest first ((0, 0) first). */
	CODEXTACTICS_API TArray<FIntPoint> CandidateOffsets(const FTurnArenaConfig& Config);

	/** Lower is better: the centre's distance from the leader minus EnemyBonusCm per engaged enemy inside. */
	CODEXTACTICS_API float CandidateScore(const FIntPoint& Offset, int32 EnemiesInside, const FTurnArenaConfig& Config);

	/** Log / feed name of a fault. */
	CODEXTACTICS_API const TCHAR* FaultName(ETurnArenaFault Fault);

	/** Debug map character of a cell class (. open, B building, D doorway, L ladder, # obstacle, ~ not level, N off the navmesh, _ no ground). */
	CODEXTACTICS_API TCHAR CellChar(ETurnArenaCell Class);
}
