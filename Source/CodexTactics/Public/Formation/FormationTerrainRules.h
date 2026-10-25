#pragma once

#include "CoreMinimal.h"
#include "Formation/FormationTypes.h"

/**
 * Terrain classifier for the automatic formation choice (Godot reference: none (UE-only, design docs/design/FORMATION_SECTORS.md
 * "Selection", contract docs/design/FORMATION_CONTRACT.md 2; reference implementation Tools/TrainingGround/src/movement/rules.ts
 * rawClass / Classify / FormationFor, geometry.ts raycastWalls)). Pure: the walls are 2D segments with a thickness. Tests:
 * CodexTactics.Formation.Terrain.*, parity CodexTactics.Formation.Parity.Classify / .FormationFor.
 *
 * Raw class (ClassifyRaw):
 * 1. Forward rays first, at 0 / -20 / +20 deg off the heading, length ClassifyAtWallDist: any hit -> at_wall.
 * 2. Side clearance per side: rays at 60 / 90 / 120 deg off the heading to that side (right = +, left = -), each long enough to reach
 *    ClassifyProbeSide ACROSS the heading (ClassifyProbeSide / sin a); clearance = min(hit * sin a) (the lateral distance); the side is
 *    blocked when its clearance <= ClassifyProbeSide (the slanted rays bridge a short gap in a wall). v1.1: the side rays ignore walls
 *    ACROSS the heading (IsAcrossHeading with ClassifySideIgnorePerpDeg: a dead end ahead is not a pair of side walls).
 * 3. Both blocked: Left + Right < ClassifyCorridorWidth -> corridor, else open (a wide street fits the diamond); only the left -> wall_left;
 *    only the right -> wall_right; none -> open.
 * A ray meets a thick wall at its faces (the centre line offset by +-Thickness / 2; a thin wall at its centre line) and is 0 when it
 * starts inside the wall body; a ray parallel to a wall never hits it.
 *
 * Hysteresis (Classify): no previous class -> the raw class at once; else a class is kept for at least ClassifyHoldTime (TimeInClass =
 * seconds since it was entered) before a different raw class may take over (FFormationTerrainTracker keeps the time).
 *
 * PassageWidth (v1.1): Left + Right clearance (the same side rays) when both sides are blocked, else unset - feeds LookYaw's narrow /
 * wide column.
 *
 * Mapping (FormationFor): open -> diamond (4 or more) / wedge (3 or fewer), wall_left -> echelon RIGHT (away from the wall), wall_right ->
 * echelon LEFT, corridor -> column, at_wall -> wall.
 */
namespace FormationTerrainRules
{
	/** Distance along the ray Origin + Dir * t (Dir unit) to the first wall surface within MaxDistance; unset = none. */
	CODEXTACTICS_API TOptional<float> RayHit(const TArray<FFormationWall>& Walls, const FVector2D& Origin, const FVector2D& Dir, float MaxDistance);

	/**
	 * v1.1: true when the wall's direction A -> B is within IgnorePerpDeg of perpendicular to Yaw (offParallel = min(|d|, 180 - |d|) of
	 * the direction's yaw off Yaw; across when offParallel >= 90 - IgnorePerpDeg).
	 */
	CODEXTACTICS_API bool IsAcrossHeading(const FFormationWall& Wall, float Yaw, float IgnorePerpDeg);

	/**
	 * Free lateral distance to a wall on one side (Side +1 = right, -1 = left) within Reach; unset = none. IgnorePerpDeg > 0: walls
	 * across the heading (IsAcrossHeading) are left out.
	 */
	CODEXTACTICS_API TOptional<float> SideClearance(const TArray<FFormationWall>& Walls, const FVector2D& Pos, float Yaw, int32 Side, float Reach,
		float IgnorePerpDeg = 0.f);

	/** v1.1: the free width between the walls on both sides (left + right clearance), unset when a side is open. */
	CODEXTACTICS_API TOptional<float> PassageWidth(const TArray<FFormationWall>& Walls, const FVector2D& LeaderPos, float LeaderYaw,
		const FFormationConfig& Config = FFormationConfig());

	/** The class the probes say right now (no hysteresis). */
	CODEXTACTICS_API EFormationTerrainClass ClassifyRaw(const TArray<FFormationWall>& Walls, const FVector2D& LeaderPos, float LeaderYaw,
		const FFormationConfig& Config = FFormationConfig());

	/** The hysteresis step alone: no previous -> Raw; Raw == previous -> Raw; else previous while TimeInClass < ClassifyHoldTime. */
	CODEXTACTICS_API EFormationTerrainClass ApplyHysteresis(EFormationTerrainClass RawClass, const TOptional<EFormationTerrainClass>& PreviousClass,
		float TimeInClass, const FFormationConfig& Config = FFormationConfig());

	/** The raw class through the hysteresis (contract Classify). */
	CODEXTACTICS_API EFormationTerrainClass Classify(const TArray<FFormationWall>& Walls, const FVector2D& LeaderPos, float LeaderYaw,
		const TOptional<EFormationTerrainClass>& PreviousClass, float TimeInClass, const FFormationConfig& Config = FFormationConfig());

	/** The formation for a terrain class and squad size (see the header comment). */
	CODEXTACTICS_API EFormationKind FormationFor(EFormationTerrainClass Class, int32 MemberCount);
}

/** Keeps the terrain class and its time over frames (the runtime side of Classify's hysteresis). */
struct CODEXTACTICS_API FFormationTerrainTracker
{
	/** The class in force; unset until the first Step. */
	TOptional<EFormationTerrainClass> Class;
	/** Seconds since Class was entered. */
	float TimeInClass = 0.f;

	/** Advances the time by DeltaSeconds, classifies (Classify) and returns the class in force (a change restarts the time). */
	EFormationTerrainClass Step(const TArray<FFormationWall>& Walls, const FVector2D& LeaderPos, float LeaderYaw, float DeltaSeconds,
		const FFormationConfig& Config = FFormationConfig());
};
