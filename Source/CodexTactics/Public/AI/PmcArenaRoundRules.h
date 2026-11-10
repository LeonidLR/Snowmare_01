#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

/**
 * The PMC arena's round bookkeeping (fix/arena-rpg-contact-roof 2026-11-10, the two known gaps after fix/pmc-arena-stalemate,
 * docs/systems/pmc-squad-ai.md section 11): what counts as CONTACT between the sides (rifle shots AND the RPG-7: a rocket launched at an
 * enemy, or a rocket's burst near one) and the fight flow computed from those contact moments; and the objective placement rule - a
 * point is only usable when BOTH sides' spawns reach it by a full (not partial) navmesh path (a roof without a ladder route made the
 * round a draw). Pure rules used by CodexTactics.PmcArena (CodexTacticsDev, Debug/PmcArenaCommand.cpp). Arena-only; UE-only, no Godot
 * reference. Tested in CodexTactics.AI.PmcArena.RocketContact* / .ContactFlow* / .ObjectiveReach*.
 */

/** A round's fight flow from its contact moments (the round JSON's "contact" + "flow"). */
struct CODEXTACTICS_API FPmcArenaFlow
{
	/** Any contact moment at all (false = a no_contact round: a -3 draw in the trainer). */
	bool bContact = false;
	/** Contact moments per minute of the round (rifle shots + rockets + rocket bursts near an enemy). */
	float EventsPerMin = 0.f;
	/** The first contact, s (-1: none). */
	float FirstContactS = -1.f;
	/** The longest gap between contact moments, including the quiet tail to the end (the whole round without contact), s. */
	float LongestQuietS = 0.f;
	int32 Events = 0;
};

namespace PmcArenaRoundRules
{
	/**
	 * A rocket's burst at Burst is contact with the enemy when a live enemy of the shooter stands within RadiusCm of it (3D distance; the
	 * same reach as the burst's suppression, suppression.json launcher.radius_cm: "one RPG salvo is enough to make any squad duck").
	 * RadiusCm <= 0 = never.
	 */
	CODEXTACTICS_API bool IsLauncherContact(const FVector& Burst, const TArray<FVector>& LiveEnemies, float RadiusCm);

	/**
	 * The flow of a round of DurationS from its contact moments (any order; times in round seconds): contact at all, moments per minute
	 * (the duration at least 1 s), the first moment, the longest quiet spell (gaps + the tail to the end; the whole round without any).
	 */
	CODEXTACTICS_API FPmcArenaFlow ComputeFlow(TArray<float> ContactTimes, float DurationS);

	/** A navmesh query answer is a usable route: a path was found and it is not partial (it ends at the goal). */
	CODEXTACTICS_API bool IsFullPath(bool bFound, bool bPartial);

	/**
	 * The objective rule: Point is usable when a full path leads to it from BOTH spawns (HasFullPath(From, To); the order matters - a
	 * drop link is one-way).
	 */
	CODEXTACTICS_API bool IsReachableFromBoth(const FVector& Point, const FVector& SpawnA, const FVector& SpawnB,
		TFunctionRef<bool(const FVector& From, const FVector& To)> HasFullPath);

	/** The first of Candidates (in order) reachable from both spawns (IsReachableFromBoth); INDEX_NONE when none is. */
	CODEXTACTICS_API int32 PickReachable(const TArray<FVector>& Candidates, const FVector& SpawnA, const FVector& SpawnB,
		TFunctionRef<bool(const FVector& From, const FVector& To)> HasFullPath);
}
