#pragma once

#include "CoreMinimal.h"

/**
 * Two-corner hold at a wall (#57, user request 2026-11-07: "the squad pressed against a wall, a peek on the right sends the right man to that
 * corner; then a peek on the left sends the left man there - but the right man leaves his position. Each of them must hold his own edge. The
 * one left in the middle should scan from the left corner to the right"). Formation mode, the squad's wall-leadership corner peek: a later
 * peek at the OTHER end of the same wall sends that end's man there while the first corner man keeps his corner peek; the members between
 * them stay at the wall and sweep their look from one corner to the other and back (towards the corners along the wall, never into it). UE-only (Godot reference: none).
 * Pure; tests CodexTactics.Formation.CornerHold.*. Runtime: USquadSubsystem (SquadSubsystemCornerHold.cpp).
 */
namespace FormationCornerHold
{
	/** A peek order whose end lies within this of a held corner's end is that corner again, cm. */
	inline constexpr double SameEndCm = 150.0;

	/** A middle man's sweep ends never turn further than this off the wall's outward normal (along the wall at most: never into it), deg. */
	inline constexpr float MaxSweepOffOutwardDeg = 85.f;

	/** What a squad corner peek order is, with a corner already held at the wall. */
	enum class EPeekOrder : uint8
	{
		/** Today's wall-leadership peek (the man nearest the corner takes it and the lead, the others stack behind him). */
		Ordinary,
		/** The OTHER end of the same wall: its end man takes it (and the lead), the first corner man keeps his corner, the rest stay. */
		SecondCorner,
		/** An end already held (by the peeker or the held corner man): nothing changes - nobody swaps. */
		AlreadyHeld,
	};

	struct FPeekOrderFacts
	{
		/** Codex.Formation.TwoCornerHold is on. */
		bool bEnabled = true;
		/** The squad's wall-leadership corner peek is in force (the peek order is not pending any more: its man is at his corner, peeking). */
		bool bPeekerAtCorner = false;
		/** The new peek's corner slot lies on that peeker's wall. */
		bool bSameWall = false;
		/** The new peek's end to the peeker's end, cm. */
		double ToPeekEdgeCm = 0.0;
		/** A second corner is held already (the two-corner hold is on), and the new end to that corner's end, cm. */
		bool bHeldCorner = false;
		double ToHeldEdgeCm = 0.0;
	};

	/** The kind of a squad corner peek order (see EPeekOrder). */
	CODEXTACTICS_API EPeekOrder DecidePeekOrder(const FPeekOrderFacts& Facts, double SameEnd = SameEndCm);

	/**
	 * The ping-pong sweep, as a fraction of the arc between the two corners (0 = the first corner's look, 1 = the second's): one way takes
	 * SweepSeconds, eased at the ends (cosine). With Count middle men each sweeps his own slice of the arc (Index 0 nearest the first corner)
	 * in step with the others, so two of them never look at the same point; one middle man sweeps the whole arc. Index outside 0..Count-1 is
	 * clamped; SweepSeconds <= 0 holds the middle of his slice.
	 */
	CODEXTACTICS_API float SweepFraction(float Seconds, float SweepSeconds, int32 Index, int32 Count);

	/**
	 * The look yaw at Fraction of the arc from FromYaw to ToYaw that passes through OutwardYaw (the wall's outward normal: the sweep never
	 * turns through the wall), deg in (-180, 180].
	 */
	CODEXTACTICS_API float SweepYaw(float FromYaw, float ToYaw, float OutwardYaw, float Fraction);

	/**
	 * A sweep end for a middle man: Yaw (his look towards a corner along the wall) clamped to at most MaxOffDeg either side of OutwardYaw
	 * (the corner men's own peek looks round the ends - behind the wall from where he stands; his ends are the corners themselves), deg.
	 */
	CODEXTACTICS_API float ClampOffOutward(float Yaw, float OutwardYaw, float MaxOffDeg = MaxSweepOffOutwardDeg);

	/**
	 * The held corner man still holds his corner: alive, on his feet, in cover at that wall, peeking. A corner man who dies / is knocked down /
	 * leaves frees his corner only (nobody takes it without a new order).
	 */
	CODEXTACTICS_API bool StillHolds(bool bAlive, bool bKnockedDown, bool bInCoverAtThatWall, bool bPeeking);

	// --- The game designer's answers 2026-11-08 (#57 open questions 1, 2, 3 and 5) ---

	/** The squad's alert state for the middle men's sweep (answer 3, "6 s, option V") - the squad's existing combat / alert facts. */
	struct FSweepAlertFacts
	{
		/** An enemy seen or heard by the squad (its known threats: sight / hearing, within bypass.threat_memory_s). */
		bool bThreatKnown = false;
		/** A squad member under fire (his under-fire threat is live). */
		bool bUnderFire = false;
		/** A squad member suppressed (a suppression tier above none). */
		bool bSuppressed = false;
	};

