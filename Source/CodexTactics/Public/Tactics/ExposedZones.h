#pragma once

#include "CoreMinimal.h"
#include "Data/CombatTypes.h"

/**
 * Gorky 17 "exposed zones": the 14 x 14 grid is split into four quadrants (NW, NE, SW, SE). A quadrant without a living
 * operative or a working turret for more than two squad turns (on the third) lets 1-2 standard (non-elite) enemies of the
 * wave in on its edge. At most one breach per turn-based fight, at most 6 living enemies on the field.
 * Godot reference: Scripts/tactics/tactical_exposed_zones_manager.gd (get_quadrant_for_pos, get_quadrant_rect,
 * evaluate_wave_roster, on_squad_turn_ended, is_quadrant_covered, find_spawn_cells_in_quadrant).
 */
class CODEXTACTICS_API FExposedZones
{
public:
	static constexpr int32 NumQuadrants = 4;
	/** Godot MAX_COMBAT_ENEMIES. */
	static constexpr int32 MaxCombatEnemies = 6;
	/** Uncovered turns after which the next uncovered turn breaches (Godot "> 2"). */
	static constexpr int32 TurnsBeforeBreach = 2;

	/** One enemy on the grid, for the roster evaluation. */
	struct FRosterEntry
	{
		EEnemyArchetype Type = EEnemyArchetype::FrostHound;
		float MaxHealth = 45.f;
		float Damage = 12.f;
		float MaxAP = 6.f;
	};

	/** Result of the end of a squad turn. */
	struct FTurnResult
	{
		/** Uncovered turns of each quadrant after this turn (0 = covered / reset after a breach). */
		int32 Turns[NumQuadrants] = { 0, 0, 0, 0 };
		/** Quadrant the reinforcements came through, or INDEX_NONE. */
		int32 BreachQuadrant = INDEX_NONE;
		int32 Spawned = 0;
	};

	/** Quadrant of a cell: 0 NW (x < mid, y < mid), 1 NE, 2 SW, 3 SE. */
	static int32 GetQuadrant(const FIntPoint& Cell, const FIntPoint& GridSize);

	/** Cells of a quadrant: Min inclusive, Max exclusive. */
	static FIntRect GetQuadrantRect(int32 Quadrant, const FIntPoint& GridSize);

	/** "North-West", "North-East", "South-West", "South-East". */
	static FString GetQuadrantName(int32 Quadrant);

	/** New fight: counters and the breach limit reset. */
	void Reset();

	/**
	 * Godot evaluate_wave_roster: power score = HP * damage * max(1, AP / 2) averaged per type; the strongest type is
	 * dropped when there are several (the wave's elite); the rest is the reinforcement pool, weakest first.
	 */
	void EvaluateRoster(const TArray<FRosterEntry>& Enemies);

	/** Non-elite types reinforcements are drawn from (hound when the roster was empty). */
	const TArray<EEnemyArchetype>& GetPool() const { return Pool; }

	/**
	 * Godot on_squad_turn_ended. Covered[Q] = a living operative or working turret stands in the quadrant.
	 * RollCount returns the wanted group size (Godot randi_range(1, 2)); TrySpawn(Quadrant, Count) spawns up to Count
	 * enemies on the quadrant's edge and returns how many appeared (0 = no free cell: no breach, the counter keeps going).
	 */
	FTurnResult EndSquadTurn(const bool Covered[NumQuadrants], int32 AliveEnemies, TFunctionRef<int32()> RollCount,
		TFunctionRef<int32(int32 Quadrant, int32 Count)> TrySpawn);

	int32 GetTurns(int32 Quadrant) const { return Turns[Quadrant]; }
	int32 GetReinforcementsSpawned() const { return ReinforcementsSpawned; }

	/** Godot max_reinforcements_per_combat. */
	int32 MaxReinforcementsPerCombat = 1;

private:
	int32 Turns[NumQuadrants] = { 0, 0, 0, 0 };
	int32 ReinforcementsSpawned = 0;
	TArray<EEnemyArchetype> Pool;
};
