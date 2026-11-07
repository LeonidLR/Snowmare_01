#pragma once

#include "CoreMinimal.h"

/**
 * Under fire (bug #43, user PIE on Depot 2026-11-07, UE-only; Godot had no shooting enemies of this kind): "the squad in a column
 * in a narrow passage is fired at by a PMC from a corner at the passage end; the squad (AGGRESSIVE) does not react until it is out
 * of the passage". The shooter WAS known to the squad (his muzzle flash demasks him within earshot, UPmcSquadSubsystem::UpdateSight),
 * but his body stood behind the corner: no clear line to it, so no combat target - no turn (a moving man faced his path) and no
 * shot back.
 *
 * Every round fired at an operative by a hostile operative now puts the shooter's muzzle flash point on the target and on every mate
 * of his team within ShareRadiusCm of him (they hear the rounds come in) for MemorySeconds:
 *  - the target's team learns the shooter (UPmcSquadSubsystem::NotifyShotAt: seen for SightRules::DemaskSeconds whatever the range);
 *  - each of them without a combat target turns to the flash - standing or walking (the 8-direction locomotion strafes), not in
 *    cover, not sprinting / carrying / panicking, never against a facing the player ordered, never on the turn-based grid;
 *  - with no line to the shooter's body he returns fire at the flash itself (the part of the shooter round the corner) - a
 *    suppressive, blind-factor round (SightRules::BlindFireAccuracyMultiplier, a hit only while the shooter stays at the spot),
 *    when his fire posture lets him open fire on his own and a line from his muzzle to the flash is clear (a rear man of the
 *    column without one only turns).
 */
struct FUnderFireConfig
{
	/** How long a round at him keeps the flash point as his threat, s. */
	float MemorySeconds = 3.f;
	/** Mates within this distance of the target share the threat, cm. */
	float ShareRadiusCm = 2500.f;
};

namespace UnderFireRules
{
	CODEXTACTICS_API const FUnderFireConfig& GetConfig();

	/** The flash point is still his threat. */
	CODEXTACTICS_API bool IsThreatActive(double Now, double LastShotAtTime, float MemorySeconds);

	/** A mate at this distance from the target shares the threat (the target himself always does). */
	CODEXTACTICS_API bool SharesThreat(bool bIsTarget, float DistanceToTargetCm, float ShareRadiusCm);

	struct FFaceFacts
	{
		bool bRealTimeFight = false;
		bool bAlive = false;
		bool bThreatActive = false;
		/** A combat target already turns him. */
		bool bHasCombatTarget = false;
		bool bInCover = false;
		bool bSprinting = false;
		bool bCarrying = false;
		bool bPanicking = false;
		/** A quick fire / SUPPRESS order turns him itself. */
		bool bFireOrder = false;
		/** A facing the player ordered (facing point / order facing). */
		bool bExplicitFacing = false;
		bool bGridOwnsFacing = false;
	};

	/** He turns to the flash point now (moving or not). */
	CODEXTACTICS_API bool ShouldFaceThreat(const FFaceFacts& Facts);

	/** He returns fire at the flash: no visible target to shoot, the posture allows his own fire, a clear line to the flash, in reach. */
	CODEXTACTICS_API bool ShouldReturnFire(bool bThreatActive, bool bHasDirectTarget, bool bMayAutoFire, bool bLineToFlash, bool bInReach);
}
