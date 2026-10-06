#pragma once

#include "CoreMinimal.h"

/**
 * Outpost stealth patrols (Sprint 11, no Godot reference — Sprint 11 spec by Gemini, docs/port/TANDEM.md
 * «SPRINT 11 DIRECTIVE»). Pure rules of the spline patrol route, the escort tether and the patrol -> engage break,
 * tested in CodexTactics.AI.PatrolRoute.*; APatrolRouteActor / AEnemyCharacter apply them in the world.
 */

/** Why a patrol broke off (logs, floating text). */
enum class EPatrolAlertCause : uint8
{
	/** The enemy itself sees an operative (Sprint 08 sight rules). */
	Sight,
	/** The enemy itself took damage. */
	Damage,
	/** Its leader / escort broke off (took damage, saw someone, was alerted). */
	PartnerAlert,
	/** A tripwire or mine went off within the trap alert radius. */
	Trap,
	/** Its leader died. */
	LeaderLost
};

/** What happened to a patrolling enemy this tick (input of PatrolRouteRules::ShouldBreakPatrol). */
struct CODEXTACTICS_API FPatrolAlertInput
{
	bool bSeesOperative = false;
	bool bTookDamage = false;
	/** The leader / an escort took damage or broke off its patrol. */
	bool bPartnerAlerted = false;
	/** Planar distance to a tripwire / mine that went off, cm; < 0: none. */
	float TrapDistanceCm = -1.f;
	float TrapAlertRadiusCm = 2000.f;
};

/** Where an escort should go this tick. */
struct CODEXTACTICS_API FEscortDecision
{
	bool bShouldMove = false;
	FVector Destination = FVector::ZeroVector;
};

namespace PatrolRouteRules
{
	/** A tripwire / mine detonation alerts patrols this close (user decision 2026-10-06: 20 m), cm. */
	inline constexpr float TrapAlertRadiusCm = 2000.f;
	/** Escort tether band around its leader (TANDEM Sprint 11-B), cm. */
	inline constexpr float EscortMinCm = 200.f;
	inline constexpr float EscortMaxCm = 350.f;
	/** A patroller is at its waypoint this close (planar), cm. */
	inline constexpr float WaypointAcceptCm = 80.f;

	/**
	 * Next waypoint after CurrentIndex on a route of NumWaypoints. Loop: 0->1->2->0. Ping-pong (not loop): 0->1->2->1->0,
	 * bInOutForward flips at the ends. Neither: stops at the last point (returns INDEX_NONE there). INDEX_NONE for an
	 * empty route; an out-of-range CurrentIndex restarts at 0. A loop takes precedence over ping-pong.
	 */
	CODEXTACTICS_API int32 GetNextWaypointIndex(int32 NumWaypoints, int32 CurrentIndex, bool bIsLoop, bool bPingPong, bool& bInOutForward);

	/** Pause at waypoint Index: its PerPointWaitTime entry when > 0, else DefaultSeconds (never negative). */
	CODEXTACTICS_API float GetWaitTime(const TArray<float>& PerPointWaitTime, int32 Index, float DefaultSeconds);

	/**
	 * Escort tether: further than MaxCm from the leader -> catch up to the middle of the band (on the line leader ->
	 * escort, so it stays on its side); already moving -> keeps going until inside the middle of the band (hysteresis, no
	 * stop-start at the edge); otherwise it mills about where it is. Planar distances.
	 */
	CODEXTACTICS_API FEscortDecision EvaluateEscort(const FVector& EscortLocation, const FVector& LeaderLocation, bool bCurrentlyMoving,
		float MinCm = EscortMinCm, float MaxCm = EscortMaxCm);

	/** A trap that went off DistanceCm away is heard (0 <= distance <= radius). */
	CODEXTACTICS_API bool IsTrapHeard(float DistanceCm, float RadiusCm = TrapAlertRadiusCm);

	/** The patrol breaks into Engage: sees an operative, took damage, its partner was alerted, or a trap went off in earshot. */
	CODEXTACTICS_API bool ShouldBreakPatrol(const FPatrolAlertInput& Input);
}
