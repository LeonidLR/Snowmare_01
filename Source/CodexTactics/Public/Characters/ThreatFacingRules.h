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
	 * Which point: the last combat target while it lives (bLastTargetAlive), else the nearest live enemy (bHasNearest), else none.
	 * Returns false when there is nothing to face.
	 */
	CODEXTACTICS_API bool PickThreatPoint(bool bLastTargetAlive, const FVector& LastTargetLocation, bool bHasNearest, const FVector& NearestLocation,
		FVector& OutPoint);
}
