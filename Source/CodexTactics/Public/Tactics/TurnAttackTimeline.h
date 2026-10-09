#pragma once

#include "CoreMinimal.h"

/**
 * Presentation order of a turn-based grid attack (user request 2026-10-09: the tracer / hit came before the shooter had
 * turned, and for the sniper before she had knelt). The decision (AP, hit roll, damage) is made at the click; the shot is
 * shown in this order: 1) the body turns to the target, 2) the sniper's kneel clip ends, 3) the fire clip starts, 4) the
 * tracer, hit, damage and feed lines at that moment, 5) bolt etc. The turn-based flow waits for it (IsBusy).
 * Runtime: UTurnBasedCombatSubsystem pending grid shot (Tick).
 */
struct FTurnShotReadiness
{
	/** Seconds since the attack was ordered. */
	float Elapsed = 0.f;
	/** How far the body (mesh) still is from the target direction, deg. */
	float BodyYawErrorDeg = 0.f;
	/** A stance clip (the sniper's kneel) is still playing. */
	bool bStanceSettling = false;
	/** The attacker is gone / dead: the shot is dropped. */
	bool bAttackerLost = false;
};

enum class ETurnShotStep : uint8
{
	/** Turning / kneeling: keep waiting. */
	Wait,
	/** Start the fire clip and resolve the shot now. */
	Fire,
	/** The attacker is gone: drop the shot (no tracer, no damage). */
	Drop
};

namespace TurnAttackTimeline
{
	/** The body counts as on target within this many degrees. */
	constexpr float AimToleranceDeg = 5.f;
	/** A shot never waits longer than this (safety: a turn / clip that never ends), s. */
	constexpr float MaxWaitSeconds = 3.f;

	CODEXTACTICS_API ETurnShotStep NextStep(const FTurnShotReadiness& Readiness);
}
