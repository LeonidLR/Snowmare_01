#pragma once

#include "CoreMinimal.h"

/**
 * R17-1 (designer 2026-11-17, TASK_BOARD): the squad climbs onto / over a mantle obstacle the way it takes a ladder - one climbs, the others
 * wait at the base and cover; he is up, the next one climbs; the first man up covers from the top (docs/design/MANTLING.md 6.4,
 * docs/design/FORMATION_CONTRACT.md 22). The queue itself is the ladder's (TTraversalQueue); these are the pure geometry / look rules of
 * USquadSubsystem's squad climb (SquadSubsystemMantleQueue.cpp). UE-only (Godot had no mantling). Tests: CodexTactics.Formation.MantleQueue.*.
 */
namespace SquadMantleRules
{
	/**
	 * The climbers' lanes along the face, as offsets from the first climber's lane (+ = along the face's tangent): 0, +Spacing, -Spacing,
	 * +2 Spacing, -2 Spacing, ... kept inside [MinOffsetCm, MaxOffsetCm] (the room the face has on either side of the first lane, its ends
	 * kept clear). At most Count lanes; fewer when the face is too short (the rest have no lane: they hold at the base).
	 */
	CODEXTACTICS_API TArray<float> LaneOffsets(int32 Count, float SpacingCm, float MinOffsetCm, float MaxOffsetCm);

	/**
	 * The climb order: the leader first, then the others by their formation slot (lower first); members without a slot (INDEX_NONE) after the
	 * slotted ones in the order given. Returns indices into Slots (LeaderIndex first).
	 */
	CODEXTACTICS_API TArray<int32> ClimbOrder(int32 LeaderIndex, const TArray<int32>& Slots);

	/** A known threat lies on the open side of the base (within 90 deg of the outward look): the waiters can see towards it. */
	CODEXTACTICS_API bool IsThreatOnOpenSide(float OutwardYaw, float ThreatYaw);

	/**
	 * A waiter's look (world yaw) by his place in the line: away from the obstacle (OutwardYaw), the threat side first - place 0 looks at a
	 * known threat on the open side, else straight out; place 1 +FanDeg, place 2 -FanDeg, place 3 +2 FanDeg, ... from that look, never more
	 * than MaxOffDeg off the outward look (nobody covers into the obstacle).
	 */
	CODEXTACTICS_API float WaitLookYaw(float OutwardYaw, int32 Place, float FanDeg, float MaxOffDeg, const TOptional<float>& ThreatYaw);

	/**
	 * The look of a man who is up (on the top / over the fence) by the order he came up: the first one covers the climb's direction (the far
	 * side nobody at the base can see) - or a known threat; the next ones +90 / -90 / back over the base, then the diagonals.
	 */
	CODEXTACTICS_API float UpLookYaw(float ClimbYaw, int32 UpIndex, const TOptional<float>& ThreatYaw);

	/**
	 * The lane candidates per face (MakeSquadMantleLanes, at half squad.lane_spacing_cm) for LanesPerFace lanes a full spacing apart: 0, +-1/2,
	 * +-1 ... spacings, i.e. 2 x LanesPerFace + 1 (LanesPerFace half-steps each side: at least LanesPerFace full-spacing lanes; the half-step neighbours let an uneven top refusing one lane take the next; a neighbour of a
	 * taken lane is dropped by the cheap landing-gap test before any trace / path query, so the costly checks stay at ~LanesPerFace a face).
	 */
	CODEXTACTICS_API int32 LaneCandidateCount(int32 LanesPerFace);

	/** A turn is timed out (squad.turn_timeout_s) only while its man is not in the clip: one climber at a time, never two (review R17-1 #1). */
	CODEXTACTICS_API bool IsTurnOverTime(bool bInClip, float TurnSeconds, float TimeoutSeconds);

	/**
	 * A refused / stalled turn is asked again (Retries > 0) only RetryWaitSeconds after it was re-armed (SinceOrderSeconds counts from the re-arm,
	 * not from the turn's start - review R17-1 #5); the first ask (Retries 0) goes at once.
	 */
	CODEXTACTICS_API bool IsRetryDue(int32 Retries, float SinceOrderSeconds, float RetryWaitSeconds);

	/**
	 * A waiter's spot place: in line (Waiting) his place in the ladder's line (it moves up as the line does); holding at the base (Hold) a fixed
	 * place, his member index (never re-picked while the line moves, so no new path query a tick - review R17-1 #4; a holder stands on the
	 * leader's lane or his own, which nobody waits on). INDEX_NONE when he is neither / not in the line.
	 */
	CODEXTACTICS_API int32 WaitSpotPlace(bool bWaiting, bool bHolding, int32 LineIndex, int32 MemberIndex);

	/** What EndSquadMantle knows of a member when a climb ends (ShouldWalkOn). */
	struct FWalkOnCheck
	{
		/** The end walks the path climb's men on (CancelSquadMantle - a smoke's reset - does not). */
		bool bResumePath = true;
		/** A path climb (a mantle link on an order's path), not a clicked one. */
		bool bFromPath = false;
		/** The turn-based fight owns the squad. */
		bool bTurnBased = false;
		/** The climb interrupted a walk of his own (the leader, a box member with his own order). */
		bool bResume = false;
		bool bAlive = false;
		/** No other order was given to him since the climb began (his order serial unchanged). */
		bool bSameOrder = false;
		/** From his feet to his walk's goal (2D), cm. */
		float DistToGoalCm = 0.f;
	};

	/**
	 * A path climb's man walks on to his own goal when the climb ends, whatever ended it (all over, out, nobody left, a mode switch, the
	 * console - review R17-1 #2: the leader is never stranded): alive, his walk interrupted by it, no new order since, not there yet, not on the
	 * turn-based grid. Mid-clip too: the order is taken after the landing (AOperativeCharacter::RequestMove).
	 */
	CODEXTACTICS_API bool ShouldWalkOn(const FWalkOnCheck& Check);
}
