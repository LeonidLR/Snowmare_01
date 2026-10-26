#pragma once

#include "CoreMinimal.h"
#include "Formation/FormationTypes.h"

/**
 * Wall layout of a squad in formation mode (group cover order) (Godot reference: none (UE-only, design
 * docs/design/FORMATION_SECTORS.md "Wall", contract docs/design/FORMATION_CONTRACT.md 2; reference implementation
 * Tools/TrainingGround/src/movement/rules.ts Layout / wallRoom)). Pure. Nobody looks through the wall: all stand along it
 * WallSpacing apart, facing the OPEN side, which they share out. Tests: CodexTactics.Formation.Wall.*, parity
 * CodexTactics.Formation.Parity.WallLayout.
 *
 * Frame: C = the point of the wall segment nearest to the clicked slot; the outward normal N = ClickedSlot - C (the slot on the
 * wall line: the left-hand normal of A -> B); K stands at C + N * (Thickness / 2 + WallFaceOffset); "right" = the right hand of a
 * man facing the wall (yaw N + 270); a step = WallSpacing along it. Look yaws relative to N, positive = right (UE yaw):
 * - 1: K looks N (6 o'clock of the heading into the wall: away from it).
 * - 2: the partner Z on the roomier side (RoomRight >= RoomLeft: right); right: K N + 45 (7:30), Z one step right N - 45 (4:30);
 *   left: K N - 45, Z one step left N + 45.
 * - 3: K N (6), P one step right N - WallFlankDeg (4), the triad's Z (v2.2; L before) one step left N + WallFlankDeg (8).
 * - 4: the 4th (Z) at the end with more room (RoomRight >= RoomLeft: right - a tie goes RIGHT; the caller breaks a travel tie with
 *   BreakRoomTie). Right: Z two steps right N - WallEndDeg (3-4), his neighbour P N - WallNeighbourDeg (5), L N + WallFlankDeg;
 *   left: Z two steps left N + WallEndDeg (8-9), L N + WallNeighbourDeg (7), P N - WallFlankDeg. K N.
 * Slots past a wall end are not refused here (the runtime's group cover rules handle a short wall).
 *
 * Slide (v2.2, user approved on an animation 2026-10-25): the squad stands in the wall layout and the leader's next destination lies at
 * the SAME wall - within SlideMaxOffWall of its surface, on the squad's side, its projection inside the wall, at least SlideMin along it.
 * Then nobody re-forms: everyone moves sideways together at the leader's pace - each target is his start slot moved along the wall by
 * the leader's progress (along the wall only), so the order and the wall offset are kept and nobody passes anybody. Looks: the leading
 * end along the wall in the travel direction turned SlideLeadOutDeg outward, the trailing end back along the wall turned as far
 * outward (the rear guard), K (when not an end) away from the wall with his search (+-ScanAmpLead), the members in between
 * FormationCoverRules::AllocateStatic (the ends and K fixed). Not active -> the targets are the start slots (the caller forms the normal
 * formation). On arrival the caller forms the normal wall layout again.
 *
 * EndLooks (v2.4, user bug report 2026-10-26: "at the end face of a wall big uncovered zones form on the sides of the end - stabilise it
 * so they cover the large zones"; contract 14; reference rules.ts EndLooks / openArcCoverage): Layout's looks are the user's rule for a
 * wall FACE (the open half-plane shared). At a wall END (the end face of a thin free-standing wall, or a man standing past a wall's end)
 * the open arc reaches round the end, back along both faces, and those looks leave both flanks open. OpenArcCoverage = of a probe ring
 * (WallEndProbeDist round the members' mean position, every WallEndStepDeg from -180) the points some member has a clear line to (no
 * wall body within WallEndRayRadius), and of those the share inside the cone (+-SightFov / 2) of a member with a clear line. EndLooks:
 * with 3+ slots and that share below WallEndCoverPct the looks are spread - K keeps his (away from the wall); the others greedily: of the
 * remaining members and their looks (whole degrees -179..180, none within LookWallAngle of the closest point of a wall whose surface is
 * within LookWallDist of his slot) the one holding the most open ring points he sees and nobody holds yet (ties: nearest his layout
 * look, then the lower degree; between members the earlier slot); a member who would add nothing keeps his layout look; a spread that
 * holds less than the base keeps the base. Positions and order never change.
 */

/** OpenArcCoverage's answer (v2.4). */
struct FFormationArcCoverage
{
	/** Probe ring points some member has a clear line to. */
	int32 Open = 0;
	/** Of those, the points inside the cone of a member with a clear line to them. */
	int32 Covered = 0;
	/** 100 x Covered / Open (100 with nothing open), %. */
	double Pct = 100.0;
};

