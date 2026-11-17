#pragma once

#include "CoreMinimal.h"

/**
 * After a retreat, face the threat again (user request 2026-10-22, UE-only; Godot had no rule): "two operatives fire at enemies
 * pushing them; I order them to retreat at a run - they turn and run, correct. But then they stand with their backs to the enemies
 * running after them. After the run the operative must turn toward the last position of the enemy he was fighting."
 *
 * In a real-time fight, an operative who stands (his move / sprint is over), has no combat target in his sights (the target fell
 * out of range / sight while he ran) and was given no facing of his own turns - at his normal stance turn rate - to his last
 * combat target (its current spot while it lives), else to the nearest live enemy within ThreatMemoryRangeCm; no enemy left -
 * no turn. The fire posture then picks targets as usual.
 * Never: in cover (the back stays to the wall), with an explicit facing (a group cover / warm spot facing, a facing order, a
 * quick fire), on the turn-based grid, in exploration (no fight).
 */
namespace ThreatFacingRules
{
	/** Enemies farther than this are no threat to turn to, cm. */
	constexpr float ThreatMemoryRangeCm = 3000.f;

	struct FFacts
	{
		/** WaveCombat in real time. */
		bool bRealTimeFight = false;
		/** Stands still: no running move order, almost no velocity. */
		bool bStanding = false;
		bool bInCover = false;
		/** A live combat target already turns him (UpdateCombatFacing). */
		bool bFacingCombatTarget = false;
		/** A facing of his own: idle facing set by an order, a facing order, a quick fire. */
		bool bExplicitFacing = false;
		bool bGridOwnsFacing = false;
		/** He fought someone in this fight (a remembered combat target). */
		bool bFoughtThisFight = false;
		/** A threat to face (the last target alive, or a live enemy within range). */
		bool bHasThreat = false;
	};

	/** He turns to the threat now. */
	CODEXTACTICS_API bool ShouldTurnToThreat(const FFacts& Facts);

	/**
	 * A facing order of the player (SetFacingPoint) against a combat target (074, 2026-11-16): a target within his weapon's reach still turns
	 * him (bug #39 2026-11-07: "a target in reach wins over a facing order"); one beyond the reach does not - the order holds. Without an order
	 * of his own any live target turns him (unchanged). A stale target (kept while he reloads, not yet out of the squad's sight) that has left
	 * the reach turned the ordered body 65 deg in two frames, and nothing turns it back (RetreatFacingSmoke 2026-11-16).
	 */
	CODEXTACTICS_API bool MayCombatTargetTurnHim(bool bHasOwnFacing, float TargetDistanceCm, float WeaponReachCm);

	/**
	 * Which point: the last combat target while it lives (bLastTargetAlive), else the nearest live enemy (bHasNearest), else none.
	 * Returns false when there is nothing to face.
	 */
	CODEXTACTICS_API bool PickThreatPoint(bool bLastTargetAlive, const FVector& LastTargetLocation, bool bHasNearest, const FVector& NearestLocation,
		FVector& OutPoint);

	/**
	 * After the fight he returns to the player's facing order (game designer's decision 2026-11-17, option A): the order stays in force
	 * until the player cancels it (a move clears it). A combat target in reach / the under-fire turn wins over it as before; once nothing
	 * has turned him to fight for this long he turns back - at his stance turn rate - to the ordered facing. A new enemy within this time
	 * turns him again and restarts the wait.
	 */
	constexpr float ReturnToOrderedFacingDelaySeconds = 3.f;

	/**
	 * He turns back to the ordered facing now: a facing order of the player, standing, not in cover, nothing turns him to fight this frame
	 * (combat target / flash / object shot / fire order), and that has lasted ReturnToOrderedFacingDelaySeconds.
	 */
	CODEXTACTICS_API bool ShouldReturnToOrderedFacing(bool bHasFacingOrder, bool bStanding, bool bInCover, bool bTurnedToFight, float QuietSeconds);
}
