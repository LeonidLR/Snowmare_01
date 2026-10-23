#pragma once

#include "CoreMinimal.h"
#include "Ladders/LadderTypes.h"

/**
 * Pure ladder rules (docs/design/LADDERS.md): geometry (quantised height, anchor points, rail tiling), the climb state table,
 * the loop phase with rung-index snapping, yaw, limb grip windows for the rung IK, refusals and the fall damage.
 * No UObject: unit tested in CodexTactics.Ladder.Rules.* / CodexTactics.Ladder.Climb.*. UE-only system (no Godot reference).
 *
 * Actor frame (LADDERS.md 3.1): origin = bottom centre of the rung plane on the floor, +X away from the wall (towards the
 * climber), +Z up. The climber hangs facing -X.
 */
namespace LadderRules
{
	/** The Fixed clip contract (LADDERS.md section 4; re-measured in UE by LadderClipShots 2026-10-22). */
	struct FClipContract
	{
		/** Rung pitch: the up loop climbs exactly two rungs. */
		float Pitch = 36.4f;
		/**
		 * ClimbOutTop starts with the feet this many rungs below the platform. Re-measured 2026-10-23 (user bug "at the very end he sinks
		 * halfway into the object"): the clip steps onto a top 5 rungs up (its left foot planted at 184 cm from 2.4 s, 5 x 36.4 = 182), then
		 * lifts the whole body ~74 cm more in its last 0.43 s (root and feet alike - the course's "fix"); that lift is taken out of the root
		 * track (ClimbOutLiftStart), so the root rises 185 cm, not 261, and the feet stay on the platform.
		 */
		int32 ClimbOutRungs = 5;
		/** From this clip time the ClimbOutTop's feet rise only with the clip's own lift (both planted / stepping up): removed from the track, s. */
		float ClimbOutLiftStart = 2.8f;
		/** IntoFromTop lands the loop pose this many rungs below the platform. */
		int32 IntoFromTopRungs = 5;
		/** Fewer rungs = a decoration ladder (user decision 2026-10-22). */
		int32 MinClimbableRungs = 7;
		/** Capsule axis in front of the rung plane while hanging (hands 46.8 cm ahead of the root + 2 cm to the bar centre). */
		float HangStandoff = 48.2f;
		/** Capsule axis to the actor's +Y side of the ladder centre (the clips hold the body 7 cm left of the ladder centre). */
		float HangLateral = 7.f;
		/** The bottom link point stands this much further out than the hang line (the walk-in). */
		float BottomLinkExtra = 20.f;
		/** The top link point / IntoFromTop start: behind the rung plane (clip: 97.3 back - 48.2). */
		float TopStandoff = 49.1f;
		/** Where ClimbOutTop ends behind the rung plane (the clip ends at 29.3; warped a bit further onto the platform). */
		float TopExitStandoff = 40.f;
	};

	/** Fall damage on landing after a knockdown on the ladder (user decision 2026-10-22). */
	struct FFallDamage
	{
		float SafeHeightCm = 100.f;
		float DamagePerCm = 0.1f;
		float MaxDamage = 40.f;
	};

	// --- Geometry ------------------------------------------------------------------------------------------------------

	/** Rungs of a ladder of Height: max(1, round(Height / Pitch)); 0 for a bad pitch. */
	CODEXTACTICS_API int32 RungCountForHeight(float Height, float Pitch);

	/** Platform floor above the ladder's floor: RungCount * Pitch. */
	CODEXTACTICS_API float TopZ(int32 RungCount, float Pitch);

	/** A climbable ladder: enough rungs and a top exit (else a decoration ladder: no nav link, no climb). */
	CODEXTACTICS_API bool IsClimbable(int32 RungCount, bool bTopExit, int32 MinClimbableRungs = 7);

	/** Rung K centre in actor space (K = 1..RungCount; K = 0 is the floor). */
	CODEXTACTICS_API FVector RungLocal(int32 K, float Pitch);

