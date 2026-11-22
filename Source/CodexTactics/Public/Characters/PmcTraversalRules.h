#pragma once

#include "CoreMinimal.h"

/**
 * Pure rules of the PMC traversal check (smart enemies T-12, docs/design/gdd/smart-enemies-roadmap.md; task 095; UE-only, no Godot
 * reference): how long a traversal leg may take, when a walk counts as having gone up a stair flight, when the walker has really
 * arrived (on the ground, not mid-air / mid-climb), the per-kind pass bar and the failure groups the smoke prints.
 * Used by CodexTactics.PmcTraversalSmoke (Source/CodexTacticsDev/Private/Debug/PmcTraversalSmokeCommand.cpp); tested in
 * CodexTactics.AI.PmcTraversal.*.
 */

/** Time budget of one leg (Content/Data/AI/pmc_traversal.json). */
struct CODEXTACTICS_API FPmcTraversalTimeout
{
	/** Floor of every leg, s (the old flat leg_timeout_seconds). */
	float BaseSeconds = 15.f;
	/** Walking speed the walk part is budgeted at, cm/s (deliberately slow: detours, turns). */
	float WalkSpeedCms = 150.f;
	/** Ladder climbing speed budgeted, cm/s (the clips climb ~41 cm/s; margin for the align / top exit). */
	float LadderClimbCms = 30.f;
	/** Fixed extra for a ladder's mount / top exit or a mantle clip, s. */
	float TransitionSeconds = 8.f;
};

/** What a leg arrival check sees of the walker this tick. */
struct CODEXTACTICS_API FPmcTraversalArrival
{
	float Dist2DCm = 0.f;
	float FeetDistZCm = 0.f;
	bool bMovingOnGround = false;
	float VelocityZ = 0.f;
	/** Mantling, on a ladder or dropping / settling after a drop. */
	bool bBusyTraversing = false;
};

namespace PmcTraversalRules
{
	/** Seconds a leg may take: base + walk distance / walk speed + ladder height / climb speed (+ transition when it climbs). */
	CODEXTACTICS_API float LegTimeoutSeconds(const FPmcTraversalTimeout& Timeout, float WalkCm, float LadderHeightCm, bool bTransition);

	/** 2D distance from P to the polyline Points (a single point: the distance to it; empty: a huge number). */
	CODEXTACTICS_API float DistToPolyline2D(const FVector2D& P, TConstArrayView<FVector2D> Points);

	/**
	 * Evidence of a stair flight being walked (review 095: a 2D near-check passed at spawn and under the flight): the feet are inside
	 * the middle band of the flight's height (LowZ + BandFraction * H .. HighZ - BandFraction * H) while within RadiusCm (2D) of the
	 * flight's polyline (foot -> mid -> top). False for a flight lower than 60 cm (no band to see).
	 */
	CODEXTACTICS_API bool IsOnStairFlight(const FVector& Feet, TConstArrayView<FVector2D> Flight, float LowZ, float HighZ, float RadiusCm,
		float BandFraction = 0.25f);

	/**
	 * The walker has arrived (review 095 item 6): within RadiusCm / ToleranceZCm of the goal, walking on the ground, no vertical speed
	 * above MaxVelocityZ and no mantle / ladder / drop in progress.
	 */
	CODEXTACTICS_API bool HasArrived(const FPmcTraversalArrival& Arrival, float RadiusCm, float ToleranceZCm, float MaxVelocityZ = 30.f);

	/** The acceptance bar: something sampled and Crossed / Sampled >= BarPercent. */
	CODEXTACTICS_API bool MeetsBar(int32 Crossed, int32 Sampled, double BarPercent = 95.0);

	/** Crossed / Sampled in percent (0 when nothing was sampled). */
	CODEXTACTICS_API double RatePercent(int32 Crossed, int32 Sampled);

	/** Counts equal keys; sorted by count (most first), then key (deterministic order for the printed failure groups). */
	CODEXTACTICS_API TArray<TPair<FString, int32>> GroupCounts(TConstArrayView<FString> Keys);
}
