#pragma once

#include "CoreMinimal.h"

/**
 * When an AI pawn re-issues its navigation move (audit 2026-10-11 action #1 / P1; UE-only, no Godot reference — Godot's
 * NavigationAgent3D re-targeted cheaply every frame). In UE 5.8 every AAIController::MoveTo runs a synchronous pathfind
 * (AIController.cpp MoveTo -> FindPathForMoveRequest), and the real-time enemy AI used to call MoveToActor /
 * MoveToLocation on every tick. These pure rules decide whether a wanted move must reach the engine again; they are
 * applied by AEnemyAIController::RequestMoveToLocation / RequestMoveToActor and tested in CodexTactics.AI.Repath.*.
 *
 * - The pawn already stands within the goal's reach (the engine's own HasReached test of the wanted move): issued, as
 *   before — MoveTo answers AlreadyAtGoal at once without a pathfind and ends the walk there. (Without it a chaser
 *   followed by the engine walked on past the reach boundary the per-tick ask stopped it at — a behaviour change.)
 * - A new goal (another actor, a location instead of an actor or back, another acceptance radius) is issued at once.
 * - A move that was stopped / aborted / replaced by someone else's move is issued at once (no added standing still).
 * - Following an actor goal: never re-issued — the engine observes the goal actor (ANavigationData observed paths,
 *   0.5 s / 1 m tether) and walks straight at it on the last segment.
 * - Following a location goal: re-issued when the goal moved more than MinGoalDeltaCm, at most every IntervalSeconds.
 * - The move ended at the goal (or a partial path's end): re-issued when the goal moved more than MinGoalDeltaCm, else
 *   every IntervalSeconds (it was pushed off the spot, the actor walked away inside the tolerance).
 * - The move failed (no path, blocked, off the path): retried every IntervalSeconds instead of every frame.
 */

/** What the pawn's path following does with the last move these rules let through. */
enum class ERepathFollowState : uint8
{
	/** No move issued yet through the rules. */
	None,
	/** Still walking that move (moving, waiting for a path or paused). */
	Following,
	/** That move ended successfully (at the goal, at the end of a partial path, or already at the goal). */
	Reached,
	/** That move failed: no path / invalid goal / refused, blocked or off its path. */
	Failed,
	/** That move was stopped (StopMovement), aborted, or replaced by another move request. */
	Stopped
};

/** A move goal as the rules compare it. */
struct CODEXTACTICS_API FRepathGoal
{
	/** Goal actor's UObject unique id; 0 = a location goal. */
	uint32 GoalActorId = 0;
	/** The goal location (actor goals: the actor's location), cm. */
	FVector Location = FVector::ZeroVector;
	/** Acceptance radius of the move, cm. */
	float AcceptanceRadius = 0.f;

	bool IsActorGoal() const { return GoalActorId != 0; }
};

/** Tunables (CVars Codex.AI.Repath / Codex.AI.RepathInterval / Codex.AI.RepathMinGoalDeltaCm). */
struct CODEXTACTICS_API FRepathSettings
{
	/** Off: every wanted move is issued (the old per-frame behaviour, for A/B measurements). */
	bool bEnabled = true;
	/** Shortest time between two re-issues of an unchanged / slowly moving goal, s. */
	float IntervalSeconds = 0.3f;
	/** A goal that moved further than this is a changed goal, cm. */
	float MinGoalDeltaCm = 75.f;
};

namespace RepathRules
{
	/**
	 * Follow state from the path-following facts. bIdle: the path following is Idle; bCurrentRequestIsOurs: its current
	 * request is the move the rules let through; bRequestFailed: that MoveTo returned Failed; bFinishedOurs: the last
	 * finished request is that move, with bFinishSuccess / bFinishInterrupted (UserAbort / NewRequest) from its result.
	 */
	CODEXTACTICS_API ERepathFollowState ClassifyFollowState(bool bHasRequest, bool bIdle, bool bCurrentRequestIsOurs, bool bRequestFailed,
		bool bFinishedOurs, bool bFinishSuccess, bool bFinishInterrupted);

	/**
	 * True when the wanted move must be issued to the engine. Last: the goal of the last issued move (nullptr: none);
	 * SecondsSinceLast: time since it was issued; bAlreadyAtGoal: the pawn is within the wanted move's reach now (the
	 * MoveTo would finish at once, no pathfind).
	 */
	CODEXTACTICS_API bool ShouldRepath(const FRepathGoal* Last, const FRepathGoal& Wanted, ERepathFollowState State, double SecondsSinceLast,
		bool bAlreadyAtGoal, const FRepathSettings& Settings);
}
