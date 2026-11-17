#pragma once

#include "CoreMinimal.h"
#include "Formation/FormationCornerRules.h"
#include "Formation/FormationTypes.h"

/**
 * The leader takes the corner himself (Godot reference: none (UE-only; game designer decision 2026-11-17, TASK_BOARD F-2 option B:
 * "if the leader himself goes to the corner, the follower's corner watch is cancelled - the leader looks round the corner"); contract
 * docs/design/FORMATION_CONTRACT.md section 20, docs/design/FORMATION_SECTORS.md "F-2"). Pure. Tests: CodexTactics.Formation.LeaderCorner.*.
 *
 * The leader's cover slot - the one he stands in, or the one he is on his way to (a cover order, a shimmy, the automatic walk to the
 * threat-side corner) - within ClaimDistCm of a FREE end of a wall (FormationCornerRules::IsFreeEnd; a joint is no corner) claims that
 * end for him:
 *  - the corner watch of that end is his (a follower watching it hands it over; the other side's watch stays a follower's);
 *  - no follower's place lies at that corner: a place ahead of the leader towards the end (or past it, round the corner) within
 *    ZoneOutCm of the wall's body is mirrored to behind his slot along the wall, at least KeepBackCm behind it and never crowding
 *    another place (his own included) - the column / echelon / wall layout / corner-stack point that put a team-mate on the slot he is walking to (CoverSmoke,
 *    Gemini report 075 point 2) is now behind him. Places already behind him are not touched.
 */

/** The free wall end the leader's cover slot claims. */
struct FFormationLeaderCorner
{
	/** Index into the walls array. */
	int32 Wall = INDEX_NONE;
	/** The end: 0 = A, 1 = B. */
	int32 End = 0;
	/** The end point of the wall's centre line. */
	FVector2D EndPoint = FVector2D::ZeroVector;
	/** Unit direction along the wall towards the end. */
	FVector2D Along = FVector2D(1.0, 0.0);
	/** Unit normal of the wall towards the side the leader's slot is on. */
	FVector2D Out = FVector2D(0.0, 1.0);
	/** The wall's thickness, cm. */
	float Thickness = 0.f;
	/** The leader's cover slot (his feet). */
	FVector2D Slot = FVector2D::ZeroVector;
};

namespace FormationLeaderCornerRules
{
	/** A cover slot this close to a free wall end (along the wall) claims it, cm (the designer's "~1.5 m"). */
	inline constexpr float ClaimDistCm = 150.f;
	/** ... while it is at most this far past the end (a slot just round the corner), cm. */
	inline constexpr float ClaimPastCm = 60.f;
	/** ... and at most this far off the wall's surface (the slot offset 45 cm + margin; FormationWallEnd::SquadWallReachCm), cm. */
	inline constexpr float SlotWallReachCm = 120.f;
	/** A follower's place stays at least this far behind the leader along the wall (two capsules), cm. */
	inline constexpr float KeepBackCm = 90.f;
	/** The corner zone reaches this far off the wall's surface, both sides, cm. */
	inline constexpr float ZoneOutCm = 200.f;
	/** A place moved behind the leader stands at least this far off the wall's surface on his side (the cover slot offset), cm. */
	inline constexpr float MinOutCm = 45.f;

	/**
	 * The free end the cover slot Slot claims: the nearest end (along its wall) of a wall whose surface is within SlotWallReachCm of the slot
	 * and whose end is a free end, the slot from ClaimDist before it to ClaimPastCm past it. Unset: no such end.
	 */
	CODEXTACTICS_API TOptional<FFormationLeaderCorner> ClaimedEnd(const TArray<FFormationWall>& Walls, const FVector2D& Slot,
		float ClaimDist = ClaimDistCm);

	/** The first of the two cover slots (the pending one first, then the one he is in) that claims an end. */
	CODEXTACTICS_API TOptional<FFormationLeaderCorner> ClaimedEndOf(const TArray<FFormationWall>& Walls, const TOptional<FVector2D>& PendingSlot,
		const TOptional<FVector2D>& CurrentSlot, float ClaimDist = ClaimDistCm);

	/**
	 * Place lies at the claimed corner: ahead of the slot less KeepBack along the wall (past the end included), within ZoneOutCm of its body.
	 * The slot, not where the leader stands: a leader walking in from afar (a wall-leadership peek) must not send the stack behind him back
	 * along the whole wall, and a corner stack lined up behind his slot is left alone.
	 */
	CODEXTACTICS_API bool IsAtClaimedCorner(const FFormationLeaderCorner& Claim, const FVector2D& Place, float KeepBack = KeepBackCm);

	/**
	 * A follower's place kept off the claimed corner: unchanged when not IsAtClaimedCorner; else mirrored through the leader's reference point
	 * along the wall (the slot), at least KeepBack behind it, MinOutCm..ZoneOutCm off the wall on the leader's side.
	 */
	CODEXTACTICS_API FVector2D KeepBehind(const FFormationLeaderCorner& Claim, const FVector2D& Place, float KeepBack = KeepBackCm);

	/**
	 * KeepBehind on every follower place of Slots (the Commander's own slot is left alone); a moved place that would crowd another (closer than
	 * KeepBack) goes on back along the wall to KeepBack behind it - the nearest to the leader first. Returns how many moved. Looks unchanged.
	 */
	CODEXTACTICS_API int32 KeepSlotsBehind(const FFormationLeaderCorner& Claim, TArray<FFormationSlot>& Slots, float KeepBack = KeepBackCm);

	/**
	 * The corner watch with the claim: FormationCornerRules::Watch, then a watch of the claimed end held by a follower goes to the leader
	 * (the Commander) - his look round the corner - and the other side's watch, if it was the leader's, to the member closest to that end.
	 * Without a claim it is Watch itself.
	 */
	CODEXTACTICS_API FFormationCornerResult Watch(const TArray<FFormationWall>& Walls, const FVector2D& LeaderPos, float LeaderYaw,
		const TArray<FFormationSlot>& Members, const FFormationCornerState& State, const TOptional<FFormationLeaderCorner>& Claim,
		const FFormationConfig& Config = FFormationConfig());
}
