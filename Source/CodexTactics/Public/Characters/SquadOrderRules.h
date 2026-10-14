#pragma once

#include "CoreMinimal.h"

/**
 * Who owns an operative's movement: his own order or the squad formation (rev 5g, game designer play-test 2026-10-14, UE-only;
 * Godot had no rule for it - every non-leader simply followed). USquadSubsystem asks these pure rules on a leader switch, on
 * Solo on / off and in every formation repath.
 *
 * - An operative keeps executing the order he was given (a move, a walk-up to use an object, a push / deploy task) whichever
 *   operative is selected afterwards, in Solo mode and in group (formation) mode. Only a new order to HIM, Regroup / Solo off,
 *   or game events (knockdown, panic, rage, a combat reaction, a level obstacle holding him) change it.
 * - Group mode: the old leader who still walks to his ordered goal finishes it and only then joins the new leader's formation.
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

	// Regroup / Solo off ends the followers' own MOVE orders (AOperativeCharacter::ReleaseOwnMoveOrder): the formation takes
	// everybody back at once; a running use / object task still finishes first.
}
