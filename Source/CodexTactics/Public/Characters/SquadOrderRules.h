#pragma once

#include "CoreMinimal.h"
#include "GameFlow/GameFlowTypes.h"

/**
 * Who owns an operative's movement: his own order or the squad formation (rev 5g, game designer play-test 2026-10-14, UE-only;
 * Godot had no rule for it - every non-leader simply followed). USquadSubsystem asks these pure rules on a leader switch, on
 * Solo on / off and in every formation repath.
 *
 * - An operative keeps executing the order he was given (a move, a walk-up to use an object, a push / deploy task) whichever
 *   operative is selected afterwards, in Solo mode and in group (formation) mode. Only a new order to HIM, Regroup / Solo off,
 *   or game events (knockdown, panic, rage, a combat reaction, a level obstacle holding him) change it.
 * - Group mode: the old leader who still walks to his ordered goal finishes it and only then joins the new leader's formation.
 * - Group mode: a follower hugging a wall leaves the cover and follows once the leader walks away from him (Solo mode, guards
 *   and the fight keep their own rules: Solo holds, guards are no followers, the formation is off in a fight).
 */
namespace SquadOrderRules
{
	/** What an operative is busy with right now (each a separate system). */
	struct FOwnOrderFacts
	{
		/** A move order of his own is running (AOperativeCharacter::HasOwnMoveOrder: player / autonomy / interaction walk). */
		bool bOwnMoveOrder = false;
		/** A use order (walk up to an object and use it: light, defuse, open) is running for him (UInteractionSubsystem). */
		bool bUseOrder = false;
		/** A relocation / deploy / tripwire task is running for him (URelocationSubsystem). */
		bool bObjectTask = false;
		/**
		 * A fire order of his own is running: suppressive fire or a quick-fire burst (user decision 2026-10-26: a member given an attack
		 * order in the pause carries it out, then rejoins the formation).
		 */
		bool bFireOrder = false;
	};

	/** He executes an order of his own: the formation leaves him alone until it is done. */
	CODEXTACTICS_API bool IsOnOwnOrder(const FOwnOrderFacts& Facts);

	/**
	 * Leader switch (a selection): the operative who becomes the leader is stopped only when he was walking a formation move
	 * (he now waits for the player's orders); an order of his own runs on.
	 */
	CODEXTACTICS_API bool ShouldStopOnBecomingLeader(bool bOnOwnOrder);

	/**
	 * Solo mode (on, or a leader switch while it is on): the followers stop and crouch where they are - except one executing an
	 * order of his own: he finishes it first and then holds where it ended.
	 */
	CODEXTACTICS_API bool ShouldHoldNow(bool bOnOwnOrder);

	/**
	 * Bug 2026-10-20 (game designer, lake demo: after the hut lesson Regroup, then the Commander RUNS - the two others followed in formation
	 * but WALKED; Solo on / off again fixed it). Regroup gives the followers the leader's stance of that moment (often crouched: the Solo
	 * guard crouch), and a crouched follower cannot copy the leader's sprint while the leader's own sprint order stood HIM up. A follower
	 * whose current stance the SQUAD gave him (Regroup, Solo's guard crouch, a tutorial confinement's copy - not a stance key) stands up to
	 * sprint with the sprinting leader; one the player crouched keeps his stance (user decision 2026-10-01: no copy of the leader's stance).
	 * bCanSprint: nothing but the stance keeps him from sprinting (cold, wounds, a carried load).
	 */
	CODEXTACTICS_API bool ShouldStandToSprintWithLeader(bool bLeaderSprinting, bool bFollowerCrouching, bool bStanceFromSquad, bool bCanSprint);

	// Regroup / Solo off ends the followers' own MOVE orders (AOperativeCharacter::ReleaseOwnMoveOrder): the formation takes
	// everybody back at once; a running use / object task still finishes first.

	/** A follower in cover leaves it once his formation slot is this far from him while the leader walks, cm. */
	constexpr float CoverLeaveSlotDistanceCm = 400.f;

	/** The situation of one formation follower who stands in cover. */
	struct FCoverFollowFacts
	{
		/** He hugs a wall (AOperativeCharacter::IsInCover). */
		bool bInCover = false;
		/** He still walks to his own order (e.g. the cover order itself): nothing changes until it is done. */
		bool bOnOwnOrder = false;
		/** Solo mode / a camera-zone perimeter hold: the followers hold where they are. */
		bool bFollowersHolding = false;
		/** Guard (T): he holds his spot (guards are no formation followers anyway). */
		bool bGuarding = false;
		/** The exploration formation is on (off in preparation / any fight: the autonomy / ROE rules decide there). */
		bool bFormationActive = true;
		/** The leader walks (a move order or a visible velocity). */
		bool bLeaderMoving = false;
		/**
		 * The leader is in cover or walks to a cover slot: the squad is stacking up (e.g. two at a door), the ones already in
		 * cover stay.
		 */
		bool bLeaderTakingCover = false;
		/** Distance from him to his formation slot around the leader, cm. */
		float DistanceToSlotCm = 0.f;
	};

	/** Group mode: the follower leaves the cover (the cover-exit to walk) and follows the leader in formation. */
	CODEXTACTICS_API bool ShouldLeaveCoverToFollow(const FCoverFollowFacts& Facts, float LeaveDistanceCm = CoverLeaveSlotDistanceCm);

