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

	// --- Moving off a wall, the man nearest the destination leads (the game designer's PIE 2026-11-08, L_Gen_Depot_1: the two-corner hold,
	//     the right-corner man leading, a move to the LEFT - the leader walked round the wall's left end dragging the next man with him) ---

	/** The current leader keeps the lead while his route is within this of the shortest (no leader hopping for a near tie), cm. */
	inline constexpr double MoveLeaderKeepCm = 150.0;

	/**
	 * The squad stands at a wall for this rule: the formation's wall order / wall layout in force, a squad corner peek, the two-corner hold.
	 * An open-ground formation move keeps its leader.
	 */
	CODEXTACTICS_API bool IsAtWallForMoveLead(bool bWallOrder, bool bWallLayoutActive, bool bCornerPeek, bool bTwoCornerHold);

	/**
	 * Who leads a move order given at a wall (the same idea as PickCornerLeader - the man nearest where the squad goes takes the lead, the
	 * others follow behind him in the usual order): the able member with the shortest route to the destination (RouteCm: the nav path
	 * length, < 0 = no route); the current leader (CurrentLeader) keeps it while his route is within KeepCm of that. INDEX_NONE when nobody
	 * able has a route.
	 */
	CODEXTACTICS_API int32 PickMoveLeader(const TArray<double>& RouteCm, const TArray<bool>& Able, int32 CurrentLeader, double KeepCm = MoveLeaderKeepCm);

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

	/**
	 * The re-form behind the new leader (the game designer's bug report 2026-11-02 item 2: the stack ended right but took far too long - the
	 * others crept along crouched, the trailing man walked backwards, the last metre was a crawl): a crouched member whose place in the stack
	 * lies farther than StandCm stands up for the walk (squad_walls.json reform_stand_m; he crouches again at his place).
	 */
	CODEXTACTICS_API bool ReformStands(double DistanceToPlaceCm, bool bCrouched, double StandCm, bool bAlongWall = false);

	/**
	 * The re-form: the man with the covered approach's REAR duty (looking back the way they came - walking backwards at move.backward_mult) -
	 * and any man whose approach look has him walk backwards into the stack (the trailing man looking back along the wall) -
	 * faces his walk while his place is far, the walking rear guard's rule v2.6 (FormationSectorRules::RearCatchUp: forward beyond
	 * move.rear_turn_lag, the rear look again within move.rear_turn_back). bTurned = the last tick's answer.
	 */
	CODEXTACTICS_API bool ReformRearFacesWalk(double DistanceToPlaceCm, bool bTurned, const FFormationConfig& Config, bool bAlongWall = false);

	/**
	 * The game designer's bug 2026-11-03 (L_Gen_Depot_1: a corner peek at the far end of the same long wall - all three stood up, turned to
	 * their walk and walked along the wall, the rear wide open; "it worked better before"): his walk into the stack runs ALONG THE SAME WALL
	 * when he stands within OffCm of the stack's face line (the line through his place along the wall; NormalYaw = its outward normal). Then
	 * he keeps his crouch and his look (the rear-looker walks backwards / sideways as before item 2) - ReformStands / ReformRearFacesWalk's
	 * bAlongWall; the stand-up and the face-the-walk rules stay for a walk that leaves the wall (another object's cover, a prone spot, the
	 * open ground). OffCm <= 0 = never (item 2's rules everywhere; squad_walls.json reform_along_wall_off_m).
	 */
	CODEXTACTICS_API bool ReformAlongWall(const FVector2D& MemberPos, const FVector2D& PlacePos, float NormalYaw, double OffCm);

	/** The plan of a box-selected group's corner peek at its wall (PlanGroupCornerPeek); indices into the members. */
	struct FGroupCornerPeekPlan
	{
		/** The member who takes the corner and peeks: the able one nearest the clicked end along the wall; INDEX_NONE = nobody able. */
		int32 Peeker = INDEX_NONE;
		/** +1 / -1: the corner lies that way along the wall (the along axis of the members' coordinates). */
		double Toward = 1.0;
		/** The others behind him, the nearest the corner first (the able ones; those who cannot walk stay where they are, not listed). */
		TArray<int32> Order;
		/** Each Order member's place along the wall (same axis); equal to where he stands when he is within KeepCm of his stack place. */
		TArray<double> PlaceAlong;
	};

	/**
	 * The game designer's bug 2026-11-03 (solo mode, three box-selected along a wall, a corner peek ordered: the MIDDLE man - the selection's
	 * leader - side-stepped slowly to the corner, off the wall round the end man): the formation's wall-leadership rule for the box-selected
	 * group - the able member NEAREST the clicked end (MemberAlong = each one's place along the wall, EdgeAlong = the end's; within TieCm the
	 * nearer wins either way) takes the corner and peeks; the others line up behind his stand point (PeekerStandAlong) along the wall, a
	 * SpacingCm apart, in the order they stand (nobody crosses another); a man already within KeepCm of his place stays where he is.
	 */
	CODEXTACTICS_API FGroupCornerPeekPlan PlanGroupCornerPeek(const TArray<double>& MemberAlong, const TArray<bool>& Able, double EdgeAlong, double PeekerStandAlong,
		double SpacingCm, double KeepCm);
}
