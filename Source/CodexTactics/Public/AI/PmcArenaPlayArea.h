#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/**
 * The PMC arena's play area (the game designer 2026-11-10, fix/arena-no-outer-strip): the arena's spawns and objective points never
 * stand in the Depot's outer strip between the long perimeter wall and the map edge - a fight there is hidden from the camera and
 * teaches the night trainer little. Per level, data-driven: the level JSON's "pmc_arena" object -> "play_area" (world cm, the XY plane
 * only; any height):
 *   "play_area": { "include_box_cm": { "min": [x, y], "max": [x, y] },                     optional: the allowed region (none = the whole map)
 *                  "exclude_boxes_cm": [ { "id": "...", "min": [x, y], "max": [x, y] } ] }  optional: regions never used
 * Pure rules used by CodexTactics.PmcArena (CodexTacticsDev, Debug/PmcArenaCommand.cpp): every sampler there (random spawns, scenario
 * sites, the far / near spawns, the objective, the squad's spawn slots) rejects a point outside the area. Arena-only; no gameplay code
 * uses it. UE-only, no Godot reference. Tested in CodexTactics.AI.PmcArena.PlayArea.*.
 */
struct CODEXTACTICS_API FPmcArenaPlayArea
{
	/** True = Include is the allowed region; false = the whole map is (only the exclusion boxes apply). */
	bool bHasInclude = false;
	FBox2D Include = FBox2D(ForceInit);
	/** Regions never used (each a closed box: its edge counts as excluded). */
	TArray<FBox2D> Exclude;
	/** The exclusion boxes' ids (the JSON's "id", else "exclude_<n>"), parallel to Exclude; logs only. */
	TArray<FString> ExcludeIds;

	/** True when the area restricts anything (an inclusion box or at least one exclusion box). */
	bool IsRestricted() const { return bHasInclude || !Exclude.IsEmpty(); }
};

namespace PmcArenaPlayAreaRules
{
	/**
	 * Point (world cm, Z ignored) is usable: inside the inclusion box (closed: a point on its edge is inside) when there is one, and in no
	 * exclusion box (closed: a point on an exclusion edge is OUT - where an exclusion box touches the inclusion edge, the exclusion wins).
	 * An unrestricted area accepts every point.
	 */
	CODEXTACTICS_API bool IsInside(const FPmcArenaPlayArea& Area, const FVector2D& Point);
	CODEXTACTICS_API bool IsInside(const FPmcArenaPlayArea& Area, const FVector& Point);

	/**
	 * The box to sample random points in: NavBounds clipped to the inclusion box in X / Y (Z kept), so the samplers waste no tries outside
	 * it. NavBounds unchanged without an inclusion box, or when the two do not overlap (the samplers then reject every point and fall back).
	 */
	CODEXTACTICS_API FBox SampleBounds(const FPmcArenaPlayArea& Area, const FBox& NavBounds);

	/**
	 * Reads the "play_area" object of a level JSON's "pmc_arena" object (Arena = that "pmc_arena" object). Missing "play_area" = an
	 * unrestricted area, true. A box = { "min": [x, y], "max": [x, y] } in world cm (the corners in any order; a degenerate box with zero
	 * width or depth is rejected). A malformed box is skipped with a line in OutErrors (the rest still loads); returns false when any
	 * entry was skipped.
	 */
	CODEXTACTICS_API bool ReadJson(const FJsonObject& Arena, FPmcArenaPlayArea& Out, TArray<FString>& OutErrors);

	/** One log line: "include [x0, y0]..[x1, y1], 2 exclusion boxes (west_strip, ...)" / "unrestricted". */
	CODEXTACTICS_API FString Describe(const FPmcArenaPlayArea& Area);
}
