#pragma once

#include "CoreMinimal.h"
#include "Tactics/DoorwayBreachRules.h"

/**
 * R17-5 (designer 2026-11-17, with R17-3): the doorway breach in the game - the stack's arrival and the follow-up entry (docs/design/
 * DOORWAY_BREACH.md section 7, the designer's answers 2026-11-11 item 6: "once the triad is set, a click inside the room = the buttonhook
 * entry: man 1 right corner, man 2 left, man 3 centre"). Pure geometry; the runtime is ACodexTacticsPlayerController's breach session
 * (CodexTacticsPlayerControllerBreach.cpp). The slots themselves are DoorwayBreachRules' (Gemini 050). UE-only. Tests
 * CodexTactics.Tactics.DoorwayBreach.Entry.*.
 */

/** One man's move of the entry. */
struct FDoorwayBreachEntryMove
{
	/** Index into the participants the plan was made for. */
	int32 Participant = INDEX_NONE;
	/** Where he goes (feet; the navmesh projection is the caller's). */
	FVector Destination = FVector::ZeroVector;
	/** He looks this way on arrival (into his sector of the room). */
	FVector LookDirection = FVector::ForwardVector;
	/** He starts this long after the entry click (the 0.3 s stagger), s. */
	float DelaySeconds = 0.f;
	/** He crouches on arrival (the centre man's low-ready). */
	bool bCrouch = false;
};

/**
 * The room behind a stacked doorway (measured once at the stack order: traces from just inside the threshold to the room's far wall and its side
 * walls), in the doorway's frame: entering = -Outward, right = the entering man's right hand.
 */
struct FDoorwayBreachRoom
{
	/** From the threshold to the far wall / to the side walls, cm. */
	float DepthCm = 0.f;
	float RightCm = 0.f;
	float LeftCm = 0.f;
	/** The threshold's floor height (the room is on this floor). */
	float FloorZ = 0.f;
	bool bValid = false;
};

namespace DoorwayBreachEntryRules
{
	/** The hooking jamb man ends this far inside the threshold, cm ... */
	inline constexpr float HookDepthCm = 150.f;
	/** ... this far past his jamb along the interior wall, cm. */
	inline constexpr float HookSideCm = 100.f;
	/**
	 * A hook stops this far short of a measured room wall, cm. The navmesh keeps about this far off the wall, so a point closer
	 * is unreachable (S01 d20: the engineer's hook at the east return, "move to (2525, -6536) -> unreachable").
	 */
	inline constexpr float HookWallClearCm = 70.f;
	/**
	 * A hook closer to the axis than this stands in the centre man's path (two 40 cm capsules plus a 20 cm step — the give-way
	 * crossing). The janitor closet behind S01 d20 is 250 cm wide, so neither return has this much room.
	 */
	inline constexpr float HookLaneCm = 100.f;
	/** An in-lane hook stops at least this far past the centre man's stop, and each further in-lane hook a step beyond that, cm. */
	inline constexpr float HookPastCentreCm = 120.f;
	/** The centre man drives this far in ("three paces"), cm. */
	inline constexpr float CentreDepthCm = 250.f;
	/** Between one man's entry and the next one's, s. */
	inline constexpr float StaggerSeconds = 0.3f;
	/** A jamb man / the centre man is at his slot within this (2D), cm. */
	inline constexpr float ArrivalCm = 80.f;
	/** A click this deep inside the threshold (along -Outward) ... */
	inline constexpr float InsideMinDepthCm = 40.f;
	/** The room is measured up to this deep ... */
	inline constexpr float RoomMaxDepthCm = 1200.f;
	/** ... and this far either side of the opening's axis; an unmeasured side (no wall within reach) counts this far, cm. */
	inline constexpr float RoomMaxSideCm = 800.f;
	/** The room is at least this big along each measure (a reveal next to the opening is no room wall), cm. */
	inline constexpr float RoomMinCm = 100.f;
	/** A hit on a room wall's own face still counts (this past the measured wall), cm ... */
	inline constexpr float RoomWallSlackCm = 15.f;
	/** ... from this under the threshold's floor ... */
	inline constexpr float RoomBelowFloorCm = 30.f;
	/** ... up to this over it (the same storey: its floor and walls, not the floor above or the yard below), cm. */
	inline constexpr float RoomHeightCm = 280.f;

	/** The entering direction (into the room, -Outward) and its right hand (Z up). */
	CODEXTACTICS_API FVector EntryDirection(const FVector& Outward);
	CODEXTACTICS_API FVector EntryRight(const FVector& Outward);

	/** Which side of the opening (seen entering) a man stands on: +1 right, -1 left (0 on the axis: right). */
	CODEXTACTICS_API int32 SideOf(const FVector& Position, const FVector& Center, const FVector& Outward);

	/** The room from the measured wall distances (< 0: no wall within reach = RoomMaxDepthCm / RoomMaxSideCm), each at least RoomMinCm. */
	CODEXTACTICS_API FDoorwayBreachRoom MakeRoom(float ForwardWallCm, float RightWallCm, float LeftWallCm, float FloorZ);

	/**
	 * The click point lies inside the stacked doorway's room: InsideMinDepthCm past the threshold up to its far wall, between its side walls
	 * (+ RoomWallSlackCm), on the threshold's storey (-RoomBelowFloorCm .. RoomHeightCm over its floor). An invalid room: nothing is inside.
	 */
	CODEXTACTICS_API bool IsInsideRoom(const FVector& Point, const FVector& Center, const FVector& Outward, const FDoorwayBreachRoom& Room);

	/**
	 * The entry (section 7.2): the jamb men hook to their own side's interior corner - the right-hand one first, the left-hand one StaggerSeconds
	 * later - the centre man drives CentreDepthCm in after them and crouches; rear security stays outside (no move). Roles[i] is the slot role
	 * participant i holds, Positions[i] where he stands now (decides a jamb man's side; a stacked fallback jamb follows its jamb man in).
	 * A valid Room pulls a hook back to HookWallClearCm inside that wall (a return wall beside a street door is closer than the full hook).
	 * One that is still inside HookLaneCm of the axis stops past the centre man; the centre man then stops short of that hook, so a
	 * closet files through the doorway instead of leaving a body stood in it.
	 */
	CODEXTACTICS_API TArray<FDoorwayBreachEntryMove> PlanEntry(const FVector& Center, const FVector& Outward, float WidthCm, const TArray<EDoorwayBreachSlotRole>& Roles,
		const TArray<FVector>& Positions, const FDoorwayBreachRoom& Room = FDoorwayBreachRoom());
}
