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

	/**
	 * The floor profile from one pair of feet to the other (fire_gate.json "ground_probe"): [0] = FromFeet, then vertical traces
	 * (static / dynamic world geometry, Ignore skipped) at CombatGroundRules::ProbeDistances, last = ToFeet. A probe that misses or
	 * lands off the previous floor by more than "melee_reach" step_max_cm retries lateral_cm to each side and keeps the floor nearest
	 * the previous sample; a probe with no floor at all stays bHit false (unknown). OutSpacingCm = the neighbour spacing.
	 */
	CODEXTACTICS_API TArray<FFloorSample> ProbeFloor(const UWorld& World, const FVector& FromFeet, const FVector& ToFeet,
		TConstArrayView<const AActor*> Ignore, float& OutSpacingCm);

	/**
	 * The one melee rule on real ground (EnemyAIRules::CanMeleeOnGround): the floor between the two pairs of feet is probed only when
	 * the answer depends on it (flat ground and far targets need no traces).
	 */
	CODEXTACTICS_API bool CanMeleeAt(const UWorld& World, const FVector& AttackerFeet, const FVector& TargetFeet, float AttackRangeCm,
		TConstArrayView<const AActor*> Ignore);
	/** CanMeleeAt between two characters' feet (FeetOf). */
	CODEXTACTICS_API bool CanMelee(const UWorld& World, const AActor& Attacker, const AActor& Target, float AttackRangeCm);

	/** Planar bite reach toward Target on the real ground (EnemyAIRules::MeleeReachPlanarOnGround): the close-in step and the flank detour. */
	CODEXTACTICS_API float MeleeReachPlanar(const UWorld& World, const AActor& Attacker, const AActor& Target, float AttackRangeCm);

	/**
	 * The platform dead zone's level edge (fire_gate.json "dead_zone" edge_drop_cm, CombatGroundRules::HasLevelEdge) on the probed
	 * floor from the shooter's feet to the target's: covers lines of any length (a lip 30-60 cm out included), a missed probe is
	 * unknown and never an edge.
	 */
	CODEXTACTICS_API bool HasDeadZoneEdge(const UWorld& World, const FVector& ShooterFeet, const FVector& TargetFeet, TConstArrayView<const AActor*> Ignore);

	/** The dead zone as every shot path applies it: the geometry (SquadFireRules::IsInDeadZone) and a blocked trace or a level edge. */
	CODEXTACTICS_API bool BlocksShot(const UWorld& World, const FVector& ShooterFeet, const FVector& TargetFeet, bool bTraceBlocked,
		TConstArrayView<const AActor*> Ignore);
}