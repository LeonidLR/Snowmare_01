#pragma once

#include "CoreMinimal.h"

/**
 * The fire arc (bug #33, user PIE on Depot 2026-11-07, UE-only): "a PRONE PMC faces one way but fires in another (at the player)".
 * Rule for every shooter (the squad and the PMC fighters share AOperativeCharacter): a round leaves only inside the arc of his body /
 * rifle - he turns first. The turn to an aim point eases in but is never slower than the aim turn rate (AimTurnRateDegPerSec,
 * 360 deg/s); lying PRONE the whole body pivots on the ground: no faster than the prone turn rate of the movement config
 * (OperativeMovementRules TurnRateProne, 143 deg/s) - a 180 deg turn takes over a second, and the round waits for it.
 * Prone there is no upper-body twist (AimOffsetRules: no aim offset prone), so the arc is the body's: the residual is the larger of
 * the rifle's and the body's error.
 */
namespace FireArcRules
{
	/** Ease rate of the standing / crouched aim turn (RInterpTo speed). */
	constexpr float AimEaseSpeed = 12.f;

	/**
	 * One frame of the aim turn from CurrentYaw to TargetYaw, deg: standing / crouched the faster of the eased and the constant
	 * AimTurnRateDegPerSec step; prone only the constant ProneTurnRateDegPerSec step.
	 */
	CODEXTACTICS_API float StepAimYaw(float CurrentYaw, float TargetYaw, float DeltaSeconds, bool bProne, float AimTurnRateDegPerSec,
		float ProneTurnRateDegPerSec);

	/** The aim error a round is judged by, deg: prone the larger of the rifle's and the body's, else the rifle's / stance's. */
	CODEXTACTICS_API float ShotResidualDeg(float AimResidualDeg, float BodyErrorDeg, bool bProne);

	/**
	 * Bug 058c / 061 (game designer 2026-11-15: "he faces the wall and his rounds fly at the enemy"; "they fire first and turn after - it
	 * must be turn, raise, then the shot"): standing / crouched and STILL, the upper-body twist alone must not carry the round - the body
	 * has to face the target within BodyArcDeg first. The residual is the larger of the rifle's error and the body's error beyond the arc.
	 * Walking fire keeps the twist (BodyArcDeg >= 180 disables the gate).
	 */
	CODEXTACTICS_API float BodyArcResidualDeg(float AimResidualDeg, float BodyErrorDeg, float BodyArcDeg);

	/** Default half-width of the standing / crouched still-fire body arc, deg (CVar Codex.Fire.BodyArcDeg). */
	constexpr float DefaultStillBodyArcDeg = 30.f;
}
