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

	/**
	 * UE rule (user decision 2026-10-04): a barricade right next to the shooter or right next to the target (their own
	 * cover) does not block the line of fire — it only lowers the accuracy (bOutThroughCover); any other occupant on the
	 * way still blocks, a barricade in the open between them too.
	 */
	CODEXTACTICS_API bool HasLineOfFireThroughCover(const FIntPoint& Start, const FIntPoint& End, const UGorkyGridManager& Grid,
		bool& bOutThroughCover);
}
