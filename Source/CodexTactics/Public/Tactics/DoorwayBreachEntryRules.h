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

namespace DoorwayBreachEntryRules
{
	/** The hooking jamb man ends this far inside the threshold, cm ... */
	inline constexpr float HookDepthCm = 150.f;
	/** ... this far past his jamb along the interior wall, cm. */
	inline constexpr float HookSideCm = 100.f;
	/** The centre man drives this far in ("three paces"), cm. */
	inline constexpr float CentreDepthCm = 250.f;
	/** Between one man's entry and the next one's, s. */
	inline constexpr float StaggerSeconds = 0.3f;
	/** A jamb man / the centre man is at his slot within this (2D), cm. */
	inline constexpr float ArrivalCm = 80.f;
	/** A click this deep inside the threshold (along -Outward) ... */
	inline constexpr float InsideMinDepthCm = 40.f;
	/** ... up to this deep ... */
	inline constexpr float InsideMaxDepthCm = 1500.f;
	/** ... and within this of the opening's axis, is a click inside the room, cm. */
	inline constexpr float InsideHalfSpanCm = 1200.f;

	/** The entering direction (into the room, -Outward) and its right hand (Z up). */
	CODEXTACTICS_API FVector EntryDirection(const FVector& Outward);
	CODEXTACTICS_API FVector EntryRight(const FVector& Outward);

	/** Which side of the opening (seen entering) a man stands on: +1 right, -1 left (0 on the axis: right). */
	CODEXTACTICS_API int32 SideOf(const FVector& Position, const FVector& Center, const FVector& Outward);

	/** The point a click on the room's floor / its inner walls lies at counts as "inside the room" (InsideMinDepthCm .. InsideMaxDepthCm deep). */
	CODEXTACTICS_API bool IsInsideRoom(const FVector& Point, const FVector& Center, const FVector& Outward);

	/**
	 * The entry (section 7.2): the jamb men hook to their own side's interior corner - the right-hand one first, the left-hand one StaggerSeconds
	 * later - the centre man drives CentreDepthCm in after them and crouches; rear security stays outside (no move). Roles[i] is the slot role
	 * participant i holds, Positions[i] where he stands now (decides a jamb man's side; a stacked fallback jamb follows its jamb man in).
	 */
	CODEXTACTICS_API TArray<FDoorwayBreachEntryMove> PlanEntry(const FVector& Center, const FVector& Outward, float WidthCm, const TArray<EDoorwayBreachSlotRole>& Roles,
		const TArray<FVector>& Positions);
}
