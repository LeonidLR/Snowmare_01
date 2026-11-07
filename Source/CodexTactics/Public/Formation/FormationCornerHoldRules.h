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
}