	/** The hang line (capsule axis) at feet height Z, actor space. */
	CODEXTACTICS_API FVector HangLocal(const FClipContract& Clip, float Z);

	/** Path start / end below and above, actor space (feet height). */
	CODEXTACTICS_API FVector BottomLinkLocal(const FClipContract& Clip);
	CODEXTACTICS_API FVector TopLinkLocal(const FClipContract& Clip, int32 RungCount);

	/** Where ClimbOutTop ends (feet on the platform), actor space. */
	CODEXTACTICS_API FVector TopExitLocal(const FClipContract& Clip, int32 RungCount);

	/**
	 * Rail segments to tile TopZ with segments of SegmentLen plus Overshoot segments above it: ceil(TopZ / SegmentLen) + Overshoot.
	 * INDEX_NONE when SegmentLen < 1 cm or Pitch < 10 cm (the generator logs and draws nothing).
	 */
	CODEXTACTICS_API int32 RailSegmentCount(float TopZ, float SegmentLen, int32 Overshoot, float Pitch);

	/** Bracket heights: Count evenly spaced between the first rung and TopZ (none without a wall / Count <= 0). */
	CODEXTACTICS_API TArray<float> BracketHeights(int32 Count, float Pitch, float TopZ);

	// --- Yaw -----------------------------------------------------------------------------------------------------------

	/** On the ladder the climber always faces the wall: LadderYaw + 180, normalised. */
	CODEXTACTICS_API float ClimberYaw(float LadderYaw);

	/** IntoFromTop arrival: facing the drop (LadderYaw) within Tolerance degrees needs no pre-turn. */
	CODEXTACTICS_API bool NeedsPreTurn(float CharacterYaw, float LadderYaw, float ToleranceDeg = 10.f);

	// --- The climb state table (LADDERS.md 5.1) -------------------------------------------------------------------------

	/** The next state for Event in From; From itself when the event does not apply there. */
	CODEXTACTICS_API ELadderClimbState Transition(ELadderClimbState From, ELadderClimbEvent Event);

	/** The climber is on the ladder (or moving onto / off it): everything but None / Queue. */
	CODEXTACTICS_API bool IsOnLadderState(ELadderClimbState State);

	/** Deadlock guard: a state may last at most 2x its expected length (+ 1 s); <= 0 = no limit. */
	CODEXTACTICS_API float StateTimeout(float ExpectedSeconds);

	// --- Loop phase and rung snapping -----------------------------------------------------------------------------------

	/**
	 * Where the climber is on the ladder: Rung = the rung his left foot stands on at the loop's start pose (0 = the floor;
	 * -1 with Time = half a loop for an odd remainder), Time = the up-loop clip time in [0, LoopLength).
	 */
	struct FLoopPos
	{
		int32 Rung = 0;
		float Time = 0.f;

		bool operator==(const FLoopPos& Other) const { return Rung == Other.Rung && FMath::IsNearlyEqual(Time, Other.Time, 1e-3f); }
	};

	/** Where ClimbOutTop starts: the loop start pose RungCount - ClimbOutRungs. */
	CODEXTACTICS_API FLoopPos ClimbOutStart(const FClipContract& Clip, int32 RungCount);

	/** Where IntoFromTop lands: the loop start pose RungCount - IntoFromTopRungs. */
	CODEXTACTICS_API FLoopPos IntoFromTopEnd(const FClipContract& Clip, int32 RungCount);

	/**
	 * The bottom pose of a ladder whose loop target is TargetRung: rung 0 at time 0 when TargetRung is even, else rung -1 at half a
	 * loop (the one-rung remainder is the first half of the loop: the target is always reached on a loop boundary).
	 */
	CODEXTACTICS_API FLoopPos BottomPos(int32 TargetRung, float LoopLength);

	/**
	 * Advances Pos by Delta seconds of clip time in Direction (+1 up, -1 down), wrapping the loop (two rungs per loop). Stops
	 * exactly on Target (returns true then) when it lies on the way.
	 */
	CODEXTACTICS_API bool Advance(FLoopPos& Pos, float Delta, int32 Direction, const FLoopPos& Target, float LoopLength);

