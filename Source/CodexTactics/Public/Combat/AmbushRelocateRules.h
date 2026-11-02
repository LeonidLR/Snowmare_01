#pragma once

#include "CoreMinimal.h"

/**
 * A bypassed ambush relocates (game designer answer 3, 2026-11-01; HYBRID since the designer's decision 1 of 2026-11-01 night,
 * docs/design/COMBAT_LAB.md "Relocation"; UE-only, no Godot reference): an ambush the squad did not pass - it walked past it while watched,
 * or went another way - looks for a CONCEALED ROUTE to a new hiding spot ahead of the squad (a navmesh path no operative has a line of
 * sight to at any point, towers and roofs included) and RUNS it on foot. Spotted on the way, it strikes at once (a coordinated plan
 * springs). No concealed route: NO teleport - it stays put and, after a few failed searches, becomes a patrol following the squad.
 * Pure rules, tested in CodexTactics.Combat.AmbushRelocate.*; UCombatPressureSubsystem samples the routes and drives the runners.
 */

/** Tuning (level JSON "pressure"."ambush"."relocate"). */
struct CODEXTACTICS_API FAmbushRelocateParams
{
	/** Relocation on ("enabled"); off = a bypassed ambush waits until recycled (the 2026-11-01 first version). */
	bool bEnabled = true;
	/** Bypassed this long before it looks for a route, s ("after_sec"). */
	float AfterSeconds = 15.f;
	/** Behind the squad: the bearing from the leader is this far off the squad's heading, degrees ("behind_angle_deg"). */
	float BehindAngleDeg = 100.f;
	/** ... or simply this far from every operative, cm ("far_cm"). */
	float FarCm = 3500.f;
	/** The new spot must lie ahead: cos(heading, leader -> spot) at least this ("min_ahead_cos"). */
	float MinAheadCos = 0.2f;
	/** The route is checked for sight every this many cm along it ("route_sample_cm"). */
	float RouteSampleCm = 100.f;
	/** Routes longer than this are not run (it would never get there in time), cm ("max_route_cm"). */
	float MaxRouteCm = 10000.f;
	/** The runners' pace as a share of their own combat walk speed (BaseWalkSpeed) ("run_speed_scale"). */
	float RunSpeedScale = 1.f;
	/** No concealed route this many searches in a row -> a patrol following the squad ("no_route_tries"; each search after_sec apart). */
	int32 NoRouteTries = 2;
	/** False: with no concealed route it only stays put (until recycled) ("no_route_patrol"). */
	bool bNoRoutePatrol = true;
};

/** What a hidden ambush does about relocating this step. */
enum class EAmbushRelocateDecision : uint8
{
	/** Not bypassed (long enough): stay. */
	Stay,
	/** Due, but an operative sees one of its members: wait until it is unseen. */
	WaitUnseen,
	/** Look for a concealed route to a new hiding spot ahead of the squad now. */
	Relocate
};

/** One candidate route of a relocation search (a hiding spot ahead and the navmesh path to it). */
struct CODEXTACTICS_API FAmbushRouteCandidate
{
	/** The spot's score (CombatPressureRules::ScoreSpot + its kind's bonus). */
	float Score = 0.f;
	/** The navmesh path's length, cm. */
	float PathCm = 0.f;
	/** A full (not partial) navmesh path exists. */
	bool bReachable = false;
	/** Route samples some operative has a line of sight to (0 = concealed all the way). */
	int32 SeenSamples = 0;
	/** The destination itself is out of every operative's sight. */
	bool bDestinationConcealed = true;
};

/** What happens when a search found no concealed route. */
enum class EAmbushNoRouteOutcome : uint8
{
	/** Stay put (hidden where it is) and search again after after_sec. */
	Stay,
	/** Give up hiding: a patrol that follows the squad on foot (its own senses back). */
	Patrol
};

/** One step of a group running its route. */
enum class EAmbushRunStep : uint8
{
	/** On its way. */
	Running,
	/** Every runner is at its new spot: it hides there. */
	Arrived,
	/** An operative saw a runner: it strikes at once. */
	Spotted,
	/** Took far longer than the route should: it stops and hides where it stands (never a teleport). */
	TimedOut
};

namespace AmbushRelocateRules
{
	/**
	 * Bypassed now: the squad is beyond StrikeRangeCm (the ambush cannot strike) and the ambush is behind the squad's way (AngleOffHeadingDeg
	 * beyond BehindAngleDeg) or farther than FarCm from every operative.
	 */
	CODEXTACTICS_API bool IsBypassed(float NearestSquadCm, float AngleOffHeadingDeg, float StrikeRangeCm, const FAmbushRelocateParams& Params);

	/** The bypass clock: grows by DeltaSeconds while bypassed, back to 0 otherwise. */
	CODEXTACTICS_API float StepBypassedSeconds(bool bBypassed, float Seconds, float DeltaSeconds);

	/** Relocate once bypassed for AfterSeconds and unseen; WaitUnseen while seen; Stay otherwise (and always when disabled). */
	CODEXTACTICS_API EAmbushRelocateDecision Decide(float BypassedSeconds, bool bSeenNow, const FAmbushRelocateParams& Params);

	/** A new spot qualifies: ahead of the squad (AheadCos >= MinAheadCos) and concealed (no operative's line of sight). */
	CODEXTACTICS_API bool IsGoodDestination(float AheadCos, bool bConcealed, const FAmbushRelocateParams& Params);

	/**
	 * Points every StepCm along the polyline Path (its first and last point included; a single point -> itself). The route's sight check
	 * traces to each of them.
	 */
	CODEXTACTICS_API TArray<FVector2D> SamplePath(const TArray<FVector2D>& Path, float StepCm);

	/** Planar length of the polyline, cm. */
	CODEXTACTICS_API float PathLength(const TArray<FVector2D>& Path);

	/** A route it may run: reachable, no sample seen, the destination concealed, not longer than MaxRouteCm. */
	CODEXTACTICS_API bool IsConcealedRoute(const FAmbushRouteCandidate& Route, const FAmbushRelocateParams& Params);

	/** The best concealed route (highest score; a tie -> the shorter path); INDEX_NONE when none is concealed. */
	CODEXTACTICS_API int32 PickRoute(const TArray<FAmbushRouteCandidate>& Routes, const FAmbushRelocateParams& Params);

	/** After FailedSearches searches in a row with no concealed route: Patrol once NoRouteTries is reached (and allowed), else Stay. */
	CODEXTACTICS_API EAmbushNoRouteOutcome NoRouteOutcome(int32 FailedSearches, const FAmbushRelocateParams& Params);

	/** The longest a run may take: twice the path at SpeedCmS plus 10 s (a door, a bump into a mate), s. */
	CODEXTACTICS_API float RunLimitSeconds(float PathCm, float SpeedCmS);

	/** Spotted wins over everything (it strikes); then every runner arrived; then the time limit; else running. */
	CODEXTACTICS_API EAmbushRunStep StepRun(bool bSpotted, bool bAllArrived, float ElapsedSeconds, float LimitSeconds);

	/** "running" / "arrived" / "spotted" / "timed out" (logs). */
	CODEXTACTICS_API const TCHAR* RunStepName(EAmbushRunStep Step);
}
