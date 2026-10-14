#pragma once

#include "CoreMinimal.h"

/**
 * Leaving a building whose fixed interior camera is on (rev 5g item 5, user request 2026-10-14; UE-only, Godot had no exit aid):
 * EXIT arrows at its doorways and the player's interior / outside view toggle. Pure parts of UInteriorExitSubsystem.
 *
 * Doorways are found where the infantry navmesh crosses the camera zone's box: every edge of the box is sampled; a sample is
 * "crossable" when a point inside and a point outside the edge both lie on the navmesh and a navmesh raycast joins them (a
 * wall cuts the navmesh, a door does not). Runs of crossable samples at least MinDoorWidthCm wide are doors. A placed
 * ABuildingExitMarker in the zone replaces the automatic search for that zone.
 */
namespace InteriorExitRules
{
	/** Tuning of the doorway search. */
	struct FExitScanConfig
	{
		/** Distance between two samples along an edge, cm. */
		float SampleSpacingCm = 20.f;
		/** No samples this close to a corner of the box, cm. */
		float CornerMarginCm = 30.f;
		/** The inside test point lies this far inside the edge, cm (deeper than a wall is thick). */
		float InsideDepthCm = 120.f;
		/** The outside test point lies this far outside the edge, cm. */
		float OutsideDepthCm = 100.f;
		/** A run narrower than this is no door (a gap at a wall end, noise), cm. */
		float MinDoorWidthCm = 60.f;
		/** A test point counts as on the navmesh when its projection is at most this far away (2D), cm. */
		float ProjectToleranceCm = 35.f;
		/** The exit walk ends this far outside the door, cm. */
		float ExitDistanceCm = 200.f;
	};

	/** Number of box edges (local -X, +X, -Y, +Y). */
	constexpr int32 EdgeCount = 4;

	/**
	 * The samples of one edge of a box with HalfExtent (local XY): points ON the edge, from one corner margin to the other every
	 * SampleSpacingCm, and the edge's outward normal (local). Edge 0 = -X, 1 = +X, 2 = -Y, 3 = +Y.
	 */
	CODEXTACTICS_API void BuildEdgeSamples(const FVector2D& HalfExtent, int32 Edge, const FExitScanConfig& Config,
		TArray<FVector2D>& OutPoints, FVector2D& OutNormal);

	/** A run of crossable samples along one edge. */
	struct FDoorRun
	{
		int32 First = 0;
		int32 Last = 0;
		/** Samples x spacing, cm. */
		float WidthCm = 0.f;
	};

	/** The runs of true samples at least MinWidthCm wide (each sample stands for SpacingCm of the edge). */
	CODEXTACTICS_API TArray<FDoorRun> FindDoorRuns(const TArray<bool>& Crossable, float SpacingCm, float MinWidthCm);

	/** Who an EXIT click sends out. */
	enum class EExitOrderScope : uint8
	{
		/** Solo: the selected operative(s) only (the others keep holding). */
		Selected,
		/** Group mode: the whole squad. */
		Squad
	};

	CODEXTACTICS_API EExitOrderScope GetExitOrderScope(bool bSoloMode);

	/**
	 * The zone's fixed camera is the view: the leader is inside, the zone has a camera, the combat mode allows it, no cinematic owns
	 * the camera, and the player did not switch to the outside view (M).
	 */
	CODEXTACTICS_API bool ShouldShowZoneCamera(bool bZoneActive, bool bHasCamera, bool bModeAllowed, bool bCinematic, bool bPlayerOutsideView);

	/** EXIT arrows show while the interior camera is the view, outside turn-based combat (the grid has its own walk rules). */
	CODEXTACTICS_API bool ShouldShowExitArrows(bool bZoneCameraShown, bool bTurnBased);
}
