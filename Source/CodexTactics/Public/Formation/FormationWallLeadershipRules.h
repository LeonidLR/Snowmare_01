#pragma once

#include "CoreMinimal.h"
#include "Formation/FormationTypes.h"
#include "Formation/FormationWallEndRules.h"

/**
 * Wall leadership - the squad's corner peek hands the formation lead to the man at that corner (Godot reference: none (UE-only; the game
 * designer's decisions 2026-11-01, docs/design/FORMATION_SECTORS.md "Wall leadership"). Pure. Tests: CodexTactics.Formation.WallLeadership.*.
 * Composes FormationWallEnd (FindStackEnd / StackSlots); no golden parity of its own (the Training Ground simulator models neither the corner
 * peek nor the formation leader).
 *
 * The decisions: the squad pressed to a wall, the player orders a peek round one of its corners -> the member NEAREST that corner (along the
 * wall) peeks and BECOMES THE FORMATION LEADER (a LEFT-corner peek -> the left-end man, a RIGHT one -> the right-end man; at the same wall
 * left then right moves the lead from one end man to the other). Nobody passes him or steps out round the corner: ALL the others line up
 * along the wall's near face BEHIND him, a stack from the corner along the wall, in the order they stood (nobody crosses another). A member
 * who cannot take it (knocked down, dead, on a ladder, not in the formation) is skipped - the next nearest takes it - and stands last in
 * the line. Leaving the wall keeps the new leader and that order.
 */

/** One member at the wall when the corner peek is ordered. */
struct FWallLeadershipMember
{
	/** His role in the wall layout in force (the current leader: Commander). */
	EFormationRole Role = EFormationRole::Commander;
	/** Where he stands. */
	FVector2D Position = FVector2D::ZeroVector;
	/** He can take the corner and walk the line: alive, on his feet, not on a ladder, in the formation. */
	bool bAble = true;
};

/** The wall leadership plan of one corner peek order (FormationWallLeadership::Plan). */
struct FWallLeadershipPlan
{
	/** Index into the members of the man who takes the corner and the lead; INDEX_NONE: nobody can (no plan). */
	int32 Leader = INDEX_NONE;
	/** The others in the line behind him, the nearest first (indices into the members; the members who cannot walk last). */
	TArray<int32> Order;
	/** Each member's role after the switch (Roles[i] for member i): the new leader Commander, the old leader the new leader's old role. */
	TArray<EFormationRole> Roles;
	/** Unit direction along the wall towards the ordered corner. */
	FVector2D Toward = FVector2D(1.0, 0.0);
	/** The corner the stack forms at: the free end of the squad's wall from the world walls (bWorldEnd), else one at the peek edge. */
	FFormationStackEnd End;
	bool bWorldEnd = false;
	/** The held stack: the new leader (Commander) first at the corner, the others behind him along the face (FormationWallEnd::StackSlots). */
	TArray<FFormationSlot> StackSlots;
};

namespace FormationWallLeadership
{
	/** Two members this close along the wall are equally near the corner (the nearer to the edge point wins), cm. */
	inline constexpr double TieCm = 10.0;
	/** HoldBehind: a man may close up this much on the man ahead before his place is held back (the leader's corner cover slot stands ~20 cm behind his stack place), cm. */
	inline constexpr double BehindSlackCm = 30.0;

	/**
	 * The direction along the wall towards the ordered corner: the wall line's direction (perpendicular to NormalYaw, the layout's outward
	 * normal) pointing from Reference (a point of the squad's line, K's place) to EdgeOnFace (the corner on the face line).
	 */
	CODEXTACTICS_API FVector2D TowardCorner(float NormalYaw, const FVector2D& EdgeOnFace, const FVector2D& Reference);