	/**
	 * Answer 3 (2026-11-08, "6 s, option V"): the one-way sweep between the two held corners runs SLOWLY out of combat (CalmSeconds,
	 * squad_walls.json corner_sweep_calm_s 12) and at AlertSeconds (corner_sweep_s 6) in combat / alert (any of the facts). A duration <= 0
	 * falls back to the other one; both <= 0 -> 0 (SweepFraction then holds the middle of his slice).
	 */
	CODEXTACTICS_API float SweepSecondsFor(const FSweepAlertFacts& Facts, float CalmSeconds, float AlertSeconds);

	/** Any of the facts: the squad is in combat / alert. */
	CODEXTACTICS_API bool IsSweepAlert(const FSweepAlertFacts& Facts);

	/**
	 * Answer 1: after the first peek the squad is bunched towards the first corner; with BOTH corners held the middle men go back to the
	 * middle of the wall. Count places spread evenly between the two corner men (CornerA, CornerB: where they stand) on the line through
	 * LinePoint along the wall (NormalYaw = its outward normal; LinePoint = a point of the wall layout's face-offset line, so the places stay
	 * against the wall like every other wall slot): place k at (k + 1) / (Count + 1) of the way from A's projection to B's - one man exactly
	 * in the middle. Ordered from A to B. Empty when Count < 1 or the gap is shorter than (Count + 1) * MinSpacingCm (every neighbour,
	 * corner men included, at least the wall layout's spacing apart): the wall between the corners is too short for everyone and the caller
	 * keeps the places it has (today's stack / overflow).
	 */
	CODEXTACTICS_API TArray<FVector2D> MiddlePlaces(const FVector2D& CornerA, const FVector2D& CornerB, const FVector2D& LinePoint, float NormalYaw, int32 Count,
		double MinSpacingCm);

	/**
	 * Who takes which middle place (nobody crosses another on the way): Order[k] = the index into Positions of the man k-th from CornerA
	 * along the line A -> B (MiddlePlaces' place k is his). Ties keep the input order.
	 */
	CODEXTACTICS_API TArray<int32> OrderAlong(const TArray<FVector2D>& Positions, const FVector2D& CornerA, const FVector2D& CornerB);

	/** The formation leadership through a two-corner hold (ids are the caller's, e.g. squad roster indices; INDEX_NONE = none). */
	struct FLeaderChain
	{
		/** Who leads the formation now. */
		int32 Leader = INDEX_NONE;
		/** The man who led before him (answer 5: he leads again if the leader falls). */
		int32 Previous = INDEX_NONE;
	};

	/**
	 * Answer 2 (2026-11-08): the second corner man takes the formation lead and keeps it (today's wall-leadership rule: the man taking a
	 * corner takes the lead); the man who led until then is remembered as the previous leader. SecondCornerMan == Leader: nothing changes.
	 */
	CODEXTACTICS_API FLeaderChain AfterSecondCorner(int32 Leader, int32 SecondCornerMan);

	/** What happens to the formation lead when its leader falls (answer 5). */
	enum class ELeaderAfterFall : uint8
	{
		/** Not a two-corner hold's leader corner man (or the rule is off): the game's existing leader-pick rule, as before. */
		NotTheHold,
		/** The man who led before him leads again. */
		PreviousLeader,
		/** The previous leader is unknown / the fallen man himself / down as well: the game's existing leader-pick rule. */
		Fallback,
	};

	struct FLeaderFallFacts
	{
		/** Codex.Formation.TwoCornerHold is on. */
		bool bEnabled = true;
		/** The fallen man (dead / knocked down) leads the formation and is the two-corner hold's second corner man (the hold's leader). */
		bool bFallenLeadsTheHold = false;
		/** The leader before him is remembered and still in the squad. */
		bool bPreviousKnown = false;
		/** ... he is the fallen man himself. */
		bool bPreviousIsFallen = false;
		/** ... he can lead (alive, on his feet, not on a ladder / panicking / guarding). */
		bool bPreviousAble = false;
		/** ... he is the hold's other corner man and still holds that corner (StillHolds). */
		bool bPreviousHoldsOtherCorner = false;
	};

	struct FLeaderAfterFall
	{
		ELeaderAfterFall Pick = ELeaderAfterFall::NotTheHold;
		/**
		 * The other corner stays held (answer 5: only the fallen man's corner is freed): the new leader is that corner's man, still holding -
		 * he leads from his corner as the squad's one corner peek (the middle men keep their places). False: the other corner ends with the
		 * leader change, as before.
		 */
		bool bKeepOtherCorner = false;
	};

	/**
	 * Answer 5 (2026-11-08): the two-corner hold's leader (the second corner man) goes down and the game must pick a new leader -> the man who
	 * led before him (normally the first corner man); only the fallen man's corner is freed, the other corner man keeps holding his. That man
	 * down too / unknown: the existing leader-pick rule (Fallback).
	 */
	CODEXTACTICS_API FLeaderAfterFall DecideLeaderAfterFall(const FLeaderFallFacts& Facts);
}
