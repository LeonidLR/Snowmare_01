#pragma once

#include "CoreMinimal.h"

/**
 * Pure rules of the red "the horde comes from there" edge arrows (game designer 2026-10-25: "arrows in RED show from which
 * direction the horde is coming; several hordes = several arrows"; UE-only, no Godot reference). The wave's live enemies are
 * grouped by their BEARING from the squad (robust: it does not depend on lane names, and a lane split over two spawn points or
 * two waves on one lane still read right); enemies already within HideRangeCm are engaged and give no arrow.
 */
namespace HordeArrowRules
{
	/** Enemies closer than this to the squad (flat, cm) are engaged: no arrow for them. */
	constexpr float DefaultHideRangeCm = 1800.f;
	/** Two enemies whose bearings differ by less than this (degrees, chained) read as one approach direction. */
	constexpr float DefaultMergeAngleDeg = 35.f;

	/** One approach direction. */
	struct FGroup
	{
		/** Mean bearing from the origin, degrees: atan2(dY, dX) of the world (0 = +X, 90 = +Y), in (-180, 180]. */
		float BearingDeg = 0.f;
		/** Flat distance (cm) from the origin to the nearest member. */
		float NearestCm = 0.f;
		/** Number of enemies in the group. */
		int32 Count = 0;
		/** Mean location of the members (the point the arrow points at). */
		FVector Centroid = FVector::ZeroVector;
	};

	/** Signed shortest angle from A to B, degrees, in (-180, 180]. */
	CODEXTACTICS_API float AngleDeltaDeg(float FromDeg, float ToDeg);

	/**
	 * Groups Positions by bearing from Origin (flat). Members within HideRangeCm are dropped. Bearings are sorted round the circle
	 * and split wherever the gap to the next one is at least MergeAngleDeg (so a chain of close enemies is one group, two lanes
	 * more than MergeAngleDeg apart are two). Result ordered by NearestCm (closest approach first).
	 */
	CODEXTACTICS_API TArray<FGroup> GroupByBearing(const FVector& Origin, const TArray<FVector>& Positions, float HideRangeCm = DefaultHideRangeCm,
		float MergeAngleDeg = DefaultMergeAngleDeg);

	/**
	 * Keeps arrow tips apart: a tip that lies within MinGapPx of an earlier tip (or of an Occupied one, e.g. the tutorial's guide
	 * arrow) is slid along its edge (perpendicular to Direction) by MinGapPx steps, alternating sides, until free or out of tries;
	 * the result stays inside the inset ViewSize rectangle.
	 */
	CODEXTACTICS_API void SpreadTips(const FVector2D& ViewSize, float MarginPx, float MinGapPx, const TArray<FVector2D>& Occupied, const TArray<FVector2D>& Directions,
		TArray<FVector2D>& InOutTips);
}
