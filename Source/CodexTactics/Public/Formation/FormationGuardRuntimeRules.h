#pragma once

#include "CoreMinimal.h"
#include "Formation/FormationBypassRules.h"
#include "Formation/FormationGuardRules.h"
#include "Formation/FormationTypes.h"

/**
 * Formation rules v2.1 in the runtime - the pure glue between USquadSubsystem and the v2.1 rules (Godot reference: none (UE-only, user
 * decisions 2026-10-25 "the squad never splits" / "B" / "corner: yes" / "5B" / "6A" in docs/design/FORMATION_SECTORS.md, contract
 * docs/design/FORMATION_CONTRACT.md 10; reference simulation Tools/TrainingGround/src/movement/sim.ts, HUD text
 * Tools/TrainingGround/src/movement/labels.ts, the bot's checks Tools/TrainingGround/src/movement/checks.ts)). Pure. Tests:
 * CodexTactics.Formation.GuardRuntime.*.
 *
 * What it covers: the leader's recorded path (the trail column's trail), the leader's pose further along his path (the guard's lookahead),
 * the world's box collision as FFormationWall (what the guard / corner / bypass rules see in a level), the walls a leg is blocked by
 * (bypass), who is exempt from ClearLook, the HUD line, and the bot's check 7 (squad split) / check 9 (corner) geometry the smokes share.
 *
 * Behind Codex.Formation.Sectors (the formation runtime) and Codex.Formation.V21 (default 1; 0 = the v2.2 runtime without the v2.1 guard,
 * trail column, turn fold, bypass, corner watch and ClearLook - an A/B switch).
 */
namespace FormationGuardRuntime
{
	/** Sim TRAIL_STEP: the leader's trail keeps a point every this many cm of his path. */
	inline constexpr float TrailStepCm = 25.f;
	/** Sim TRAIL_KEEP: this much of the leader's path is kept (more than the column's length), cm. */
	inline constexpr float TrailKeepCm = 1500.f;
	/** Sim WALK_LOOK_DEG: the leader's look within this of his travel is looking where he walks (exempt from ClearLook), deg. */
	inline constexpr float WalkLookDeg = 35.f;
	/** A box wall keeps the layout convention (its centre line from end to end) up to this cap past its ends, cm (a 40 cm wall: exact). */
	inline constexpr float LayoutCapCm = 20.f;
	/** A box thicker than twice this is bulky (a building, a container stack): its four sides become four thin walls, cm. */
	inline constexpr float BulkyHalfCm = 150.f;
	/** Sim AGENT_RADIUS (nav.ts): a leg passing closer than this to a wall body is blocked by it (the bypass), cm. */
	inline constexpr float AgentRadiusCm = 35.f;
	/** A leg shorter than this never plans a bypass (the click is next to him), cm. */
	inline constexpr float BypassMinLegCm = 300.f;
	/** The steered leader takes the next bypass waypoint this close to the current one, cm. */
	inline constexpr float BypassHandoverCm = 120.f;
	/** Check 7 (checks.ts LIMITS.splitSeconds): a cut lasting this long is a split, s. */
	inline constexpr float SplitSeconds = 0.3f;
	/** Check 9 (checks.ts LIMITS.cornerPeek): the hidden area's point behind the far face at the wall end, cm. */
	inline constexpr float CornerCheckPeekCm = 200.f;
	/** Check 9 (checks.ts LIMITS.cornerMargin): the corner / hidden point within fov / 2 - this of a member's facing, deg. */
	inline constexpr float CornerCheckMarginDeg = 10.f;

	/** Console Codex.Formation.V21 != 0 (default 1). */
	CODEXTACTICS_API bool IsEnabled();

	/**
	 * Sim trail: appends Pos when it lies at least StepCm from the last point, then drops the oldest points beyond KeepCm of path (the
	 * newest point always stays). An empty trail takes Pos at once.
	 */
	CODEXTACTICS_API void PushTrail(TArray<FVector2D>& Trail, const FVector2D& Pos, float StepCm = TrailStepCm, float KeepCm = TrailKeepCm);

	/** The trail for FormationShapeRules::TrailSlots: Trail plus the leader's position as its last point (unless it already ends there). */
	CODEXTACTICS_API TArray<FVector2D> TrailWithLeader(const TArray<FVector2D>& Trail, const FVector2D& LeaderPos);

	/**
	 * Sim aheadPose: the leader's pose Distance cm further along Path (Path[0] = him now; then his nav path, the bypass waypoints, his
	 * destination); Yaw = the direction of the piece it lies on. A path shorter than Distance ends at its last point (the last piece's
	 * direction); a single point: that point with Heading.
	 */
	CODEXTACTICS_API FFormationPose AheadPose(const TArray<FVector2D>& Path, float Heading, float Distance);

	/**
	 * The leader's nav path ahead of him: Points = his path following's points (start .. goal); the piece he walks on now is the one
	 * closest to LeaderPos (a later one on a tie, the path following's index is not trusted - the crowd agent follows the corridor), so
	 * the result is [LeaderPos, the end of that piece, the points after it]; [LeaderPos] for fewer than two points.
	 */
	CODEXTACTICS_API TArray<FVector2D> PathAheadFrom(const TArray<FVector2D>& Points, const FVector2D& LeaderPos);

