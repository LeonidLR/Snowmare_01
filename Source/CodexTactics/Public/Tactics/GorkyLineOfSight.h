#pragma once

#include "CoreMinimal.h"

class UGorkyGridManager;

/**
 * Line of sight on the orthogonal Gorky 17 grid: Bresenham walk between the cells; any occupant except a mine on an
 * intermediate cell blocks it (the start and end cells never block).
 * Godot reference: Scripts/tactics/gorky17_los.gd (Gorky17LoS.has_line_of_sight).
 */
namespace GorkyLineOfSight
{
	CODEXTACTICS_API bool HasLineOfSight(const FIntPoint& Start, const FIntPoint& End, const UGorkyGridManager& Grid);
}
