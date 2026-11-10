#pragma once

#include "CoreMinimal.h"

/**
 * The prone crawl under fire (the game designer's CrawlAndShoot_Bug_01, 2026-11-09; UE-only, Godot had no directional crawl): "An operative
 * lying prone under fire, ordered to crawl forward, tries to do two things at once: crawl AND return fire. That does not happen. When he is told
 * to crawl away, he must crawl first. He keeps his body facing the danger but moves in the direction we ordered."
 *
 * - The ORDERED CRAWL: prone, alive, out of cover, moving on an order - his own move order (the player's click, the under-fire manoeuvre's step,
 *   the autonomy / the PMC commander) or a formation move he is actually crawling on (the player's order to the squad). While it lasts he holds
 *   his fire: no round, no grenade, no new reload (a fire / reload clip is a full-body clip prone: it froze the crawl), the combat target and the
 *   threat memory are kept; the fire resumes on arrival / a new order (the order ends). An explicit fire order (quick fire, SUPPRESS) stops him
 *   first, so it is never held; the player's attack order to a prone man who lies still fires as before.
 * - The FACING while crawling: the body (capsule + pelvis) faces the danger and he moves in the ordered direction (the directional crawl clips
 *   strafe): the combat target, else the muzzle flash he is under fire from, else (ordered crawl only) the threat he fought (its live spot), else
 *   the movement as before.
 * Used by AOperativeCharacter::RunCombatShooting / UpdateCombatFacing (OperativeCharacterCombat.cpp / OperativeCharacterAim.cpp) and the native
 * prone crawl layer of UOperativeAnimInstance (OperativeAnimInstanceProneCrawl.cpp). Tests CodexTactics.Characters.ProneCrawl.*;
 * smoke CodexTactics.ProneCrawlSmoke. Console A/B: Codex.Combat.ProneCrawlHoldFire (0 = the old rule: he fires while crawling).
 */
namespace ProneCrawlRules
{
	/** A formation move counts as crawling only above this speed (a follower parked in his slot fires as before), cm/s. */
	constexpr float FormationCrawlMinSpeedCm = 10.f;

	struct FCrawlFacts
	{
		bool bAlive = true;
		bool bProne = false;
		bool bInCover = false;
		/** A move order of his own is running (HasOwnMoveOrder: the player's, the manoeuvre's step, the autonomy's, the commander's). */
		bool bOwnMoveOrder = false;
		/** A formation move (FollowTo) is running. */
		bool bFormationMove = false;
		/** His horizontal speed, cm/s. */
		float Speed2D = 0.f;
	};

	/** He is crawling on an order now (the fire hold and the threat facing below). */
	CODEXTACTICS_API bool IsOrderedCrawl(const FCrawlFacts& Facts);

	/** He holds his fire this frame (bHoldEnabled = Codex.Combat.ProneCrawlHoldFire). */
	CODEXTACTICS_API bool HoldsFire(const FCrawlFacts& Facts, bool bHoldEnabled);

	/** What the crawling body faces. */
	enum class ECrawlFacing : uint8
	{
		/** The movement direction (nothing to face: as before). */
		Movement,
		/** The live combat target. */
		CombatTarget,
		/** The muzzle flash he is under fire from (no combat target). */
		FireFlash,
		/** The threat he fought (its live spot / the nearest live enemy) - ordered crawl only. */
		RememberedThreat
	};

	/** The facing priority: target > flash > remembered threat (ordered crawl only) > movement. */
	CODEXTACTICS_API ECrawlFacing ChooseFacing(bool bOrderedCrawl, bool bHasCombatTarget, bool bUnderFire, bool bHasRememberedThreat);

	/**
	 * The direction of the move relative to the body, deg clockwise from the body's forward (90 = to his right, +-180 = backwards): what the
	 * directional crawl clips play. BodyYaw / MoveYaw in world degrees.
	 */
	CODEXTACTICS_API float RelativeMoveDeg(float BodyYawDeg, float MoveYawDeg);

	/**
	 * The crawl layer's weight target: eligible (prone, alive, up, out of cover, not on a ladder / carrying / in the sniper pose) and either
	 * crawling on an order or moving faster than StartSpeed (a slower body lies in the ABP's prone pose - the rifle idle / aim / fire).
	 */
	CODEXTACTICS_API float LayerTarget(bool bEligible, bool bOrderedCrawl, float LocomotionSpeed, float StartSpeed);

	/**
	 * The shared crawl phase's rate (cycles a second) for a body crawling at Speed: Speed over the blended cycle distance - the current
	 * direction's (CycleCm, the clips' natural speed x length) cross-faded with the previous direction's (FromCycleCm) by FromShare, so the clips
	 * that still show keep their hands / knees planted during a reversal's cross-fade - clamped to [0, MaxRate / Length].
	 */
	CODEXTACTICS_API float PhaseRate(float Speed, float CycleCm, float FromCycleCm, float FromShare, float MaxPlayRate, float LengthSeconds);
}
