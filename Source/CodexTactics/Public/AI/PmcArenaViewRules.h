#pragma once

#include "CoreMinimal.h"

/**
 * The PMC arena's clip readability (fix/arena-clip-readability 2026-11-10: the game designer could not read the night trainer's
 * arena clips - the two squads looked the same, part of one squad stood behind a fence, the camera lost one squad). Pure rules used
 * by CodexTactics.PmcArena (CodexTacticsDev, Debug/PmcArenaCommand.cpp): the record camera's framing / smoothing, the team overlay
 * colours and the spawn placement check. Arena-only; no gameplay code uses them. UE-only, no Godot reference.
 * Tested in CodexTactics.AI.PmcArena.ClipView.* / .TeamColors / .SpawnPlacement.
 */

/** A framed arena view: the look point and the eye's distance from it along the camera's view direction, cm. */
struct CODEXTACTICS_API FPmcArenaView
{
	FVector Look = FVector::ZeroVector;
	float DistanceCm = 0.f;
};

/** The record camera's framing parameters (the arena defaults: 65 deg down, a 50 deg horizontal FOV, 16:9, 20..110 m). */
struct CODEXTACTICS_API FPmcArenaFraming
{
	/** Degrees below the horizon (65 = steep top-down: fences hide less). */
	float PitchDeg = 65.f;
	/** The camera's view yaw (degrees, world); the arena turns it so the two spawns lie across the wide frame. */
	float YawDeg = 0.f;
	float HorizontalFovDeg = 50.f;
	float Aspect = 16.f / 9.f;
	/** Free room round the box on every side, cm. */
	float MarginCm = 600.f;
	/** The orthographic fit x this (the near half of a steep view spreads wider on the screen than the fit assumes). */
	float PerspectiveSlack = 1.12f;
	float MinDistanceCm = 2000.f;
	float MaxDistanceCm = 11000.f;
};

namespace PmcArenaViewRules
{
	/**
	 * The view that fits all Points (the live fighters + the objective) in the frame: the box of the points in the camera's horizontal
	 * right / forward frame, its centre = the look point (Z = the points' mean), the distance from the screen extents (right = width,
	 * forward x sin(pitch) + height x cos(pitch) = height; an orthographic fit, the margin covers the perspective), clamped
	 * Min..MaxDistance; a box too wide for MaxDistance is centred on the points' mean instead (the bulk in the middle, an outlier
	 * off the edge). No points: a zero look at the minimum distance.
	 */
	CODEXTACTICS_API FPmcArenaView FrameBox(const TArray<FVector>& Points, const FPmcArenaFraming& Framing);

	/**
	 * The fighters the camera frames: a man with nobody (no other fighter, not the objective when bHasObjective) within IsolationCm is a
	 * lone flanker far off - left out, so the fight itself stays big on the frame (a Depot round's 80 m detour of one man made everybody
	 * 8 px tall). A side is never dropped: when all its live men are lone, they are all kept (both squads stay in the frame). Sides[i] =
	 * fighter i's side (any ids; missing = one side). Returns the kept fighters' positions.
	 */
	CODEXTACTICS_API TArray<FVector> FramedFighters(const TArray<FVector>& Fighters, const TArray<int32>& Sides, bool bHasObjective, const FVector& Objective,
		float IsolationCm = 3000.f);

	/** The eye of View: back from the look point against the view direction (pitch down, yaw) by the distance. */
	CODEXTACTICS_API FVector EyeOf(const FPmcArenaView& View, const FPmcArenaFraming& Framing);

	/** The camera's yaw for a round: perpendicular to the A -> B spawn axis (the two squads left and right on the wide frame). */
	CODEXTACTICS_API float YawAcross(const FVector& SpawnA, const FVector& SpawnB);

	/** The exponential smoothing factor of a step: 1 - 2^(-Delta / HalfLife) (frame-rate independent; HalfLife <= 0 = 1, a snap). */
	CODEXTACTICS_API float SmoothAlpha(float DeltaSeconds, float HalfLifeSeconds);

	/**
	 * Current eased towards Target: the look point with LookHalfLife, the distance with ZoomOutHalfLife when it grows (a fighter leaving
	 * the frame is caught fast) and the slower ZoomInHalfLife when it shrinks (no pumping when a man dies or steps back in).
	 */
	CODEXTACTICS_API FPmcArenaView Smooth(const FPmcArenaView& Current, const FPmcArenaView& Target, float DeltaSeconds, float LookHalfLife = 0.6f,
		float ZoomOutHalfLife = 0.5f, float ZoomInHalfLife = 1.5f);

	/** The team overlay colour (the game designer 2026-11-10): side 0 (team A) red, side 1 (team B) green; rgb + the overlay alpha. */
	CODEXTACTICS_API FLinearColor TeamColor(int32 SideIndex);

	/**
	 * A squad member's spawn spot is accepted when it reaches the round's goal (the objective, else the other side's spawn) by a full path
	 * and lies close to its side's spawn by the navmesh as well as by air: path length <= straight x DetourFactor + SlackCm. A spot behind a
	 * fence / wall (the Depot street, 2026-11-10) is a few metres away by air but reached only round the fence's end - rejected.
	 * PathFromSpawnCm < 0 = no full path from the spawn.
	 */
	CODEXTACTICS_API bool IsSpawnSpotUsable(bool bReachesGoal, float PathFromSpawnCm, float StraightFromSpawnCm, float DetourFactor = 2.f, float SlackCm = 500.f);
}
