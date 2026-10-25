#pragma once

#include "CoreMinimal.h"
#include "Formation/FormationTypes.h"

/**
 * Which side of a wall the leader - and with him the whole squad - passes (Godot reference: none (UE-only, user decisions 2026-10-25
 * "B" / "the squad never splits" in docs/design/FORMATION_SECTORS.md, contract docs/design/FORMATION_CONTRACT.md 10.2; reference
 * implementation Tools/TrainingGround/src/movement/rules.ts FormationBypassRules / wallSideOf)). Pure. Tests:
 * CodexTactics.Formation.Bypass.*, parity CodexTactics.Formation.Parity.Bypass.
 *
 * Candidates: the straight line and the paths over one or two of the wall's four corners (BypassRadius off the body), no leg closer to
 * the body than BypassRadius (or than its own end point already is); the shortest per side. No known threat (0 <= AgeS <=
 * BypassThreatMemoryS) -> the shortest side. With threats: the side whose path has the larger share of samples (BypassSample apart)
 * hidden from EVERY known threat by this wall is the shielded one; taken when its length <= (1 + BypassMaxDetour) x the shortest, else
 * the shortest stays (TooLong).
 */

/** The wall on the squad's left or right as it goes by (passing round the wall's left end = the wall on the right). */
enum class EFormationWallSide : uint8
{
	Left,
	Right
};

/** Why ChooseSide took its side. */
enum class EFormationBypassReason : uint8
{
	/** No known threat, or the wall hides it on neither / the shortest side. */
	Shortest,
	/** The detour that hides the squad from the threat was taken. */
	Shielded,
	/** A side would hide the threat but its detour exceeds BypassMaxDetour. */
	TooLong
};

/** A known threat (an enemy seen, or heard / last known position) and the seconds since that contact. */
struct FFormationThreat
{
	FVector2D Pos = FVector2D::ZeroVector;
	float AgeS = 0.f;
};

/** ChooseSide's answer. */
struct FFormationBypassChoice
{
	EFormationWallSide WallOn = EFormationWallSide::Left;
	/** The chosen path's length over the shortest one's, minus 1 (0 = the shortest). */
	float Detour = 0.f;
	/** Which side the shortest path takes. */
	EFormationWallSide Shortest = EFormationWallSide::Left;
	EFormationBypassReason Reason = EFormationBypassReason::Shortest;
	/** The chosen path round the wall, leader to destination (corners BypassRadius off the wall body). */
	TArray<FVector2D> Path;
};

namespace FormationBypassRules
{
	/** cm: the bypass paths keep this far off the wall body (room for the column to wrap the end; the corner man's distance). */
	inline constexpr double BypassRadius = 150.0;
	/** cm: shielding is sampled along a candidate path at this step. */
	inline constexpr double BypassSample = 50.0;

	/**
	 * Which side a path from its first to its last point passes Wall (the path must not cross the wall body): the wall end farther off
	 * the straight line first -> last is the reference R; R's side of the straight line (UE: +Y = right), flipped when the loop path +
	 * the straight line back winds round R.
	 */
	CODEXTACTICS_API EFormationWallSide WallSideOf(const TArray<FVector2D>& Path, const FFormationWall& Wall);

	/** The side of Wall the leader passes on his way to Destination (see the header comment). */
	CODEXTACTICS_API FFormationBypassChoice ChooseSide(const FFormationWall& Wall, const FVector2D& LeaderPos, const FVector2D& Destination,
		const TArray<FFormationThreat>& Threats, const FFormationConfig& Config = FFormationConfig());
}
