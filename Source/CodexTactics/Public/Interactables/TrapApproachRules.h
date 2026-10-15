#pragma once

#include "CoreMinimal.h"

/**
 * Walking up to a trap to disarm it (game designer 2026-10-15, every level; UE-only). The operative used to stop at the object
 * box's interaction range (a mine: its 1.6 m trigger box + 1.1 m = ~2.7 m from the charge; a wire: 1.5 m) - far too far for the
 * disarm and the pickup clip. Now he walks to a point StandOffCm from the trap - offset from the charge TOWARDS him (never onto the
 * mine itself), for a wire from the closest point of the wire line towards his side - faces it, and the menu opens once he is
 * within StandOffCm + 10 cm while walking in (StandOffCm + ReachToleranceCm once stopped; the walk's goal lies GoalInsetCm inside
 * the stand-off because the move ends within its 25 cm acceptance radius: he arrives at ~70 cm). Values per instance / Blueprint:
 * AInteractableActor::DisarmStandOffCm / DisarmReachToleranceCm. Tests: CodexTactics.Interactables.TrapApproach.*.
 */
namespace TrapApproachRules
{
	/** Default stand-off: the operative's root this far from the charge / the wire line, cm. */
	inline constexpr float DefaultStandOffCm = 60.f;
	/** The disarm spot counts as reached within the stand-off + this, cm (MovementConfig.AcceptanceRadius 25 + slack). */
	inline constexpr float DefaultReachToleranceCm = 25.f;
	/** A mine's defuser keeps his lease until he is this far outside its trigger radius, cm. */
	inline constexpr float DefuserLeaseExitMarginCm = 30.f;

	/** Planar point StandOffCm from Trap towards From (From on the trap: +X). */
	CODEXTACTICS_API FVector2D MineApproachPoint(const FVector2D& Trap, const FVector2D& From, float StandOffCm);

	/** Closest point of the wire segment A-B to From. */
	CODEXTACTICS_API FVector2D ClosestPointOnWire(const FVector2D& A, const FVector2D& B, const FVector2D& From);

	/**
	 * Planar point StandOffCm from the wire A-B on From's side: the closest wire point pushed towards From (From on the line: the
	 * wire's left normal).
	 */
	CODEXTACTICS_API FVector2D WireApproachPoint(const FVector2D& A, const FVector2D& B, const FVector2D& From, float StandOffCm);

	/**
	 * The walk's goal lies this much closer than the stand-off, cm: the move ends within its 25 cm acceptance radius of the goal, so
	 * he arrives at about the stand-off + 10 (a wire goal at 45 cm still keeps a 40 cm capsule off the wire).
	 */
	inline constexpr float GoalInsetCm = 15.f;
	/** While still walking the menu waits until he is within the stand-off + this, cm (stopped: within the full tolerance). */
	inline constexpr float WalkingReachCm = 10.f;

	/** Distance of the walk's goal from the charge / the wire line, cm (StandOffCm - GoalInsetCm, >= 0). */
	CODEXTACTICS_API float GoalDistance(float StandOffCm);

	/**
	 * He stands close enough to work on the trap: DistanceCm (to the charge / the wire line) <= stand-off + tolerance once he stopped,
	 * <= stand-off + WalkingReachCm while he still walks in (so the menu does not stop him at the outer edge).
	 */
	CODEXTACTICS_API bool IsAtDisarmSpot(float DistanceCm, float StandOffCm, float ToleranceCm, bool bStillWalking = false);

	/**
	 * Mine trigger immunity of the operative walking up to defuse it (rev 5j): he is ignored while he is the approaching defuser
	 * and, once inside the trigger radius under that protection, until he steps out of it again (TriggerRadius + exit margin) -
	 * the menu, the crouched work, a slip and the walk away happen inside the radius now. Returns the new lease.
	 */
	CODEXTACTICS_API bool UpdateDefuserLease(bool bHasLease, bool bIsApproachingDefuser, float DistanceCm, float TriggerRadiusCm,
		float ExitMarginCm = DefuserLeaseExitMarginCm);
}
