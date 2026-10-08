#pragma once

#include "CoreMinimal.h"

/**
 * Spawn lane matching (Godot main.gd _get_enemy_spawn_pos + Architect decision Q9 / Sprint 05-C, 2026-10-04): the level
 * JSON / Wave Editor names lanes NORTH_GATE / WEST_FLANK / EAST_FLANK / FAR_PERIMETER while the map's spawn points carry
 * Godot's Russian lane names («Северные ворота», «Левый фланг (Прорыв)», «Правый фланг», «Дальний периметр»); in Godot
 * they never matched. Both spellings now name the same lane. The Russian names are map / LevelJson data and stay as
 * matching aliases; the English display names ("North gate", ...) are accepted too and used for player text.
 */
namespace SpawnLaneRules
{
	/** NORTH_GATE / WEST_FLANK / EAST_FLANK / FAR_PERIMETER for any spelling (any case), else the name upper-cased. */
	CODEXTACTICS_API FString CanonicalLane(const FString& Lane);

	/** English player-facing lane name ("North gate", "West flank", ...) for a known lane, else the name as given. */
	CODEXTACTICS_API FString GetLaneDisplayName(const FString& Lane);

	/** Empty / "ANY" requests match every point; otherwise the canonical lanes are equal or one name contains the other. */
	CODEXTACTICS_API bool LanesMatch(const FString& PointLane, const FString& RequestedLane);
}
