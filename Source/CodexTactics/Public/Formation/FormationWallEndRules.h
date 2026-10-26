#pragma once

#include "CoreMinimal.h"
#include "Formation/FormationTypes.h"
#include "Templates/Function.h"

/**
 * The squad at a wall's END - runtime glue of the formation wall layout (Godot reference: none (UE-only; the game designer's bug report
 * 2026-10-26, docs/design/FORMATION_SECTORS.md "v2.4 - the corner stack, the layout that never splits"). Pure. Tests:
 * CodexTactics.Formation.WallEnd.*. Composes the pure rules of FormationWallRules (Slide), FormationCornerRules (the corner look); no golden parity of its own (the Training Ground simulator does not model it yet).
 *
 * Corner stack (bug A, user: "the man on the corner side should come up and peek round the corner; the Commander stays behind him; the man
 * on the far side looks back along the wall"): the squad stands in the wall layout and the leader's next destination lies at a FREE end of
 * the same wall (StackReachCm before it .. StackPastCm past it, on the squad's side, within slide.max_off_wall of the wall) -> the wall
 * slide runs to the edge: the place nearest the end (the point man) stops EdgeInsetCm short of it and looks round the corner (the corner
 * watch's look at the corner: the point CornerGraze beyond the wall body at its end), the others keep the slide's order behind him - with
 * three, K in the middle is second - the trailing end looks back along the wall (the rear guard), K away from the wall.
 *
 * Fitting the wall layout (bug B, user: "the team split completely, which must never happen"): FormationWallRules::Layout places the
 * members a step apart either side of K whatever stands there. A place that is not usable from K's place (a wall body on the way - the
 * junction wall of a T or an L, the end of the wall - or no floor) moves to the next free step on K's other side; the looks of the moved
 * line follow the positions (the outer end along the wall, its neighbour half way, K out).
 */

/** A free wall end the corner stack peeks round. */
struct FFormationStackEnd
{
	/** The end point of the wall's centre line. */
	FVector2D End = FVector2D::ZeroVector;
	/** Unit direction along the wall towards the end (the squad's travel). */
	FVector2D Along = FVector2D(1.0, 0.0);
	/** The wall's thickness, cm. */
	float Thickness = 0.f;
	/** Index into the walls array. */
	int32 Wall = INDEX_NONE;
};

/**
 * The squad's corner peek in the wall layout (user bug report 2026-10-26: "solo, hovering the wall's corner shows the peek silhouette and a
 * click peeks round it; in formation that is gone"): who peeks round the hovered end and whether the others form the corner stack there.
 */
struct FFormationSquadPeek
{
	/** The member who peeks (his role in the layout). */
	EFormationRole Peeker = EFormationRole::Commander;
	/** The others form the v2.4 corner stack at End (FindStackEnd held): the leader's move to StackDestination asks the slide for it. */
	bool bStack = false;
	FFormationStackEnd End;
	FVector2D StackDestination = FVector2D::ZeroVector;
	/** The stack's places (StackSlots; empty without a stack). */
	TArray<FFormationSlot> StackSlots;
};

namespace FormationWallEnd
{
	/** The squad peek's stack destination lies this far out from the wall face at the hovered end (inside slide.max_off_wall), cm. */
	inline constexpr float PeekDestinationOutCm = 60.f;

	/**
	 * The squad's corner peek round the end at EdgeOnFace (the exposed end of the squad's wall on its face line - the cover rules' peek edge):
	 * with the corner stack there (FindStackEnd for the move PeekDestinationOutCm out from the face at that end) the peeker is the stack's
	 * point man - the place nearest that end, so with three K is second; without a stack (a joint, K already within FormationWallRules::SlideMin
	 * of the end, no squad wall) the member nearest EdgeOnFace peeks and the others hold. StartSlots = the layout in force (K first), Members =
	 * each member's role and where he stands (Yaw unused), NormalYaw = the layout's outward normal.
	 */
	CODEXTACTICS_API FFormationSquadPeek PlanSquadPeek(const TArray<FFormationWall>& Walls, const TArray<FFormationSlot>& StartSlots, float NormalYaw,
		const FVector2D& EdgeOnFace, const TArray<FFormationSlot>& Members, const FFormationConfig& Config = FFormationConfig());

