#pragma once

#include "CoreMinimal.h"
#include "AI/PmcCommanderRules.h"

/**
 * The pure rules of the PMC order policy (UE-only, no Godot reference; Learning Agents probe P0, 2026-11-23): a neural policy picks the
 * ORDER of each PMC defender in place of the utility scorer (docs/systems/pmc-policy-probe.md). No world access and no Learning Agents
 * types here: the action set, the action mask that keeps the scorer's safety gates, the observation vector built from the commander's
 * views and the reward of one step. UPmcPolicyInteractor / FPmcPolicyBrain feed them to the plugin. Tested in CodexTactics.AI.PmcPolicy.*.
 */

/** The reward terms of the defenders' policy (the lead's P0 numbers). */
struct CODEXTACTICS_API FPmcPolicyRewards
{
	/** Per attacker killed since the last step (shared by the living defenders). */
	float AttackerKilled = 1.f;
	/** The agent's own death (his last step). */
	float OwnDeath = -1.f;
	/** The defenders won the round. */
	float RoundWon = 3.f;
	/** The attackers captured the point. */
	float PointCaptured = -3.f;
	/** Per second an attacker stood on the point. */
	float EnemyOnPointPerSecond = -0.02f;
};

/** What happened to one agent between two of his steps. */
struct CODEXTACTICS_API FPmcPolicyEvents
{
	int32 AttackersKilled = 0;
	bool bDied = false;
	bool bRoundWon = false;
	bool bPointCaptured = false;
	float EnemyOnPointSeconds = 0.f;
};

namespace PmcPolicyRules
{
	// --- Actions: one exclusive choice over the defender's orders ---

	/** The orders the policy chooses between, in action-index order (Hold first: the action that is never masked alone). */
	inline constexpr EPmcOrder ActionOrders[] = {
		EPmcOrder::Hold, EPmcOrder::TakeCover, EPmcOrder::Suppress, EPmcOrder::Reload, EPmcOrder::FallBack,
		EPmcOrder::Objective, EPmcOrder::Ambush, EPmcOrder::Crouch, EPmcOrder::RpgStrike };
	inline constexpr int32 ActionCount = UE_ARRAY_COUNT(ActionOrders);
	/** The action index of Hold. */
	inline constexpr int32 HoldAction = 0;

	/** The action index of Order; INDEX_NONE when the policy cannot give it (Advance, Flank, Heal, Relocate, Sidearm). */
	CODEXTACTICS_API int32 ActionIndexOf(EPmcOrder Order);
	/** The order of an action index; Hold for an index out of range. */
	CODEXTACTICS_API EPmcOrder OrderOfAction(int32 ActionIndex);

	// --- The scorer's safety gates as a mask ---

	/**
	 * The orders that may be given now, bit i = EPmcOrder i (PickCommitted's gates, without its ranking):
	 * - an order is allowed while the scorer scores it above 0 and it is not cooling down after a failure (Commitment.FailedUntil);
	 * - a running active order (not Hold / Ambush / Crouch) that still scores and is younger than Weights.OrderMinHoldSeconds is the ONLY
	 *   one allowed (the commitment: no walk re-decided twice a second);
	 * - nothing allowed at all = Hold (PickCommitted's fallback).
	 */
	CODEXTACTICS_API uint32 AllowedOrderMask(const TArray<FPmcOrderScore>& Scored, const FPmcCommitment& Commitment, const FPmcWeights& Weights);
	CODEXTACTICS_API bool IsOrderAllowed(uint32 OrderMask, EPmcOrder Order);

	/**
	 * The action indices the policy may NOT sample (the Learning Agents action modifier's masked indices). Never all of them: when no
	 * action's order is allowed (bCanAct false: the fighter does not decide this tick; or only an order outside the action set is
	 * allowed) everything but Hold is masked and the pick is settled by GatePick.
	 */
	CODEXTACTICS_API void MaskedActions(uint32 OrderMask, bool bCanAct, TArray<int32>& OutMaskedActions);

	/** A provider's pick through the gates: the pick when allowed, else the running order when that is allowed (its minimum hold), else Hold. */
	CODEXTACTICS_API EPmcOrder GatePick(EPmcOrder Pick, uint32 OrderMask, const FPmcCommitment& Commitment);

