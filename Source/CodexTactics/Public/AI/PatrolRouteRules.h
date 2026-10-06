#pragma once

#include "CoreMinimal.h"

/**
 * Outpost stealth patrols (Sprint 11 + user requests 2026-10-06: trap -> Search, detection -> Engage; no Godot reference — Sprint 11 spec by Gemini, docs/port/TANDEM.md
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
	/** A tripwire / mine / placed charge went off within the trap alert radius (starts a search, not Engage). */
	Trap,
	/** Its leader died. */
	LeaderLost,
	/** The enemy itself hears the squad (footsteps by gait, a gunshot, a thrown grenade). */
	Hearing,
	/** A frost hound smells an operative. */
	Smell
};

/**
 * How a patrolling enemy reacts to what happened (user amendment 2026-10-06): a detection or an attack by the squad
 * breaks it into Engage (and starts the fight on ambush levels); a trap / placed charge only sends it searching.
 */
enum class EPatrolReaction : uint8
{
	None,
	/** Hunts for the squad around the blast, then returns to its route. */
	Search,
	/** Patrol -> Engage. */
	Engage
};

/** What happened to a patrolling enemy this tick (input of PatrolRouteRules::ShouldBreakPatrol). */
struct CODEXTACTICS_API FPatrolAlertInput
{
	/** Sees an operative (suspicion meter full). */
	bool bSeesOperative = false;
	/** Hears squad footsteps, a squad gunshot or a thrown grenade. */
	bool bHearsOperative = false;
	/** A hound smells an operative. */
	bool bSmellsOperative = false;
	/** Took damage from the squad's direct action (shot, blow, thrown grenade). */
	bool bTookDamage = false;
	/** Hurt by a trap / placed charge (tripwire, mine, trapped object, barrel): counts as a trap event. */
	bool bTookTrapDamage = false;
	/** The leader / an escort broke off its patrol into Engage. */
	bool bPartnerAlerted = false;
	/** The leader / an escort started a search. */
	bool bPartnerSearching = false;
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

	/**
	 * Engage when it detects the squad (sight / hearing / smell), took damage from the squad or its partner engaged;
	 * Search when a trap went off within the trap radius, a trap hurt it or its partner searches; None otherwise.
	 * Engage wins over Search.
	 */
	CODEXTACTICS_API EPatrolReaction EvaluateAlert(const FPatrolAlertInput& Input);

	/** EvaluateAlert == Engage (the patrol breaks; a trap alone no longer does — user amendment 2026-10-06). */
	CODEXTACTICS_API bool ShouldBreakPatrol(const FPatrolAlertInput& Input);

	/** EvaluateAlert == Search. */
	CODEXTACTICS_API bool ShouldStartSearch(const FPatrolAlertInput& Input);

	/** The search is over after DurationSeconds (<= 0: ends at once) and the patrol returns to its route. */
	CODEXTACTICS_API bool IsSearchOver(float ElapsedSeconds, float DurationSeconds);

	/** Search pace: patrol speed x multiplier, never above its normal (combat) speed, never below the patrol pace. */
	CODEXTACTICS_API float GetSearchSpeed(float PatrolWalkSpeed, float SpeedMultiplier, float NormalSpeed);

	/** Sweep point at Angle01 (0..1 of a turn) and Distance01 (0..1, area-uniform) of Radius around Origin (keeps Origin.Z). */
	CODEXTACTICS_API FVector PickSearchPoint(const FVector& Origin, float RadiusCm, float Angle01, float Distance01);

	/** Index of the waypoint closest (planar) to Location; INDEX_NONE for an empty route. Back on the route after a search. */
	CODEXTACTICS_API int32 FindNearestWaypoint(const TArray<FVector>& Waypoints, const FVector& Location);
}