	/**
	 * A box of world collision (its horizontal footprint: Center, the two horizontal half axes) as the rules' walls, appended to Out: a
	 * box up to 2 x BulkyHalfCm thick -> ONE wall along its longer axis (the layout convention: the centre line A -> B with the thickness;
	 * a thicker box pulls the ends in so the round caps reach at most LayoutCapCm past the box), a bulkier box -> its four sides as thin
	 * walls (corners are joints). Degenerate boxes add nothing.
	 */
	CODEXTACTICS_API void WallsFromBox(const FVector2D& Center, const FVector2D& HalfAxisA, const FVector2D& HalfAxisB, TArray<FFormationWall>& Out);

	/** Both ends of Walls[Index] are free: no other wall within the thicknesses + FormationCornerRules::CornerJoint of either end. */
	CODEXTACTICS_API bool IsFreeStanding(const TArray<FFormationWall>& Walls, int32 Index);

	/**
	 * Sim planLeg: the first wall along the leg From -> To that blocks it (FormationMath::SegmentBlocked with Radius), measured by its
	 * closest point's projection on the leg; bFreeStandingOnly skips walls that are not IsFreeStanding (a wall of a room / a corridor is
	 * the nav mesh's business). Unset = no such wall.
	 */
	CODEXTACTICS_API TOptional<int32> FirstBlockingWall(const TArray<FFormationWall>& Walls, const FVector2D& From, const FVector2D& To, double Radius,
		bool bFreeStandingOnly);

	/** The points the leader is steered through round the wall: FFormationBypassChoice::Path without its first (him) and last (the destination). */
	CODEXTACTICS_API TArray<FVector2D> BypassWaypoints(const FFormationBypassChoice& Choice);

	/** The bypass waypoint the leader heads for: Index advanced past every waypoint within HandoverCm of Pos (Waypoints.Num() = done). */
	CODEXTACTICS_API int32 AdvanceWaypoint(const TArray<FVector2D>& Waypoints, int32 Index, const FVector2D& Pos, float HandoverCm = BypassHandoverCm);

	/** labels.ts FORMATION_LABEL_EN: the formation's English name; the diamond / wedge of 2 is "Pair". */
	CODEXTACTICS_API FString FormationLabel(EFormationKind Formation, int32 MemberCount);

	/** labels.ts guardHudText: the name in effect, or "Echelon left -> Column (wall)" while the guard falls back (level > 0, a different formation). */
	CODEXTACTICS_API FString GuardHudText(EFormationKind Requested, EFormationKind Effective, EFormationGuardLevel Level, int32 MemberCount);

	/** The contract name of a bypass reason (sim BypassFrame.reason): "shortest", "shielded", "too_long" (logs / smokes; no player text). */
	CODEXTACTICS_API const TCHAR* BypassReasonText(EFormationBypassReason Reason);

	/** "left" / "right". */
	CODEXTACTICS_API const TCHAR* WallSideText(EFormationWallSide Side);

	/**
	 * Sim (v2.1.1, user "6A"): does this member's look go through FormationSectorRules::ClearLook? Not in the wall layout (or its slide),
	 * not running / in the wall mode, not the corner watcher, and not the LEADER walking (LeaderTravelYaw set) with his look within
	 * WalkLookDeg of his travel (he must see his way).
	 */
	CODEXTACTICS_API bool ShouldClearLook(EFormationKind Formation, EFormationMode Mode, bool bCornerWatcher, bool bLeader,
		const TOptional<float>& LeaderTravelYaw, float LookYaw);

	/** One squad member for check 7: the place he holds, where he stands and his slot (the leader: K, his position as both). */
	struct FSplitMember
	{
		EFormationRole Place = EFormationRole::Commander;
		FVector2D Pos = FVector2D::ZeroVector;
		FVector2D Slot = FVector2D::ZeroVector;
	};

	/**
	 * The bot's check 7 (checks.ts): what a wall cuts in the squad right now - outside the trail column the leader's straight line to
	 * each follower's slot ("P's slot") or, else, to the follower ("P"); and the chain neighbours by PLACE (FormationGuardRules::ChainPairs
	 * of Formation, the trail column's: column) on their positions ("P-Z"), in the trail column also on their slots ("P-Z slots").
	 * Members[0] = the leader. Walls crossed with radius 0. Empty = the squad is whole.
	 */
	CODEXTACTICS_API TArray<FString> SplitCuts(const TArray<FFormationWall>& Walls, EFormationKind Formation, bool bTrail, int32 MemberCount,
		const TArray<FSplitMember>& Members);

	/**
	 * The bot's check 9 geometry (checks.ts): the end End (0 = A, 1 = B) of Wall seen from the leader's side - G = the graze point
	 * CornerGraze past the body at the end, H = the hidden point CornerCheckPeekCm behind the wall's far face from G.
	 */
	CODEXTACTICS_API void CornerCheckPoints(const FFormationWall& Wall, int32 End, const FVector2D& LeaderPos, FVector2D& OutGraze, FVector2D& OutHidden);
}