	/**
	 * Who takes the corner (decisions 2, 3, 7): the able member farthest along Toward - the nearest the corner along the wall; within TieCm
	 * the one nearer EdgeOnFace. INDEX_NONE when nobody is able.
	 */
	CODEXTACTICS_API int32 PickCornerLeader(const TArray<FWallLeadershipMember>& Members, const FVector2D& EdgeOnFace, const FVector2D& Toward);

	/**
	 * The line behind the new leader (decision 4): every other member, the able ones first by their place along Toward (the nearest the
	 * corner first - nobody crosses another on the way there), then those who cannot walk (in the same order).
	 */
	CODEXTACTICS_API TArray<int32> StackOrder(const TArray<FWallLeadershipMember>& Members, int32 LeaderIndex, const FVector2D& Toward);

	/**
	 * The roles after the switch (decision 5 - only the formation leadership moves): NewLeader takes Commander, the member who was Commander
	 * takes NewLeader's old role, everyone else keeps his. Unchanged when NewLeader already leads (or is out of range).
	 */
	CODEXTACTICS_API TArray<EFormationRole> SwapRoles(const TArray<FWallLeadershipMember>& Members, int32 NewLeader);

	/**
	 * The stack line before its shift to the corner: K at KPlace, then RolesBehind[i] at KPlace - Toward * (i * Spacing + FirstGapCm + Spacing)
	 * - the first man one spacing plus FirstGapCm behind K (FormationWallEnd::AtEndGapCm: the leader in his corner cover stands ~20 cm behind
	 * his stack place), the rest a spacing apart; looks unset (StackSlots gives them).
	 */
	CODEXTACTICS_API TArray<FFormationSlot> StackLine(const FVector2D& KPlace, const FVector2D& Toward, const TArray<EFormationRole>& RolesBehind, float Spacing,
		float FirstGapCm);

	/** A corner at the peek edge when the world walls give none (no squad wall in them): the end at EdgeOnFace, along Toward, no thickness. */
	CODEXTACTICS_API FFormationStackEnd EndAtEdge(const FVector2D& EdgeOnFace, const FVector2D& Toward);

	/**
	 * The whole plan for a corner peek ordered at EdgeOnFace (see the header comment). Walls = the world walls round the squad (may be empty),
	 * LineOrigin = a point of the wall layout's line (K's place), NormalYaw = its outward normal, Members = everyone in the formation (the
	 * current leader with Role Commander). The new leader's place on the line is his own position projected onto it; the corner = the free
	 * end FormationWallEnd::FindStackEnd finds for a move PeekDestinationOutCm out from the edge (pushed SlideMin on when he is already
	 * there), else EndAtEdge; the stack = FormationWallEnd::StackSlots of StackLine (he stands EdgeInsetCm short of the end).
	 */
	CODEXTACTICS_API FWallLeadershipPlan Plan(const TArray<FFormationWall>& Walls, const FVector2D& LineOrigin, float NormalYaw, const FVector2D& EdgeOnFace,
		const TArray<FWallLeadershipMember>& Members, const FFormationConfig& Config = FFormationConfig());

	/**
	 * Nobody passes the man ahead on the way into the stack (decision 4): Target (a member's place, MyPlace before any clamp) moved back
	 * along Toward so it stays as far behind AheadPos (where the man ahead of him in the line stands now) as MyPlace lies behind AheadPlace
	 * (the man ahead's place), less SlackCm. Unchanged when it is already behind that.
	 */
	CODEXTACTICS_API FVector2D HoldBehind(const FVector2D& Target, const FVector2D& MyPlace, const FVector2D& AheadPlace, const FVector2D& AheadPos,
		const FVector2D& Toward, double SlackCm = BehindSlackCm);

	/**
	 * The man a line member keeps behind: Able[i] = the line's i-th member (the leader first, then Order) can walk -> the position in the line
	 * of the nearest able member before Position (the leader for the first); INDEX_NONE for the leader himself or when none is able.
	 */
	CODEXTACTICS_API int32 AheadInLine(const TArray<bool>& Able, int32 Position);
}