	/** Why a provider's (gated) pick replaces / keeps the current order, in PickCommitted's terms (the arena's track log). */
	CODEXTACTICS_API EPmcPickReason PickReason(EPmcOrder Pick, const TArray<FPmcOrderScore>& Scored, const FPmcCommitment& Commitment);

	// --- Observations: built from the commander's views only ---

	/** The observation vector's layout. Every value lies in 0..1, the bearing pair in -1..1. */
	enum EObservation : int32
	{
		ObsHealth,
		ObsClip,
		ObsAmmo,
		ObsHasReserve,
		ObsInCover,
		ObsCoverQuality,
		ObsCoverAvailable,
		ObsExposed,
		ObsCrouching,
		ObsProne,
		ObsMoving,
		ObsUnderFire,
		ObsSuppressed,
		/** A line of fire to a real threat. */
		ObsFireLine,
		/** The launcher rule's best target score; 0 without a launcher. */
		ObsLauncherScore,
		ObsAtPost,
		ObsHasDefencePost,
		/** His distance to the point over ObjectiveDistanceScaleCm (1 without an objective). */
		ObsObjectiveDistance,
		ObsOnPoint,
		/** Inside the defended area (the roof's spot span / the ground radius x area_factor). */
		ObsInsideZone,
		/** A real threat is known (not the attackers' stand-in point). */
		ObsThreatKnown,
		ObsThreatSeen,
		/** Its distance over ThreatDistanceScaleCm (1 when none). */
		ObsThreatDistance,
		ObsThreatInRange,
		/** Seconds since it was seen over ThreatAgeScaleSeconds (0 seen now, 1 when none). */
		ObsThreatAge,
		/** The threat's bearing off his line to the point: cos / sin of the planar angle fighter -> point vs fighter -> threat (0 / 0 without either). */
		ObsThreatBearingCos,
		ObsThreatBearingSin,
		/** How far he stands outside the seen enemies' look sectors (0.5 unknown). */
		ObsAttentionOff,
		ObsEnemiesKnown,
		ObsEnemiesSeen,
		/** The living mates besides him over MatesScale. */
		ObsMatesAlive,
		ObsMatesReloading,
		ObsMatesSuppressing,
		ObsContact,
		ObsShotsExchanged,
		/** Seconds since the last shot between the sides over QuietScaleSeconds. */
		ObsQuiet,
		ObsTimeFrac,
		ObsHoldFrac,
		ObsEnemyOnPoint,
		/** The first of ActionCount one-hot values: the order he is running (all 0: none, or one outside the action set). */
		ObsRunningOrder,
		ObsCount = ObsRunningOrder + ActionCount
	};
	inline constexpr int32 ObservationSize = ObsCount;

	inline constexpr float ObjectiveDistanceScaleCm = 5000.f;
	inline constexpr float ThreatDistanceScaleCm = 10000.f;
	inline constexpr float ThreatAgeScaleSeconds = 10.f;
	inline constexpr float QuietScaleSeconds = 30.f;
	inline constexpr float EnemiesScale = 6.f;
	inline constexpr float MatesScale = 5.f;
	inline constexpr float MatesBusyScale = 3.f;

	/**
	 * Fills Out (ObservationSize values) from one fighter's view and his squad view. AreaFactor = the team's area_factor (the defended
	 * area's radius fallback while the subsystem measured none).
	 */
	CODEXTACTICS_API void BuildObservation(const FPmcFighterView& Fighter, const FPmcSquadView& Squad, TArrayView<float> Out, float AreaFactor = 2.f);

	// --- Rewards ---

	/** The reward of one step: kills + own death + the round's end + the seconds an attacker stood on the point. */
	CODEXTACTICS_API float StepReward(const FPmcPolicyEvents& Events, const FPmcPolicyRewards& Rewards = FPmcPolicyRewards());

	/**
	 * The round's end for the defending side (bDefenderIsA: side A defends): won = it is the winner; captured = the attackers took
	 * the point (FPmcRoundOutcome::ObjectiveResult "captured"). A draw / a lost elimination is neither (the deaths already cost).
	 */
	CODEXTACTICS_API void OutcomeEvents(const FPmcRoundOutcome& Outcome, bool bDefenderIsA, bool& bOutRoundWon, bool& bOutPointCaptured);
}
