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
 * 1. Only when the leader is ARRIVING (v1.2: Destination set and within ClassifyArriveDist of him - walking past a wall or turning at a
 *    corridor bend never triggers the wall layout): forward rays first, at 0 / -20 / +20 deg off the heading, length
 *    ClassifyAtWallDist: any hit -> at_wall.
 * 2. Side clearance per side: rays at 60 / 90 / 120 deg off the heading to that side (right = +, left = -), each long enough to reach
 *    ClassifyProbeSide ACROSS the heading (ClassifyProbeSide / sin a); clearance = min(hit * sin a) (the lateral distance); the side is
 *    blocked when its clearance <= ClassifyProbeSide (the slanted rays bridge a short gap in a wall). v1.1: the side rays ignore walls
 *    ACROSS the heading (IsAcrossHeading with ClassifySideIgnorePerpDeg: a dead end ahead is not a pair of side walls).
 * 3. Both blocked -> corridor whatever the width (v1.2, user decision: any passage is a column; PassageWidth picks the narrow / wide
 *    column sectors); only the left -> wall_left; only the right -> wall_right; none -> open.
 * A ray meets a thick wall at its faces (the centre line offset by +-Thickness / 2; a thin wall at its centre line) and is 0 when it
 * starts inside the wall body; a ray parallel to a wall never hits it.
 *
 * Hysteresis (v2, contract 8: in cm of the LEADER'S PATH, asymmetric; Classify / AdvanceState, state in -> state out):
 * - no current class -> the raw class at once; the raw class equal to the current one -> kept (the candidate is dropped);
 * - TIGHTER at once: a raw class of higher ClassRank (open 0 < wall_left / wall_right 1 < corridor 2 < at_wall 3) replaces the current
 *   one immediately (the squad never walks a diamond into a passage); leaving at_wall (a new move) is immediate too;
 * - LOOSER (or equal rank, e.g. wall_left -> wall_right) only after it has been the raw class for ClassifyHoldDistance of the leader's
 *   path: StepDistance per tick adds to CandidateDistance while the raw class stays that candidate (another raw class restarts it;
 *   standing still adds nothing). A gap in a wall shorter than that never flips the echelon; leaving a passage the column holds
 *   until its tail is out.
 * - PassageWidth of the state: PassageWidth (measured) while the raw class is corridor, HELD while the current class stays corridor
 *   after the leader has left the passage (the column's tail keeps the narrow sectors), unset for any other current class - this is
 *   what LookYaw takes.
 * FFormationTerrainTracker is the runtime's entry point (keeps the state).
 *
 * Destination (v1.2) = where the leader's current move ends (unset = none); in the wall layout the leader's own wall slot is his
 * destination (his last step must not drop the layout).
 *
 * PassageWidth (v1.1): Left + Right clearance (the same side rays) when both sides are blocked, else unset - feeds LookYaw's narrow /
 * wide column.
 *
 * Mapping (FormationFor): open -> diamond (4 or more) / wedge (3 or fewer) - in a blizzard (v1.2, user decision) the blizzard tight
 * diamond; wall_left -> echelon RIGHT (away from the wall), wall_right ->
 * echelon LEFT, corridor -> column, at_wall -> wall.
 */
/** v2 classifier state carried tick to tick (contract 8: {current, candidate, candidateDistance, passageWidth}). */
struct FFormationClassifyState
{
	/** The class in force; unset = no history (start). */
	TOptional<EFormationTerrainClass> Current;
	/** A looser raw class waiting for its path distance; unset = none. */
	TOptional<EFormationTerrainClass> Candidate;
	/** Leader path walked while Candidate has been the raw class, cm. */
	float CandidateDistance = 0.f;
	/** The passage width while Current is corridor (measured, or held after the leader left the passage), else unset; LookYaw's input. */
	TOptional<float> PassageWidth;
};

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

	/**
	 * v2.1: the free width round a man when at least ONE side has a wall body within ClassifyProbeSide, unset when both are open.
	 * Measured to each wall's closest point (body distance; the side = which side of the heading that point lies, right = +), so a
	 * wall END beside him counts as well as a wall along him; an open side counts as ClassifyProbeSide. The trail column's sectors
	 * use it per member (a wall beside a man -> the narrow column's sectors).
	 */
	CODEXTACTICS_API TOptional<float> SideWidth(const TArray<FFormationWall>& Walls, const FVector2D& Pos, float Yaw,
		const FFormationConfig& Config = FFormationConfig());

	/** v1.2: the leader is arriving = Destination is set and within ClassifyArriveDist of LeaderPos. */
	CODEXTACTICS_API bool IsArriving(const FVector2D& LeaderPos, const TOptional<FVector2D>& Destination, const FFormationConfig& Config = FFormationConfig());

	/** The class the probes say right now (no hysteresis); Destination = the leader's move destination (at_wall only on arrival). */
	CODEXTACTICS_API EFormationTerrainClass ClassifyRaw(const TArray<FFormationWall>& Walls, const FVector2D& LeaderPos, float LeaderYaw,
		const FFormationConfig& Config = FFormationConfig(), const TOptional<FVector2D>& Destination = TOptional<FVector2D>());

	/** v2: how tight a class is - open 0, a wall on one side 1, a passage 2, at a wall 3. */
	CODEXTACTICS_API int32 ClassRank(EFormationTerrainClass Class);

	/**
	 * v2: the hysteresis step alone (Classify without the probes): RawClass = ClassifyRaw, MeasuredWidth = PassageWidth when RawClass is
	 * corridor (else unset); StepDistance = the leader's path this tick, cm. Returns the next state (see the header comment).
	 */
	CODEXTACTICS_API FFormationClassifyState AdvanceState(EFormationTerrainClass RawClass, const TOptional<float>& MeasuredWidth,
		const FFormationClassifyState& State, float StepDistance, const FFormationConfig& Config = FFormationConfig());

	/** v2 contract Classify: the probes (ClassifyRaw, PassageWidth) through AdvanceState. Pure: state in -> state out. */
	CODEXTACTICS_API FFormationClassifyState Classify(const TArray<FFormationWall>& Walls, const FVector2D& LeaderPos, float LeaderYaw,
		const TOptional<FVector2D>& Destination, const FFormationClassifyState& State, float StepDistance,
		const FFormationConfig& Config = FFormationConfig());

	/** The formation for a terrain class and squad size; bBlizzard (v1.2): open ground in a blizzard -> the blizzard tight diamond. */
	CODEXTACTICS_API EFormationKind FormationFor(EFormationTerrainClass Class, int32 MemberCount, bool bBlizzard = false);
}

/**
 * The runtime's entry point to the terrain classifier (v2): keeps FFormationClassifyState tick to tick. Each tick: Step with the leader's
 * position, heading, move destination and the path he walked since the last Step; read GetClass (-> FormationTerrainRules::FormationFor)
 * and GetPassageWidth (-> FormationSectorRules::LookYaw).
 */
struct CODEXTACTICS_API FFormationTerrainTracker
{
	/** The classifier state (Current unset until the first Step). */
	FFormationClassifyState State;

	/**
	 * Classifies (FormationTerrainRules::Classify) and returns the class in force. Destination = the leader's move destination (unset =
	 * none: no at_wall); StepDistance = the leader's path since the last Step, cm (standing still: 0).
	 */
	EFormationTerrainClass Step(const TArray<FFormationWall>& Walls, const FVector2D& LeaderPos, float LeaderYaw, const TOptional<FVector2D>& Destination,
		float StepDistance, const FFormationConfig& Config = FFormationConfig());

	/** The class in force (open before the first Step). */
	EFormationTerrainClass GetClass() const;

	/** The passage width LookYaw takes (held with the corridor class; unset otherwise). */
	TOptional<float> GetPassageWidth() const;

	/** Forgets the history (the next Step takes the raw class at once). */
	void Reset();
};
