#pragma once

#include "CoreMinimal.h"
#include "Formation/FormationTypes.h"
#include "Formation/FormationWallRules.h"

/**
 * The wall layout's look sectors at a wall with two ends - symmetry and no duplicated sector while another is open (Godot reference: none
 * (UE-only; the game designer's bug report 2026-11-02 item 5 on L_Gen_Depot_1, docs/design/FORMATION_SECTORS.md "v2.7 - both wall ends").
 * Pure. Tests: CodexTactics.Formation.WallSectors.*. Runs AFTER FormationWallRules::EndLooks (whose golden parity with the Training Ground
 * stays untouched) on the slots it returns.
 *
 * The bug: the squad crouched along a short wall segment (both ends free), three men: K out, the right-end man turned round HIS corner by
 * EndLooks, the left-end man kept his face look (forward-left, 8 o'clock) - inside K's cone - and the sector round the LEFT corner stayed
 * open. EndLooks measures one probe ring 6 m out: from that far the area round the left corner lies inside the forward-left cone, so the
 * greedy saw nothing left to gain and the left man "added nothing". Close to the corner (2-4 m) that area is open and unwatched.
 *
 * The rules:
 * 1. Coverage is measured on TWO probe rings round the members' mean position (NearRingCm and wall_end.probe_dist), every
 *    FormationWallRules::WallEndStepDeg; a ring point is OPEN when some member has a clear line to it (no wall body within
 *    FormationWallRules::WallEndRayRadius) and COVERED when it lies inside the cone (+-SightFov / 2) of a member with a clear line to it.
 * 2. Both ends alike: the end men are the two members farthest apart along the wall line (at least half a WallSpacing apart). An end man
 *    who may move (not K, not a fixed role), does not look round his own corner yet (his look at most 90 deg off the outward normal towards
 *    his own side) and whose corner is OPEN and BESIDE him (a line CornerReachCm along the wall to his side and CornerBackCm behind the wall
 *    line is clear, and at least MinGainPoints ring points behind the wall line on his side, CornerOpenFromDeg or more off the normal, he
 *    has a clear line to - a wall end or a gap; never on a long face, a corner farther along, or at an inner corner) turns round it: to the MIRROR
 *    of the other end man's look when that man looks round his own corner, else to CornerLookDeg off the normal (squad_walls.json
 *    wall_corner_look_deg, default 112 - the look EndLooks gave the right-end man in the report). Not when the look is into a wall next to
 *    him (LookWallDist / LookWallAngle, as EndLooks) or it would hold fewer ring points nobody else holds than his look now. Both end men
 *    round their corners but more than SymmetryToleranceDeg apart (EndLooks' greedy serves one end first): one takes the other's mirror -
 *    the variant holding the most ring points (never fewer than now), on a tie the one nearer CornerLookDeg.
 * 3. No duplicated sector while one is open: while an open ring point is uncovered, the movable member (not K, not fixed, not already
 *    turned by this rule) whose best look (whole degrees, not into a wall) gains the most points nobody else holds over what only he holds
 *    now turns to it - when the gain is at least MinGainPoints; each member turns at most once. Ties: the smaller turn.
 * Positions never change. A face (the half-plane held by the layout's looks) keeps the user's wall rule: nothing is open and uncovered there.
 */

/** Why a member's look was changed by FormationWallSectorRules::Balance. */
enum class EFormationWallSectorFix : uint8
{
	None,
	/** He took the mirror of the other end man's look round the corner (rule 2). */
	Mirror,
	/** He turned round his open corner to CornerLookDeg (rule 2, the other end man not round his yet). */
	Corner,
	/** He turned from a duplicated sector to an open one (rule 3). */
	Dedup
};

/** Balance's answer. */
struct FFormationWallSectorResult
{
	/** Some look changed. */
	bool bChanged = false;
	/** Two-ring coverage of the given looks / of the result, %. */
	double BasePct = 100.0;
	double Pct = 100.0;
	/** Open ring points nobody watches before / after. */
	int32 BaseUncovered = 0;
	int32 Uncovered = 0;
	/** The slots with their looks (positions and order unchanged). */
	TArray<FFormationSlot> Slots;
	/** Per slot: why his look changed. */
	TArray<EFormationWallSectorFix> Fixes;
};

namespace FormationWallSectorRules
{
	/** The near probe ring's radius, cm (the far ring is wall_end.probe_dist). */
	inline constexpr double NearRingCm = 300.0;
	/** Rule 3 turns a member only for at least this many more ring points held (2 deg apart on each ring). */
	inline constexpr int32 MinGainPoints = 3;
	/** Rule 2: the end men must stand at least this share of a WallSpacing apart along the wall. */
	inline constexpr double EndSpreadShare = 0.5;
	/** Rule 2: his corner is beside him - a line this far along the wall to his side and this far behind the wall line is clear, cm. */
	inline constexpr double CornerReachCm = 200.0;
	inline constexpr double CornerBackCm = 150.0;
	/** Rule 2: ring points this far or more off the outward normal (towards his side) lie round his corner, deg. */
	inline constexpr double CornerOpenFromDeg = 105.0;
	/** Rule 2: two end men both round their corners count as alike within this, deg (else one takes the other's mirror). */
	inline constexpr double SymmetryToleranceDeg = 10.0;
	/** Rule 2: the default look round an open corner, off the outward normal, deg (squad_walls.json wall_corner_look_deg). */
	inline constexpr float DefaultCornerLookDeg = 112.f;

	/** Rule 1: the two-ring coverage of a standing group (Open / Covered points, Pct). */
	CODEXTACTICS_API FFormationArcCoverage RingCoverage(const TArray<FFormationWall>& Walls, const TArray<FFormationSlot>& Slots,
		const FFormationConfig& Config = FFormationConfig());

	/**
	 * Diagnostic (tests, smokes): while some open ring point is unwatched, how many movable members (not slot 0, not Fixed) keep a look
	 * whose cone is (mostly) a sector somebody else holds: turning to their best look (not into a wall) would hold at least MinGainPoints
	 * more ring points nobody else holds. 0 when nothing open is unwatched (overlapping cones are no fault then).
	 */
	CODEXTACTICS_API int32 CountRedundantWhileOpen(const TArray<FFormationWall>& Walls, const TArray<FFormationSlot>& Slots, const FFormationConfig& Config = FFormationConfig(),
		const TArray<EFormationRole>& Fixed = TArray<EFormationRole>());

	/**
	 * Rules 2 and 3 (see the header comment). Slots = the wall layout after EndLooks (K first), NormalYaw = its outward normal, Walls = the
	 * walls round the group, Fixed = more roles whose looks a higher rule decided (the corner stack's). Fewer than 3 slots: unchanged (the
	 * pair's looks are a user decision).
	 */
	CODEXTACTICS_API FFormationWallSectorResult Balance(const TArray<FFormationWall>& Walls, const TArray<FFormationSlot>& Slots, float NormalYaw,
		const FFormationConfig& Config = FFormationConfig(), const TArray<EFormationRole>& Fixed = TArray<EFormationRole>(),
		float CornerLookDeg = DefaultCornerLookDeg);
}
