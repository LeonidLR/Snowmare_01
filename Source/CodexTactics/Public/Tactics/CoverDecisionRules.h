#pragma once

#include "CoreMinimal.h"
#include "Characters/OperativeMovementRules.h"
#include "Tactics/CoverTypes.h"

/**
 * Tactical decisions of an operative in cover (Sprint 12-F; UE-only, no Godot reference — Gemini Sprint 12 spec),
 * calibrated against TypeSafe Jev with Scripts/Tools/jev_validate_cover.py (2026-10-06: 21 / 24 scenarios, 88 %;
 * the three disagreements are at Jev confidence <= 0.23) and frozen here as deterministic pure rules. Used by
 * USquadAutonomySubsystem for AI-controlled operatives (Commander Mode); the player's own operative follows the
 * player's fire-mode choice. Tested in CodexTactics.Tactics.Cover.DecisionPeekVsBlindFire / LaserForcesCrouch.
 */

/** What the operative does with his rifle at the wall. */
enum class ECoverFireDecision : uint8
{
	/** Stay behind the cover, no shot. */
	Hold,
	/** Lean out of the corner (rise over a low cover) and fire aimed. */
	CornerPeek,
	/** Fire round the corner / over the top without showing the head (-40 %). */
	BlindFire,
	/**
	 * The enemy stands out on the open side (in front of the wall, CoverFacingRules::ShouldCornerShot false): no corner
	 * shot — he steps off the wall for a normal aimed shot and comes back (user decision 2026-10-06).
	 */
	OpenShot
};

enum class ECoverStanceDecision : uint8
{
	Stand,
	Crouch
};

/** Inputs of the fire decision. */
struct CODEXTACTICS_API FCoverFireSituation
{
	ECoverHeight Height = ECoverHeight::HighCover;
	/** A free corner towards the enemy (a low cover is always fired over: true). */
	bool bEdgeExposed = true;
	/** A marksman's laser rests on him (AMarksmanEnemyCharacter::IsAimingAtTarget). */
	bool bSniperLaserOnMe = false;
	float HealthFraction = 1.f;
	/** 0..1: how many enemies fire at him right now (0 none, ~0.3 one now and then, >= 0.5 several). */
	float SuppressionPressure = 0.f;
	/** Damage he took in the last few seconds (HP). */
	float RecentIncomingDamage = 0.f;
	float DistanceToEnemyCm = 1000.f;
	/** The enemy is out on the open side, in front of the wall (not behind it / around the corner). */
	bool bEnemyInFrontOfCover = false;
};

/** Inputs of the stance decision. */
struct CODEXTACTICS_API FCoverStanceSituation
{
	ECoverHeight Height = ECoverHeight::HighCover;
	bool bSniperLaserOnMe = false;
	float HealthFraction = 1.f;
	float SuppressionPressure = 0.f;
	/** An enemy shoots from above (it sees over the wall). */
	bool bEnemyElevated = false;
	/** He is about to fire aimed (rise over a low cover for it). */
	bool bWantsAimedFire = false;
};

/** Thresholds (the Jev-calibrated defaults; mirrored in jev_validate_cover.py — keep in step). */
struct CODEXTACTICS_API FCoverDecisionConfig
{
	/** Below this health share he is «badly wounded»: never peeks, blind fire only at point-blank range, else holds. */
	float PeekMinHealthFraction = 0.35f;
	/** Danger at or above this: blind fire instead of a peek. */
	float BlindFireDangerThreshold = 0.6f;
	/** Enemy beyond blind-fire reach and this much danger: hold. */
	float HoldDangerFar = 0.85f;
	/** Point-blank range, cm: blind fire already at CloseRangeDanger. */
	float CloseRangeCm = 600.f;
	float CloseRangeDanger = 0.3f;
	/** Beyond this blind fire is futile: peek (or hold), cm. */
	float BlindFireMaxRangeCm = 2500.f;
	/** Recent damage that counts as «heavy» (danger 0.5 from damage alone), HP. */
	float DamageNormaliser = 40.f;
	/** Danger added below PeekMinHealthFraction. */
	float LowHealthDangerBonus = 0.3f;
	/** Low cover: stands up to fire aimed only under this pressure. */
	float StandMaxSuppressionLow = 0.3f;
	/** High cover: crouches from this pressure on. */
	float CrouchSuppressionHigh = 0.5f;
	/** Suppression pressure per enemy currently targeting him. */
	float PressurePerShooter = 0.35f;
	/** Recent damage memory, s (the damage decays linearly over it). */
	float RecentDamageSeconds = 5.f;

	// --- Sustained corner aim (user request 2026-10-07; Jev-calibrated with Scripts/Tools/jev_validate_corner_aim.py:
	// 19 / 24 scenarios, 79 %; the five disagreements are at Jev confidence <= 0.49). ---

