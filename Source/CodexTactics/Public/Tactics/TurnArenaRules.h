#pragma once

#include "CoreMinimal.h"

/**
 * Where a turn-based fight may be laid out (UE-only; the game designer's rule, repeated 2026-10-22 after the Demo 01 lake
 * play-test put the grid INSIDE the base building: "a turn-based fight may start ONLY on absolutely flat open ground, with no
 * obstacles in the middle except barrels, barricades and turrets"; the earlier user decision 2026-09-30 - "turn-based entry on
 * flat ground only" - checked only the squad's feet against the median ground height).
 *
 * The grid (Cells x Cells of CellSize) is sampled cell by cell (UTurnBasedCombatSubsystem::FindTurnArena): ground height, a solid
 * obstacle in the cell (anything blocking pawns except the squad, the enemies and the allowed props), a roof / ceiling overhead,
 * the navmesh. The CORE (every cell but the BorderCells ring) must be usable: ground within FlatToleranceCm of the median, no
 * obstacle, no roof, on the navmesh. Border cells that fail become grid obstacles (a wall at the edge is fine). Every living
 * operative must stand inside the grid (not on its outermost cell ring) on a usable cell or right next to one (he is placed on
 * the nearest free cell). Candidate grid centres are searched round the
 * squad (SearchRadiusCells); the one nearest the squad wins, more engaged enemies inside it break ties. No candidate -> no
 * turn-based fight (a feed line says why).
 */
struct FTurnArenaConfig
{
	int32 Cells = 14;
	float CellSize = 150.f;
	/**
	 * Outer ring of the grid that may hold walls / buildings / slopes (they turn into obstacle cells - never walkable), cells. The
	 * core ("the middle", 8 x 8 cells = 12 m) must be open, level ground.
	 */
	int32 BorderCells = 3;
	/** "Absolutely flat": a core cell's ground within this of the median, cm. */
	float FlatToleranceCm = 30.f;
	/** Grid centres tried up to this many cells from the leader each way. */
	int32 SearchRadiusCells = 6;
	/** An engaged enemy inside the grid is worth this much distance of the centre from the squad, cm. */
	float EnemyBonusCm = 150.f;
};

/** One cell as sampled in the world. */
struct FTurnArenaSample
{
	bool bGround = false;
	float GroundZ = 0.f;
	/** A solid obstacle (wall, building, vehicle, fence...) in the cell above the ground. */
	bool bBlocked = false;
	/** A roof / ceiling / overhang right above the cell (inside a building). */
	bool bRoofed = false;
	bool bOnNav = false;
};

enum class ETurnArenaFault : uint8
{
	None,
	NoGround,
	NotFlat,
	Obstacle,
	Roof,
	OffNavmesh,
	/** An operative outside the grid or on its outer ring. */
	SquadOutside,
	/** An operative on an unusable cell with no usable cell next to it. */
	SquadOnBadCell
};

struct FTurnArenaVerdict
{
	ETurnArenaFault Fault = ETurnArenaFault::None;
	/** The first offending cell (grid coordinates). */
	FIntPoint Cell = FIntPoint(-1, -1);
	/** Median ground height of the core. */
	float GroundZ = 0.f;
	/** Border cells that become obstacles. */
	TArray<FIntPoint> BlockedBorder;
	bool IsValid() const { return Fault == ETurnArenaFault::None; }
};

namespace TurnArenaRules
{
	/** Not on the outer BorderCells ring. */
	CODEXTACTICS_API bool IsCoreCell(const FIntPoint& Cell, const FTurnArenaConfig& Config);

	/** The cell can carry a unit: ground within the flat tolerance of GroundZ, no obstacle, no roof, on the navmesh. */
	CODEXTACTICS_API ETurnArenaFault CellFault(const FTurnArenaSample& Sample, float GroundZ, const FTurnArenaConfig& Config);

	/**
	 * The arena verdict. SampleAt(Cell) gives the sample of a grid cell (0..Cells-1); UnitCells are the living operatives' cells
	 * in grid coordinates (any value; outside / on the outer ring = SquadOutside).
	 */
	CODEXTACTICS_API FTurnArenaVerdict Evaluate(TFunctionRef<FTurnArenaSample(const FIntPoint&)> SampleAt, const TArray<FIntPoint>& UnitCells,
		const FTurnArenaConfig& Config);

	/** Candidate centre offsets (cells) within SearchRadiusCells, nearest first (0, 0 first). */
	CODEXTACTICS_API TArray<FIntPoint> CandidateOffsets(const FTurnArenaConfig& Config);

	/** Lower is better: the centre's distance from the squad minus EnemyBonusCm per engaged enemy inside. */
	CODEXTACTICS_API float CandidateScore(const FIntPoint& Offset, int32 EnemiesInside, const FTurnArenaConfig& Config);

	/** Log / feed name of a fault. */
	CODEXTACTICS_API const TCHAR* FaultName(ETurnArenaFault Fault);
}
