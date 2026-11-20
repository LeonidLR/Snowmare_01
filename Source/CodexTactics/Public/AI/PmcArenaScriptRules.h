#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/**
 * The PMC arena's SCRIPTED OPPONENT (smart-enemies T-03, docs/design/gdd/smart-enemies-roadmap.md T-03; GDD smart-enemies.md "Acceptance
 * Criteria": scenario smokes use a scripted opponent - a walking / bounding stand-in for the player's squad - not self-play).
 * CodexTactics.PmcArena ... Opponent=Script Script=run puts side B (always the attackers) on the scenario route and walks it to the goal in
 * one go with NO commander (UPmcSquadSubsystem::SetTeamCommanded(B, false)); the men fire back by the operative body's own rules. The same
 * seed gives the same walk: nothing here is random.
 *
 * This file is the PURE half: the mode names, the data keys and the route -> waypoints rule. The runtime follower (issuing the Move orders,
 * the per-man progress) is in CodexTacticsDev Debug/PmcArenaCommand.cpp. Script=bound is the seam T-07 fills (a bounding follower: one
 * half-squad moves while the other covers); until then ResolveMode falls back to run with a warning. UE-only, no Godot reference.
 * Tested in CodexTactics.AI.PmcArena.ScriptOpponent.
 */
namespace PmcArenaScriptRules
{
	enum class EMode : uint8
	{
		/** Walk the waypoints without stopping, return fire by the body's own rules. */
		Run,
		/** Bounding overwatch (T-07): not implemented yet - falls back to Run. */
		Bound,
	};

	/** The "pmc_arena" keys (new, all optional; the code defaults below without them). */
	struct CODEXTACTICS_API FConfig
	{
		/** script_waypoint_m: a waypoint every this far along the route, cm. */
		float WaypointCm = 600.f;
		/** script_arrive_m: a man has reached a waypoint within this planar distance, cm. */
		float ArriveCm = 200.f;
		/** script_level_m: ... and within this height of it (feet vs the route's point), cm. */
		float LevelCm = 150.f;
		/** script_sprint: the Move orders are sprints (a sprinting operative cannot fire - CanShoot); false = he walks and fires back. */
		bool bSprint = false;
	};

	/** "run" / "bound" (case-insensitive) -> the mode; false for anything else (Out untouched). */
	CODEXTACTICS_API bool ParseMode(const FString& Text, EMode& Out);
	CODEXTACTICS_API const TCHAR* ModeName(EMode Mode);

	/**
	 * The mode actually played. Bound is not implemented (T-07 fills it): it plays as Run and bFellBack is set so the caller logs the warning.
	 * Run -> Run.
	 */
	CODEXTACTICS_API EMode ResolveMode(EMode Requested, bool& bFellBack);

	/**
	 * Reads the "script_*" keys of a level JSON's "pmc_arena" object (Arena = that object) into Out, clamped (waypoint 1..50 m, arrive
	 * 0.5..10 m, level 0..10 m). A missing key keeps Out's value.
	 */
	CODEXTACTICS_API void ReadJson(const FJsonObject& Arena, FConfig& Out);

	/**
	 * The route as waypoints: a point every SpacingCm of path length (the 3D polyline), the route's start left out (the men stand on it), the
	 * route's END always the last waypoint exactly. A last interval shorter than half the spacing is merged into the end (no stub waypoint
	 * a step from the goal). Repeated points are ignored. A one-point route gives that point, an empty one gives nothing, SpacingCm <= 0
	 * gives only the end. Pure and deterministic: the same route and spacing give the same list.
	 */
	CODEXTACTICS_API TArray<FVector> WaypointsFromRoute(const TArray<FVector>& Route, float SpacingCm);

	/** The polyline's length (3D), cm. */
	CODEXTACTICS_API float RouteLength(const TArray<FVector>& Route);

	/**
	 * A man's next waypoint index after standing at Feet: Index while he has not reached waypoint Index (ArriveCm planar and LevelCm in
	 * height), else the next one, and so on - Waypoints.Num() = he has reached the goal. A man who reaches several in one step (a waypoint
	 * on top of the next) passes them all; he never goes backwards.
	 */
	CODEXTACTICS_API int32 AdvanceWaypoint(const TArray<FVector>& Waypoints, int32 Index, const FVector& Feet, const FConfig& Config);
}