	/** Damage taken in the last seconds that counts as a big hit: duck back, HP. */
	float AimBigHitDamage = 30.f;
	/** Incoming fire pressure (SuppressionFromShooters) at or above which he ducks back: two shooters on him. */
	float AimBreakSuppression = 0.7f;
	/** Below this health share AND hit again in the last seconds: duck back (badly wounded = PeekMinHealthFraction always). */
	float AimWoundedHealthFraction = 0.5f;
	/** No target for this long: he lowers the rifle and relaxes back against the wall, s. */
	float AimNoTargetGraceSeconds = 2.f;
	/** Magazine share at or below which a lull (or a forced duck) is used to reload behind the corner. */
	float AimEarlyReloadFraction = 0.34f;
	/** A lull: no target in sight for at least this long, s. */
	float AimEarlyReloadLullSeconds = 0.5f;
	/** After a duck for safety he does not lean out again for this long, s. */
	float AimReentryDelaySeconds = 1.5f;
	/** An enemy on his open side (in front of the wall) closer than this flanks him: duck, cm. */
	float AimFlankDangerCm = 800.f;
	/** A grenade this close: duck, cm. */
	float AimGrenadeDangerCm = 500.f;
};

/** What the operative leaned out in the corner fire stance does now (CoverDecisionRules::DecideCornerAim). */
enum class ECornerAimDecision : uint8
{
	/** Keep aiming round the corner and keep firing from the fire stance (cvr_*_fire_idle / cvr_*_fire). */
	StayAndFire,
	/** Back behind the corner (cvr_*_fire_to_idle) and reload there; out again afterwards if targets remain. */
	DuckToReload,
	/** Back behind the corner for safety (no lean-out for AimReentryDelaySeconds). */
	DuckForSafety,
	/** No target left for the grace period: back to the plain cover pose. */
	ReturnNoTargets
};

/** Inputs of the sustained corner aim decision. */
struct CODEXTACTICS_API FCornerAimSituation
{
	/** Rounds in the magazine / magazine size (0 = empty). */
	float ClipFraction = 1.f;
	/** Spare rounds to reload from. */
	bool bHasReserve = true;
	/** Seconds since a corner-shot target (behind the wall / round the corner, in range) was last in sight; 0 = now. */
	float SecondsWithoutTarget = 0.f;
	/** Turn-based: the pose is kept between his shots within his turn (no "no targets" return). */
	bool bHoldWithoutTargets = false;
	float HealthFraction = 1.f;
	float RecentIncomingDamage = 0.f;
	float SuppressionPressure = 0.f;
	bool bSniperLaserOnMe = false;
	bool bGrenadeNearby = false;
	/** An enemy on his open side (in front of the wall) within AimFlankDangerCm. */
	bool bFlankEnemyNear = false;
};

namespace CoverDecisionRules
{
	/** 0.5 x suppression + 0.5 x min(1, damage / normaliser), + the low-health bonus. */
	CODEXTACTICS_API float DangerScore(const FCoverDecisionConfig& Config, const FCoverFireSituation& Situation);

	/**
	 * Corner Peek vs Blind Fire vs Hold (Jev-calibrated 2026-10-06): high cover without a corner -> Hold (the wall blocks
	 * every line); a sniper laser -> Hold; badly wounded -> BlindFire at point-blank range, else Hold; enemy beyond blind
	 * reach -> CornerPeek unless the danger is extreme (Hold); point-blank with moderate danger -> BlindFire; otherwise
	 * BlindFire from BlindFireDangerThreshold, CornerPeek below it.
	 */
	CODEXTACTICS_API ECoverFireDecision DecideFire(const FCoverDecisionConfig& Config, const FCoverFireSituation& Situation);

	/**
	 * Stand vs Crouch at the wall (Jev-calibrated 2026-10-06): a sniper laser -> Crouch at once; low cover -> Stand only to
	 * fire aimed under light pressure while healthy, else Crouch; high cover -> Crouch under pressure, when badly wounded or
	 * against an elevated enemy, else Stand.
	 */
	CODEXTACTICS_API ECoverStanceDecision DecideStance(const FCoverDecisionConfig& Config, const FCoverStanceSituation& Situation);

	/** Suppression pressure from the number of enemies targeting him (clamped to 1). */
	CODEXTACTICS_API float SuppressionFromShooters(const FCoverDecisionConfig& Config, int32 ShootersTargetingHim);

	/** Recent damage after DeltaSeconds (linear decay over RecentDamageSeconds). */
	CODEXTACTICS_API float DecayRecentDamage(const FCoverDecisionConfig& Config, float RecentDamage, float DeltaSeconds);

	CODEXTACTICS_API EOperativeStance ToStance(ECoverStanceDecision Decision);
	CODEXTACTICS_API ECoverFireMode ToFireMode(ECoverFireDecision Decision);
	CODEXTACTICS_API const TCHAR* FireDecisionName(ECoverFireDecision Decision);

	/**
	 * Sustained corner aim (user request 2026-10-07, Jev-calibrated): while leaned out in the corner fire stance he stays
	 * and fires; he breaks the aim only for a reason. Empty magazine -> DuckToReload (no spare rounds: DuckForSafety, the
	 * weapon is switched behind the corner); sniper laser / grenade near / an enemy flanking on his open side ->
	 * DuckForSafety; badly wounded, a big hit, heavy incoming fire, or wounded and hit again -> DuckForSafety (DuckToReload
	 * when the magazine is low: the forced duck is used to reload); a lull with a low magazine -> DuckToReload; no target
	 * for AimNoTargetGraceSeconds -> ReturnNoTargets (never while bHoldWithoutTargets); else StayAndFire.
	 */
	CODEXTACTICS_API ECornerAimDecision DecideCornerAim(const FCoverDecisionConfig& Config, const FCornerAimSituation& Situation);

	CODEXTACTICS_API const TCHAR* CornerAimDecisionName(ECornerAimDecision Decision);
}
