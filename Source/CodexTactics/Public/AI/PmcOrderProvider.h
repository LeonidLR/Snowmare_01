#pragma once

#include "CoreMinimal.h"
#include "AI/PmcCommanderRules.h"

class AOperativeCharacter;

/**
 * The order provider hook of the PMC commander (UE-only, no Godot reference; Learning Agents probe P0, 2026-11-23).
 * UPmcSquadSubsystem scores the catalog orders per fighter and runs the scorer's pick (PmcCommanderRules::PickCommitted). A team with a
 * provider (UPmcSquadSubsystem::SetTeamOrderProvider) asks it instead: every decision the subsystem builds one request per deciding
 * fighter and hands the whole team to the provider in ONE call (a neural policy evaluates its agents together), then executes the picks
 * through the same ExecuteOrder as the scorer's. No provider = the scorer's code path, unchanged.
 */

/** One fighter's decision as the provider sees it: what the scorer saw, what it scored and what it would run. */
struct CODEXTACTICS_API FPmcOrderRequest
{
	/** The fighter deciding now (alive; not on a ladder, mid-drop or at a mortar - those do not decide this tick). */
	TWeakObjectPtr<AOperativeCharacter> Fighter;
	/** His view (RunningOrder set). */
	FPmcFighterView View;
	/** The squad as he sees it (the mates' counters without his own running order). */
	FPmcSquadView Squad;
	/** The scorer's scores, best first; 0 = the order makes no sense / is ruled out now. */
	TArray<FPmcOrderScore> Scored;
	/** His current order, whether it still runs, its age and the cooldowns of the orders that failed. */
	FPmcCommitment Commitment;
	/** The order the scorer would run (PmcCommanderRules::PickCommitted). */
	EPmcOrder ScorerPick = EPmcOrder::Hold;
	/**
	 * Out: the provider's order (starts as ScorerPick - a provider that leaves it gives the scorer's choice). The subsystem passes it
	 * through the scorer's gates (PmcPolicyRules::GatePick): an order the scorer rules out becomes Hold, a running order inside its
	 * minimum hold goes on.
	 */
	EPmcOrder Pick = EPmcOrder::Hold;
};

/** Picks the orders of a commanded team in place of the utility scorer's pick. Owned by its creator; the subsystem keeps a weak pointer. */
class CODEXTACTICS_API IPmcOrderProvider
{
public:
	virtual ~IPmcOrderProvider() = default;

	/**
	 * One decision of team TeamId (Weights = the team's commander weights): writes Request.Pick for every request. Game thread; may
	 * block while a trainer updates the policy.
	 */
	virtual void PickOrders(int32 TeamId, const FPmcWeights& Weights, TArrayView<FPmcOrderRequest> Requests) = 0;
};
