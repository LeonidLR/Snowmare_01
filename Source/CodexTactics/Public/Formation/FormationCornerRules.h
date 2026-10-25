#pragma once

#include "CoreMinimal.h"
#include "Formation/FormationTypes.h"

/**
 * Slicing the pie at the free end of a wall the squad passes along (Godot reference: none (UE-only, user decision 2026-10-25
 * "corner: yes" in docs/design/FORMATION_SECTORS.md, contract docs/design/FORMATION_CONTRACT.md 10.3; reference implementation
 * Tools/TrainingGround/src/movement/rules.ts FormationCornerRules / cornerAhead)). Pure. Tests: CodexTactics.Formation.Corner.*,
 * parity CodexTactics.Formation.Parity.Corner.
 *
 * A free wall end beside the leader (CornerAhead): from CornerLeadDist before it until CornerHoldDist past it along his heading, ONE
 * member per side of the leader - the one closest to the end when the watch starts, not the other side's watcher - looks: before he is
 * past the end, at the corner (CornerGraze beyond the body); past it, at the point CornerPeekDist behind the wall's far face at the end.
 * The nearest such end on each side wins (a corridor exit: two corners, two men). It overrides that member's patrol sector only (walk
 * mode; not run, all forward or the wall layout); a combat target or an explicit facing order keeps priority (the integration).
 */

/** One watched wall end. */
struct FFormationCornerWatch
{
	/** Index into the walls array. */
	int32 Wall = 0;
	/** The end: 0 = A, 1 = B. */
	int32 End = 0;
	/** The member looking round it (chosen once per corner). */
	EFormationRole Role = EFormationRole::Commander;
};

/** At most one watched end on each side of the leader (start: none). */
struct FFormationCornerState
{
	TOptional<FFormationCornerWatch> Left;
	TOptional<FFormationCornerWatch> Right;
};

/** A watcher's look. */
struct FFormationCornerLook
{
	EFormationRole Role = EFormationRole::Commander;
	float Yaw = 0.f;
};

/** Watch's answer: the next state and the watchers' look yaws (left side first). */
struct FFormationCornerResult
{
	FFormationCornerState State;
	TArray<FFormationCornerLook> Looks;
};

namespace FormationCornerRules
{
	/** cm: the corner man's look grazes the wall end this far beyond its body. */
	inline constexpr double CornerGraze = 50.0;
	/** cm: a wall end within this of another wall is a joint (a corner of a room / corridor), not a free end to look round. */
	inline constexpr double CornerJoint = 20.0;

	/**
	 * The leader's distance along his heading to the free end End of Walls[WallIndex] that the wall runs back from, unset when it is not
	 * one: the wall across his heading (FormationTerrainRules::IsAcrossHeading), the end more than ClassifyProbeSide aside, the wall not
	 * running back from it, or another wall within the thicknesses + CornerJoint (a joint).
	 */
	CODEXTACTICS_API TOptional<float> CornerAhead(const TArray<FFormationWall>& Walls, int32 WallIndex, int32 End, const FVector2D& LeaderPos,
		float LeaderYaw, const FFormationConfig& Config = FFormationConfig());

	/** One watch step. Members = the squad's {role, position} (the leader first); their Yaw is ignored. */
	CODEXTACTICS_API FFormationCornerResult Watch(const TArray<FFormationWall>& Walls, const FVector2D& LeaderPos, float LeaderYaw,
		const TArray<FFormationSlot>& Members, const FFormationCornerState& State, const FFormationConfig& Config = FFormationConfig());
}