	/** A destination this far short of the free end still asks for the edge, cm. */
	inline constexpr float StackReachCm = 125.f;
	/** ... or this far past it (the ghost at the corner), cm. */
	inline constexpr float StackPastCm = 200.f;
	/** The point man's centre stands this far short of the end (his capsule just inside the wall's span), cm. */
	inline constexpr float EdgeInsetCm = 40.f;
	/** K's place stands within this of the squad's wall surface (WallFaceOffset 45 + margin), cm. */
	inline constexpr float SquadWallReachCm = 120.f;
	/** The squad's wall runs within this cosine of the layout's line. */
	inline constexpr double ParallelCos = 0.985;

	/**
	 * The squad's wall in Walls: parallel to the layout's line (NormalYaw = the outward normal), K's place in front of its body within
	 * SquadWallReachCm and over its span. INDEX_NONE when none.
	 */
	CODEXTACTICS_API int32 FindSquadWall(const TArray<FFormationWall>& Walls, const FVector2D& KPlace, float NormalYaw);

	/**
	 * Bug A: the free end of the squad's wall Destination asks for (see the header comment), unset when it is no corner stack: no squad
	 * wall, a move shorter than FormationWallRules::SlideMin, the end a joint (another wall within the thicknesses + CornerJoint), the
	 * destination outside the window or on the wall's far side.
	 */
	CODEXTACTICS_API TOptional<FFormationStackEnd> FindStackEnd(const TArray<FFormationWall>& Walls, const FVector2D& KPlace, float NormalYaw,
		const FVector2D& Destination, const FFormationConfig& Config = FFormationConfig());

	/** How far along End.Along the start places move so the leading one stands EdgeInsetCm short of the end (negative: back), cm. */
	CODEXTACTICS_API float StackShift(const TArray<FFormationSlot>& StartSlots, const FFormationStackEnd& End);

	/** The point man's look round the corner from PointPos: at the corner (FormationCornerRules::CornerGraze beyond the wall body). */
	CODEXTACTICS_API float CornerPeekYaw(const FFormationStackEnd& End, const FVector2D& PointPos);

	/**
	 * The held corner stack: the start places (K first) moved StackShift along the wall, their looks - the point man round the corner, the
	 * trailing end back along the wall turned slide.lead_out_deg outward, K (not an end) the outward normal, a fourth
	 * member (between them) the normal turned wall.neighbour_deg towards the rear. In StartSlots' order.
	 */
	CODEXTACTICS_API TArray<FFormationSlot> StackSlots(const TArray<FFormationSlot>& StartSlots, const FFormationStackEnd& End, float NormalYaw,
		const FFormationConfig& Config = FFormationConfig());

	/** The member at the leading end of the stack (the point man): the place farthest along End.Along. */
	CODEXTACTICS_API EFormationRole PointRole(const TArray<FFormationSlot>& Slots, const FFormationStackEnd& End);

	/**
	 * Bug B: the wall layout fitted to what stands there (see the header comment). Slots = FormationWallRules::Layout's (K first), NormalYaw =
	 * its outward normal, Usable(KPlace, Place) = the place can be stood on and reached from K's place without passing a wall body. A
	 * place that is not usable moves to the next usable step on K's other side (beyond the members there); when that one is not usable either
	 * it stays. Unchanged when every place is usable. The moved line's looks: K keeps his; a member alone on a side looks wall.flank_deg off
	 * the normal to that side, two on a side: the outer one wall.end_deg, the inner one wall.neighbour_deg (the 4-man layout's rule); with
	 * two members the pair's +-45.
	 */
	CODEXTACTICS_API TArray<FFormationSlot> FitLayout(const TArray<FFormationSlot>& Slots, float NormalYaw,
		TFunctionRef<bool(const FVector2D& KPlace, const FVector2D& Place)> Usable, const FFormationConfig& Config = FFormationConfig());

	/** A team-mate this close to the straight way to the place stands in it, cm (two capsules and a margin). */
	inline constexpr float StepRoundLaneCm = 55.f;
	/** ... and is stepped round this far out from the wall (on the open side), cm. */
	inline constexpr float StepRoundOutCm = 110.f;

	/**
	 * Bug D (2026-10-26): a member walking along the wall into his wall-layout place (a re-form after a leader switch: K in the middle) and a
	 * team-mate standing on his straight way there (within StepRoundLaneCm of it, more than StepRoundLaneCm from both ends) -> the point
	 * StepRoundOutCm out from that team-mate (NormalYaw = the wall's outward normal) and StepRoundLaneCm past him along the wall to walk to first; the nearest such team-mate. Unset: the
	 * way is clear. The nav mesh ignores bodies: without it he pushed against the crouched leader for good.
	 */
	CODEXTACTICS_API TOptional<FVector2D> StepRound(const FVector2D& Here, const FVector2D& Place, const TArray<FVector2D>& Others, float NormalYaw);
}