/** EndLooks' answer (v2.4). */
struct FFormationWallEndResult
{
	/** The layout's looks held less than WallEndCoverPct of the open arc (a wall end) and were spread. */
	bool bSpread = false;
	/** OpenArcCoverage of the given looks / of the result (the slots, WallEndProbeDist, SightFov), %. */
	double BasePct = 100.0;
	double Pct = 100.0;
	/** The slots with their looks (positions and order unchanged). */
	TArray<FFormationSlot> Slots;
};

/** Slide's answer. */
struct FFormationSlideResult
{
	/** The destination lies at the same wall, on the squad's side, within SlideMaxOffWall, along it by >= SlideMin. */
	bool bActive = false;
	/** Where the leader's move along the wall ends (his start slot moved along the wall; the wall offset kept). */
	FVector2D LeaderTarget = FVector2D::ZeroVector;
	/** World targets (the start layout moved by the leader's progress) and look yaws, in StartSlots' order. */
	TArray<FFormationSlot> Targets;
	/** The leading end (looks ahead along the wall); unset when not active. */
	TOptional<EFormationRole> Lead;
	/** The trailing end (looks back; the rear guard); unset when not active or alone. */
	TOptional<EFormationRole> RearGuard;
	/** Yaw of the wall's outward normal (from the wall to the squad). */
	float NormalYaw = 0.f;
	/** Places from the leading end to the trailing end (the order the slide keeps). */
	TArray<EFormationRole> Order;
};

namespace FormationWallRules
{
	/** cm: a destination closer than this along the wall is not a slide (the layout just stays). */
	inline constexpr double SlideMin = 50.0;

	/**
	 * v2.2 wall slide (see the header comment). StartSlots = the wall layout as Layout placed it (K first), LeaderPos = the leader now,
	 * Destination = his next destination, Time = the patrol time (K's search).
	 */
	CODEXTACTICS_API FFormationSlideResult Slide(const FFormationWall& Wall, const TArray<FFormationSlot>& StartSlots, const FVector2D& LeaderPos,
		const FVector2D& Destination, float Time, const FFormationConfig& Config = FFormationConfig());

	/** The pair's look offsets from the outward normal (7:30 / 4:30), deg. */
	inline constexpr float PairLookDeg = 45.f;

	/** The layout: slots in slot order (K, Z / K, P, Z / K, P, L, Z), Yaw = the look yaw. */
	CODEXTACTICS_API TArray<FFormationSlot> Layout(const FFormationWall& Wall, const FVector2D& ClickedSlot, int32 MemberCount, float RoomLeft,
		float RoomRight, const FFormationConfig& Config = FFormationConfig());

	/** The outward normal's yaw Layout uses (ClickedSlot - the nearest wall point; on the line: the left-hand normal of A -> B). */
	CODEXTACTICS_API float OutwardYaw(const FFormationWall& Wall, const FVector2D& ClickedSlot);

	/** Free wall length beyond the nearest wall point to ClickedSlot on each side (left / right seen facing the wall), cm. */
	CODEXTACTICS_API void WallRoom(const FFormationWall& Wall, const FVector2D& ClickedSlot, float& OutRoomLeft, float& OutRoomRight);

	/**
	 * Design "tie -> the side he reaches with less travel": when RoomLeft == RoomRight and the 4th man's travel to the left end is
	 * shorter, nudges RoomLeft up by 1 cm so Layout puts him left (Layout itself sends a tie right).
	 */
	CODEXTACTICS_API void BreakRoomTie(float& RoomLeft, float& RoomRight, float TravelLeft, float TravelRight);

	/** v2.4: the probe ring is sampled this often, deg (rules.ts WALL_END_STEP). */
	inline constexpr double WallEndStepDeg = 2.0;
	/** v2.4: a probe line passing this close to a wall body is blocked (a zero-thickness traced face blocks too), cm (WALL_END_RAY_RADIUS). */
	inline constexpr double WallEndRayRadius = 1.0;

	/** v2.4 (bot check 13, the smokes): how much of the open arc round a standing group its cones hold (see the header comment). */
	CODEXTACTICS_API FFormationArcCoverage OpenArcCoverage(const TArray<FFormationWall>& Walls, const TArray<FVector2D>& Positions,
		const TArray<float>& LookYaws, float FovDeg, float ProbeCm);

	/** v2.4: the wall layout's looks at a wall end (see the header comment). Slots = Layout's (K first), Walls = the walls round them. */
	CODEXTACTICS_API FFormationWallEndResult EndLooks(const TArray<FFormationWall>& Walls, const TArray<FFormationSlot>& Slots,
		const FFormationConfig& Config = FFormationConfig());
}
