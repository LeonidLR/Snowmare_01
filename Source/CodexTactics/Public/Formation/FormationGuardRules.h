#pragma once

#include "CoreMinimal.h"
#include "Formation/FormationTypes.h"

/**
 * Formation guard - no formation may cut the squad in two with a wall (Godot reference: none (UE-only, user decisions 2026-10-25 in
 * docs/design/FORMATION_SECTORS.md, contract docs/design/FORMATION_CONTRACT.md 10.1 / 10.7; reference implementation
 * Tools/TrainingGround/src/movement/rules.ts FormationGuardRules)). Pure. Tests: CodexTactics.Formation.Guard.*, parity
 * CodexTactics.Formation.Parity.Guard / .TurnAhead.
 *
 * Any formation (manual or auto) cut by a wall: an echelon takes its mirror if the mirror is whole, anything else (or a cut mirror, or a
 * cut straight column) the TRAIL column (FormationShapeRules::TrailSlots: the column along the leader's own path, whole by
 * construction). Whole = no blocked roles at the leader AND at Ahead (his pose GuardLookahead further along his path), so the squad
 * folds before a passage entrance. A sharp turn ahead (TurnAhead > TurnFoldDeg) folds a WIDE formation (echelons, line) into the trail
 * column too. Hysteresis as Classify v2: a higher level at once; a lower one after ClassifyHoldDistance of path with it (at once when
 * the current fallback is itself cut); a new request starts over. The request is never cleared (a menu choice comes back by itself).
 */

/** How far the guard fell back: 0 = the requested formation, 1 = its mirror echelon, 2 = the trail column. */
enum class EFormationGuardLevel : uint8
{
	Requested = 0,
	Mirror = 1,
	Trail = 2
};

/** v2.3 part 9: why the guard falls back - a wall cuts the formation (or a man off), or a sharp turn ahead (user "5B"). */
enum class EFormationGuardCause : uint8
{
	Wall,
	Turn
};

/** The guard's state carried tick to tick (contract { requested, level, effective, candidate, candidateDistance, cause }). */
struct FFormationGuardState
{
	/** The formation asked for (the menu's manual choice, or FormationFor of the terrain); unset before the first Resolve. */
	TOptional<EFormationKind> Requested;
	EFormationGuardLevel Level = EFormationGuardLevel::Requested;
	/** The formation in effect (= Requested at level 0; the trail column is Column at level 2). */
	TOptional<EFormationKind> Effective;
	/** A lower level waiting for its path distance; unset = none. */
	TOptional<EFormationGuardLevel> Candidate;
	/** Leader path walked while Candidate has been the raw level, cm. */
	float CandidateDistance = 0.f;
	/**
	 * v2.3 part 9 (user 2026-10-26: the turn fold's HUD says "(turn)"): why the level in force is above 0 - Wall when a wall cuts the
	 * requested formation or a member is cut off (it wins over a turn at the same time), Turn for the turn fold alone; unset at level 0.
	 * Kept while a fallback is held.
	 */
	TOptional<EFormationGuardCause> Cause;
};

/** A position + heading (the leader's pose ahead on his path). */
struct FFormationPose
{
	FVector2D Pos = FVector2D::ZeroVector;
	float Yaw = 0.f;
};

/** What a guard state means for the slots. */
struct FFormationGuardEffective
{
	/** The formation in effect (unset before the first Resolve). */
	TOptional<EFormationKind> Formation;
	/** True at level Trail: the slots come from FormationShapeRules::TrailSlots, not Slots. */
	bool bTrail = false;
};

namespace FormationGuardRules
{
	/**
	 * Neighbours in a formation's chain (the leader's own segments to every slot come on top): echelon right K-L-P-Z, echelon left /
	 * column K-P-L-Z, line L-K-P-Z (three: Z-K-P), diamond / wedge / blizzard P-Z (+ L-Z with 4), hedgehog the ring K-P-Z-L-K. Roles the member
	 * count lacks are skipped (the chain closes over them). None for 2 members or the wall.
	 */
	CODEXTACTICS_API TArray<TPair<EFormationRole, EFormationRole>> ChainPairs(EFormationKind Formation, int32 MemberCount);

	/**
	 * The roles whose slot a wall cuts off (in slot order): the segment from the leader to the slot, or a chain segment touching it,
	 * passes within GuardClearance of a wall body (FormationMath::SegmentBlocked). Empty = whole here. The wall is never blocked.
	 */
	CODEXTACTICS_API TArray<EFormationRole> BlockedRoles(const TArray<FFormationWall>& Walls, EFormationKind Formation, int32 MemberCount,
		const FVector2D& LeaderPos, float LeaderYaw, const FFormationConfig& Config = FFormationConfig());

	/**
	 * v2.1.1: the sharpest heading change on the leader's path within TurnLookahead ahead of him: the largest |Heading - piece
	 * direction| over the path pieces starting within that distance (Path[0] = the leader now; the nav path, then the route's next
	 * points), deg 0..180; 0 for a straight path or none.
	 */
	CODEXTACTICS_API float TurnAhead(const TArray<FVector2D>& Path, float Heading, const FFormationConfig& Config = FFormationConfig());

	/**
	 * The guard step: the formation in effect for Requested (see the header comment). Ahead = the leader's pose GuardLookahead further
	 * along his path (unset = standing / arrived); TurnAheadDeg = TurnAhead of his path; StepDistance = his path this tick, cm.
	 * v2.3 part 6 (contract 13.6): bAllInSight = every member has a clear line (no wall body) to the leader; false -> the trail column at
	 * once (cause Wall), released by the usual hold. v2.3 part 9: the state names its Cause (unset at level 0).
	 */
	CODEXTACTICS_API FFormationGuardState Resolve(const TArray<FFormationWall>& Walls, EFormationKind Requested, int32 MemberCount,
		const FVector2D& LeaderPos, float LeaderYaw, const TOptional<FFormationPose>& Ahead, float TurnAheadDeg, const FFormationGuardState& State,
		float StepDistance, const FFormationConfig& Config = FFormationConfig(), bool bAllInSight = true);

	/** The formation in effect at a guard state, and whether its slots are the trail column's (level Trail). */
	CODEXTACTICS_API FFormationGuardEffective Effective(const FFormationGuardState& State);
}
