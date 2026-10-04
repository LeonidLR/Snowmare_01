#pragma once

#include "CoreMinimal.h"

/**
 * Spawn lane matching (Godot main.gd _get_enemy_spawn_pos + Architect decision Q9 / Sprint 05-C, 2026-10-04): the level
 * JSON / Wave Editor names lanes NORTH_GATE / WEST_FLANK / EAST_FLANK / FAR_PERIMETER while the map's spawn points carry
 * Godot's Russian lane names («Северные ворота», «Левый фланг (Прорыв)», «Правый фланг», «Дальний периметр»); in Godot
 * they never matched. Both spellings now name the same lane.
 */
namespace SpawnLaneRules
{
	/** NORTH_GATE / WEST_FLANK / EAST_FLANK / FAR_PERIMETER for either spelling (any case), else the name upper-cased. */
	CODEXTACTICS_API FString CanonicalLane(const FString& Lane);

	/** Empty / "ANY" requests match every point; otherwise the canonical lanes are equal or one name contains the other. */
	CODEXTACTICS_API bool LanesMatch(const FString& PointLane, const FString& RequestedLane);
}
