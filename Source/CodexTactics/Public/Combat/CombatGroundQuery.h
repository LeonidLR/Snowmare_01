#pragma once

#include "CoreMinimal.h"
#include "Combat/CombatGroundRules.h"

class AActor;
class UWorld;

/**
 * World side of the uneven-ground combat rules (G007, CombatGroundRules): the one feet helper, the floor probe between two pairs of
 * feet, and the melee / dead-zone verdicts every caller uses - the real-time AI (reach, close-in step, flank detour), the operative's
 * shot lines (EvaluateShotLine / EvaluateBlindLine), the turn-based bite / shot and RampFightSmoke - so real time, turn-based and the
 * gate test all see the same answer. Tunables: fire_gate.json via FireGateRules::GetConfig().
 */
namespace CombatGround
{
	/**
	 * A character's feet: its capsule bottom (actor location - scaled capsule half height). Crouch / prone resize the capsule and
	 * keep the feet in place, so this holds in every stance and for any capsule size. Any other actor: its location.
	 */
	CODEXTACTICS_API FVector FeetOf(const AActor& Actor);
}