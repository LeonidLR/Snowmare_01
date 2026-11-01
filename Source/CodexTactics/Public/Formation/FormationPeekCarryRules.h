#pragma once

#include "CoreMinimal.h"
#include "Tactics/CoverTypes.h"

/**
 * The squad's corner peek moves on to the NEXT wall (the game designer's play-test 2026-11-01 on L_FormationLabCombat, bug_04 A; behind
 * Codex.Formation.Sectors + Codex.Formation.PeekCarry). Godot reference: none (UE-only; docs/systems/squad-and-orders.md "The squad's corner
 * peek moves on to the next wall").
 *
 * The bug: the squad peeking round the end of one wall (USquadSubsystem::OrderFormationCornerPeek), the player double-clicked the end of the
 * PARALLEL wall ("corner to fire from") to peek round ITS corner. The click is a group cover order = the formation wall layout
 * (USquadSubsystem::OrderFormationWall): the peek ended as "the leader's next order", the old peeker stayed in his corner cover (the
 * followers-in-cover rule only lets a man out when his slot is far and the leader walks; on the wall route his trail place lay beside him),
 * the other two crouched at the new wall in its wall END layout - no peek, a broken shape.
 * Now (1) the wall order takes every member out of his cover / corner first, (2) a wall order given while the squad peeks at a slot with a
 * free corner carries the peek over: once the squad is at the new wall, the squad's corner peek at that corner is ordered (the same order
 * as the player's click on that wall's end). Pure; tests CodexTactics.Formation.PeekCarry.*.
 */
namespace FormationPeekCarry
{
	/** The aim point the carried peek "clicks": this far in from the corner along the face (inside the end window of a peek click), cm. */
	inline constexpr float AimInsetCm = 25.f;
	/** The carried peek is ordered once the leader has stood still this long and everyone is at the wall, s. */
	inline constexpr float LeaderStillSeconds = 0.4f;
	/** ... or, with a straggler still on his way, once the leader has stood still this long, s. */
	inline constexpr float StragglerSeconds = 8.f;
	/** Never later than this after the order (no path, a blocked member): the carry is dropped, the squad holds the wall layout, s. */
	inline constexpr float MaxSeconds = 40.f;
	/** The leader's stillness counts once he has walked since the order, or this long after it (his slot where he stood), s. */
	inline constexpr float StartGraceSeconds = 1.5f;

	/** What the group cover order looked like when it was given. */
	struct FCarryOrderFacts
	{
		/** The squad's corner peek was in force (USquadSubsystem::GetFormationPeeker). */
		bool bPeekInForce = false;
		/** The clicked cover slot has a free corner within its probe range (the HUD's "corner to fire from"). */
		bool bSlotAtCorner = false;
		/** The order became the formation wall layout (group mode, not a box selection / switch / wrap). */
		bool bWallLayoutOrder = false;
		/** Planned in the tactical pause (the layout is ordered on the release; the carry is not). */
		bool bPlanned = false;
	};

	/** The wall order carries the squad's corner peek over to the clicked corner. */
	CODEXTACTICS_API bool ShouldCarry(const FCarryOrderFacts& Facts);

	/**
	 * The clicked slot's corner to peek round: the nearer exposed edge (a slot reports the distance along its wall to each free corner).
	 * OutSide = that edge's side (his left / right facing out), OutAlongCm = its signed distance along the slot's RightTangent (right +,
	 * left -). False when neither edge is exposed.
	 */
	CODEXTACTICS_API bool PickCorner(bool bLeftExposed, float LeftCm, bool bRightExposed, float RightCm, ECoverFacing& OutSide, float& OutAlongCm);

	/** Where the carried peek "clicks": AimInsetCm in from the corner along the face (EdgeAlongCm from the slot, signed as PickCorner). */
	CODEXTACTICS_API float AimAlong(float EdgeAlongCm);

	/** One frame of the carry. */
	struct FCarryProgressFacts
	{
		/** The leader is the one who gave the order. */
		bool bSameLeader = true;
		/** No newer order of the leader's since (his move order serial unchanged). */
		bool bLeaderOrderUnchanged = true;
		/** The squad's wall order is still in force. */
		bool bWallOrder = true;
		/** Everyone is at his wall slot (the covered approach is over and every member stands settled at his place). */
		bool bApproachDone = false;
		/** The leader has walked since the order (a stand-up clip first keeps him still: not yet "arrived"). */
		bool bLeaderMoved = false;
		/** How long the leader has stood still, s. */
		float LeaderStillSeconds = 0.f;
		/** Since the order, s. */
		float Seconds = 0.f;
	};

	enum class EPeekCarryStep : uint8
	{
		/** The squad is still on its way to the new wall. */
		Wait,
		/** At the wall: order the squad's corner peek at the carried corner now. */
		Fire,
		/** Another order / leader / no wall order any more / too long: no carried peek. */
		Drop,
	};

	CODEXTACTICS_API EPeekCarryStep Decide(const FCarryProgressFacts& Facts);

	CODEXTACTICS_API const TCHAR* StepName(EPeekCarryStep Step);
}