	/**
	 * Group cover order (game designer 2026-10-20, reverses the 2026-10-11 "no automatic cover stacking" for GROUP orders): who
	 * goes along when the player orders the direct-order operative (the leader) into cover.
	 */
	struct FGroupCoverJoinFacts
	{
		/** He is the one the order was given to (he takes the clicked slot himself). */
		bool bDirectOrder = false;
		/** Alive and not knocked out of the squad (a knocked-down one is fine: the order waits until he is up). */
		bool bAlive = true;
		/** Two or more operatives are box-selected (USquadSubsystem::HasMultiSelection). */
		bool bMultiSelection = false;
		/** He is one of them. */
		bool bSelected = false;
		/** Solo mode is on (the followers hold where they are). */
		bool bSoloMode = false;
		/** The exploration formation is on (off in preparation / any fight). */
		bool bFormationActive = true;
		/** He is a formation follower (not the leader, not a guard). */
		bool bFollower = false;
		/** He executes an order of his own (IsOnOwnOrder): in group mode he finishes it (a box selection includes him anyway). */
		bool bOnOwnOrder = false;
		/** Aboard / boarding a vehicle, or held by a level obstacle (bridge, fence queue). */
		bool bUnavailable = false;
	};

	/**
	 * He joins: a box selection of two or more -> every selected one; else group mode (Solo off, the formation on) -> every
	 * formation follower who is not busy with an order of his own. Never the direct-order operative himself, the dead, or one
	 * aboard / held. Solo with a single selection, a fight without a box selection: nobody (only the direct order, as before).
	 */
	CODEXTACTICS_API bool ShouldJoinGroupCover(const FGroupCoverJoinFacts& Facts);

	/**
	 * Which order a cover click is: a GROUP cover order when at least one operative joins it (ShouldJoinGroupCover), else the
	 * single cover order of the direct-order operative.
	 */
	CODEXTACTICS_API bool IsGroupCoverOrder(int32 JoiningCount);

	/**
	 * Game designer play-test 2026-10-22 ("when a fight ends the operatives must stay where they were"): the phase change that
	 * ends a fight and returns to exploring (PostCombat after the last wave / an ambush fight, or a skirmish straight back to
	 * Exploration) arms the post-fight hold - the formation does not walk the followers to their slots.
	 */
	CODEXTACTICS_API bool ArmsPostFightHold(ECodexGamePhase Previous, ECodexGamePhase Phase);

	/**
	 * The post-fight hold stays until the leader moves (the player's first move order); then the formation follows as usual.
	 * Bug 2026-10-23 (lake, after the horde: "even pressing B he does not return to the formation"): Regroup (B / Solo off) is an
	 * order to come back - it ends the hold at once (bRegroup).
	 */
	CODEXTACTICS_API bool KeepsPostFightHold(bool bHold, bool bLeaderMoving, bool bRegroup = false);

	/**
	 * The exploration formation runs (followers walk to their slots, Solo / Regroup, group cover, the post-fight hold): while exploring
	 * and after the final fight. Bug 2026-10-23: PostCombat lasts until the level ends (nothing returns the flow to Exploration), and
	 * the formation was off there - after the horde no follower ever walked again, B toggled Solo without effect. Godot
	 * _return_squad_to_prep_stations cleared every member's is_tactical_mode at the fight's end (the formation followed again).
	 * bPostCombatFormation false = the old rule (console Codex.Squad.PostCombatFormation 0, A/B).
	 */
	CODEXTACTICS_API bool IsFormationPhase(ECodexGamePhase Phase, bool bPostCombatFormation = true);

	/**
	 * User decision 2026-10-26 (game designer: "in combat the soldiers do NOT return to the formation, B does nothing - until I give the
	 * order to switch to solo mode, they must stay in formation"): formation FOLLOWING runs in the fight as well - the cutscene, the
	 * preparation / the rest between waves, the wave fight in real time and in the tactical pause, the wave-cleared panel - not on the
	 * turn-based grid (it moves everyone itself) and not after a game over. Root cause of the report: the old rule (IsFormationPhase,
	 * Godot main.gd is_tactical_mode = individual control from the preparation to the fight's end; TANDEM Sprint 03 "formation only in
	 * exploration") switched the formation off for the whole fight, so Solo on / off (B) had no visible effect there.
	 * bCombatFormation false = that old rule (console Codex.Squad.CombatFormation 0, A/B). A spotted trap's Solo keeps IsFormationPhase;
	 * the group cover order follows IsGroupCoverPhase.
	 */
	CODEXTACTICS_API bool IsFollowPhase(ECodexGamePhase Phase, ECodexCombatMode Mode, bool bPostCombatFormation = true, bool bCombatFormation = true);

	/**
	 * User decision 2026-10-26 ("group cover in combat: YES"): the GROUP cover order without a box selection (the others take cover too, the
	 * formation wall layout, the wall group, the corner stack / slide along the wall) runs wherever the formation follows
	 * (IsFollowPhase: exploring, after the final fight, and in the fight - real time and the tactical pause; never on the turn-based
	 * grid). It was exploration-only (IsFormationPhase): in a fight a cover click stayed the single order of the selected operative.
	 * bGroupCoverInCombat false = that old rule (console Codex.GroupCover.InCombat 0, A/B). Solo with a single selection stays the
	 * single order everywhere (ShouldJoinGroupCover).
	 */
	CODEXTACTICS_API bool IsGroupCoverPhase(ECodexGamePhase Phase, ECodexCombatMode Mode, bool bPostCombatFormation = true, bool bCombatFormation = true,
		bool bGroupCoverInCombat = true);
}
