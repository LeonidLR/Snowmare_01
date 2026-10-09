#pragma once

#include "CoreMinimal.h"
#include "Characters/OperativeMovementRules.h"

/**
 * Sniper rifle handling (user request 2026-10-09; UE-only, no Godot reference): the Medic-Sapper's extra weapon fires only
 * kneeling (crouched) or prone and standing still. An order to shoot while standing / moving makes her stop, kneel by
 * herself, then fire; prone stays prone. Turn-based: the kneel costs the normal stance AP on top of the shot.
 * Runtime: AOperativeCharacter::PrepareSniperShot, UTurnBasedCombatSubsystem::ResolveAttackCell, UOperativeAnimInstance
 * sniper layer (OperativeAnimInstanceSniper.cpp).
 */

/** What the operative must do before a sniper shot can leave the barrel. */
enum class ESniperFireStep : uint8
{
	/** Kneeling / prone, still, no stance clip playing: fire. */
	Ready,
	/** Walking under an automatic (fire posture) target: no shot and no stop - the player's move order wins. */
	WaitForMove,
	/** A direct order (priority target, Ctrl + click, planned shot) while moving: stop first. */
	Stop,
	/** Standing still: kneel by herself (stance change), then fire. */
	Kneel,
	/** The kneel / prone clip is still playing: the shot waits for it. */
	Settle
};

/** Inputs of the sniper fire step (gathered by the operative). */
struct FSniperFireContext
{
	EOperativeStance Stance = EOperativeStance::Standing;
	/** A move order runs or the body still moves (AOperativeCharacter::IsMoving). */
	bool bMoving = false;
	/** A stance transition clip plays (the body is going down). */
	bool bStanceTransitionPlaying = false;
	/** The player ordered this shot (priority target, Ctrl + click, planned / turn-based shot); false = automatic fire. */
	bool bDirectOrder = false;
};

namespace SniperRules
{
	/** Kneeling (crouched) or prone: the only stances the rifle fires from. */
	CODEXTACTICS_API bool CanFireInStance(EOperativeStance Stance);

	/** The rifle fires now: allowed stance and not moving. */
	CODEXTACTICS_API bool CanFireNow(EOperativeStance Stance, bool bMoving);

	/** The stance she takes to fire: standing -> crouching (kneel); crouching / prone stay. */
	CODEXTACTICS_API EOperativeStance FiringStance(EOperativeStance Current);

	/** The next step before the shot (stop / kneel / settle / fire); see ESniperFireStep. */
	CODEXTACTICS_API ESniperFireStep NextStep(const FSniperFireContext& Context);

	/** Turn-based cost of a sniper attack: the shot, plus the kneel when standing. */
	CODEXTACTICS_API int32 TurnBasedAttackCost(EOperativeStance Stance, int32 StanceApCost, int32 AttackApCost);

	/** Enough AP for the kneel (when standing) and the shot. */
	CODEXTACTICS_API bool CanAffordTurnBasedAttack(int32 ApLeft, EOperativeStance Stance, int32 StanceApCost, int32 AttackApCost);

	/** The stance the Commander Mode autonomy may set for a sniper: never standing (it would only stand her up to kneel again). */
	CODEXTACTICS_API EOperativeStance AutonomyStance(EOperativeStance Desired, bool bSniper);

	/** Index of the per-stance sniper clip arrays (0 stand, 1 knee, 2 prone). */
	CODEXTACTICS_API int32 ClipIndex(EOperativeStance Stance);

	/** A bolt is worked after a shot while rounds remain in the magazine (an empty one is reloaded instead). */
	CODEXTACTICS_API bool ShouldCycleBolt(int32 RoundsLeftAfterShot);

	/** Play rate that fits a clip into Seconds (a reload stretched to the weapon's reload time); 1 without a length. */
	CODEXTACTICS_API float PlayRateToFit(float ClipSeconds, float Seconds);
}