	/**
	 * Where a turn-round mid-climb happens (a new order behind him, 2026-10-23): the next rung boundary (a loop start or its half) from Pos in
	 * Direction (+1 up, -1 down); Pos itself counts only when it is not on a boundary already.
	 */
	CODEXTACTICS_API FLoopPos NextRungBoundary(const FLoopPos& Pos, int32 Direction, float LoopLength);

	/** Pos lies at / past Limit going in Direction (along the ladder). */
	CODEXTACTICS_API bool IsBeyond(const FLoopPos& Pos, const FLoopPos& Limit, int32 Direction, float LoopLength);

	/**
	 * Which end of the ladder serves a destination (a new order mid-climb): the one with the shorter way there (the navmesh path lengths from
	 * the two link points; < 0 = unreachable from that end). True = the top. Equal / both unreachable: the one he heads to now.
	 */
	CODEXTACTICS_API bool PreferTopEnd(float FromTopCm, float FromBottomCm, bool bGoingUp);

	/**
	 * Feet height over the ladder's floor at Pos: Rung * Pitch + 2 * Pitch * Fraction, Fraction = the root track's rise share at
	 * Pos.Time (0 at the loop start, 1 at its end) - every loop boundary lands exactly on a rung (rung-index snapping).
	 */
	CODEXTACTICS_API float HeightAt(const FLoopPos& Pos, float Fraction, float Pitch);

	// --- Rung IK grip windows (measured on the up loop, 54 frames, LadderClipShots CONTACT lines 2026-10-22) -------------

	struct FLimbGrip
	{
		/** Rung index (relative to FLoopPos::Rung) the limb holds, valid when Alpha > 0. */
		int32 RungOffset = 0;
		/** 0 = free (moving to the next rung), 1 = on the rung. */
		float Alpha = 0.f;
	};

	/** The grip of Limb at the up loop's normalised time (0..1): rung offset and IK alpha (eased 5-frame = ~0.17 s fades). */
	CODEXTACTICS_API FLimbGrip LimbGrip(ELadderLimb Limb, float NormalizedTime);

	// --- Entry, interrupts, falls ---------------------------------------------------------------------------------------

	struct FEntryContext
	{
		bool bTurnBased = false;
		bool bCarrying = false;
		bool bMounted = false;
		bool bKnockedDown = false;
		bool bDead = false;
		bool bClimbable = true;
		bool bHasClips = true;
		bool bLeasedByOther = false;
		bool bStanding = true;
	};

	/** Why he may not start the climb now (None = go; Leased / MustStand = wait at the link). */
	CODEXTACTICS_API ELadderRefusal CanStartClimb(const FEntryContext& Context);

	/**
	 * An enemy may climb (LADDERS.md 7.5, user decision 2026-10-22): its class climbs (the non-mutant humans), its body has the clips
	 * (else a warning and it paths around) and the ladder admits enemies (ALadderActor::bAllowEnemies). Mutants never.
	 */
	CODEXTACTICS_API bool EnemyMayClimb(bool bClassClimbs, bool bHasClips, bool bLadderAllowsEnemies);

	/**
	 * Turn-based (LADDERS.md 7.6): a climber who joins the grid off his ladder stays where it left him when he stands on an upper floor
	 * (feet more than ToleranceCm over the grid's ground - the one-level grid would snap him over a roof's edge); else he takes a cell.
	 */
	CODEXTACTICS_API bool JoinsOnUpperFloor(float FeetZ, float GridGroundZ, float ToleranceCm = 60.f);

	/** Abort placement: the top end when the climber is above half way (Rung > RungCount / 2), else the bottom. */
	CODEXTACTICS_API bool NearestEndIsTop(int32 Rung, int32 RungCount);

	/** Fall damage for a drop of HeightCm: clamp((H - Safe) * PerCm, 0, Max). */
	CODEXTACTICS_API float FallDamage(float HeightCm, const FFallDamage& Rule = FFallDamage());
}
