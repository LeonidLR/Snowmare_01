#pragma once

#include "CoreMinimal.h"
#include "AI/RepathRules.h"
#include "DetourCrowdAIController.h"
#include "EnemyAIController.generated.h"

/**
 * AI controller for enemy characters with Detour Crowd avoidance.
 * Prevents swarming enemies from clipping through each other. While its enemy is held by the turn-based fight
 * (AEnemyCharacter::IsTurnBasedHeld) every navigation move is refused: the grid moves its units itself, and an AI move
 * issued from an event (a hit, a timer) would walk a frozen enemy off the grid (bug fix 2026-10-06).
 *
 * Re-path throttle (audit 2026-10-11 #1): the real-time enemy AI asks for its move every tick through
 * RequestMoveToLocation / RequestMoveToActor; only the asks RepathRules::ShouldRepath lets through reach MoveTo (each
 * MoveTo is a synchronous pathfind in UE 5.8). CVars: Codex.AI.Repath (1 on / 0 = every ask issued, the old behaviour),
 * Codex.AI.RepathInterval, Codex.AI.RepathMinGoalDeltaCm. Counters for smokes / probes: GetTotalMoveRequestCount (every
 * MoveTo that reached the engine on any enemy controller), GetTotalPathfindCount (those past the already-at-goal test)
 * and GetTotalMoveAskCount (every throttled ask).
 */
UCLASS()
class CODEXTACTICS_API AEnemyAIController : public ADetourCrowdAIController
{
	GENERATED_BODY()

public:
	AEnemyAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Refuses the move (Failed) while the pawn is held by the turn-based fight; otherwise the normal move (counted). */
	virtual FPathFollowingRequestResult MoveTo(const FAIMoveRequest& MoveRequest, FNavPathSharedPtr* OutPath = nullptr) override;

	/**
	 * Throttled MoveToLocation(Goal, AcceptanceRadius, bStopOnOverlap false, pathfinding on, the other defaults): issued
	 * only when RepathRules::ShouldRepath says so. Returns true when a MoveTo was issued by this call.
	 */
	bool RequestMoveToLocation(const FVector& Goal, float AcceptanceRadius);

	/** Throttled MoveToActor(Goal, AcceptanceRadius, the other defaults); see RequestMoveToLocation. */
	bool RequestMoveToActor(AActor* Goal, float AcceptanceRadius);

	/** Forgets the last throttled move: the next ask is issued at once (turn-based hold, world AI pause). */
	void ResetRepathState();

	/** What the path following does with the last throttled move. */
	ERepathFollowState GetRepathFollowState() const;

	/** MoveTo calls of this controller that reached the engine (a pathfind, or an already-at-goal check). */
	int64 GetMoveRequestCount() const { return MoveRequestCount; }

	/** MoveTo calls of this controller past the already-at-goal test: path queries (synchronous pathfinds). */
	int64 GetMovePathfindCount() const { return MovePathfindCount; }

	/** Throttled asks of this controller (what the per-tick AI wanted: the request count before the throttle). */
	int64 GetMoveAskCount() const { return MoveAskCount; }

	/** Sum of GetMoveRequestCount over every enemy controller since the last ResetTotalMoveCounters. */
	static int64 GetTotalMoveRequestCount();

	/** Sum of GetMovePathfindCount over every enemy controller since the last ResetTotalMoveCounters. */
	static int64 GetTotalPathfindCount();

	/** Sum of GetMoveAskCount over every enemy controller since the last ResetTotalMoveCounters. */
	static int64 GetTotalMoveAskCount();

	/** Zeroes the two totals (smokes / probes measure a window). */
	static void ResetTotalMoveCounters();

	/** The settings from the Codex.AI.Repath* CVars. */
	static FRepathSettings GetRepathSettings();

protected:
	virtual void OnPossess(APawn* InPawn) override;

	virtual void OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result) override;

private:
	/** Counts the ask and asks RepathRules whether it must be issued (Request: the wanted move, for the reach test). */
	bool ShouldIssue(const FRepathGoal& Wanted, const FAIMoveRequest& Request);

	/** Remembers the move just issued (its goal, time and the MoveTo result). */
	void RecordIssued(const FRepathGoal& Wanted, EPathFollowingRequestResult::Type Code);

	/** Goal of the last throttled move that was issued. */
	FRepathGoal LastRepathGoal;
	bool bHasRepathGoal = false;
	/** World time it was issued, s. */
	double LastRepathTime = 0.0;
	/** Request id it got. */
	uint32 LastRepathRequestId = 0;
	/** Its MoveTo returned Failed. */
	bool bLastRepathFailed = false;

	/** Id of the move the last MoveTo call returned (read right after MoveToLocation / MoveToActor). */
	uint32 LastMoveToRequestId = 0;

	/** The last request the path following finished, and how. */
	uint32 LastFinishedRequestId = 0;
	bool bLastFinishSuccess = false;
	bool bLastFinishInterrupted = false;

	int64 MoveRequestCount = 0;
	int64 MovePathfindCount = 0;
	int64 MoveAskCount = 0;
};
