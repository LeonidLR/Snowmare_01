// CodexTactics.Formation.WallLeadership.* - wall leadership (Formation/FormationWallLeadershipRules; the game designer's decisions
// 2026-11-01, docs/design/FORMATION_SECTORS.md "Wall leadership"): the corner peek's man nearest that corner becomes the formation leader,
// the others stack behind him along the near face in the order they stood, nobody passes him or stands past the corner, an unable end man
// is skipped. UE-only (no golden: the Training Ground simulator models neither the corner peek nor the formation leader).

#include "Misc/AutomationTest.h"
#include "Formation/FormationRuntimeRules.h"
#include "Formation/FormationWallLeadershipRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define FORMATION_WALL_LEADERSHIP_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Formation.WallLeadership." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace FormationWallLeadershipTestUtil
{
	/** The long wall along X (-600..600, 40 thick), the squad in front of it at y -65 (the outward normal yaw -90). */
	constexpr float LeadNormalYaw = -90.f;
	const FVector2D LeadWestEdge(-600.0, -20.0);
	const FVector2D LeadEastEdge(600.0, -20.0);
	const FVector2D LeadLineOrigin(0.0, -65.0);

	TArray<FFormationWall> LeadWalls()
	{
		FFormationWall Wall;
		Wall.A = FVector2D(-600.0, 0.0);
		Wall.B = FVector2D(600.0, 0.0);
		Wall.Thickness = 40.f;
		return { Wall };
	}

	FWallLeadershipMember LeadMember(EFormationRole Role, double X, bool bAble = true)
	{
		FWallLeadershipMember Member;
		Member.Role = Role;
		Member.Position = FVector2D(X, -65.0);
		Member.bAble = bAble;
		return Member;
	}

	/** The group cover order's layout of three: the Commander mid-wall, P on the west (x -100), Z on the east (x 100). */
	TArray<FWallLeadershipMember> LeadThree()
	{
		return { LeadMember(EFormationRole::Commander, 0.0), LeadMember(EFormationRole::Right, -100.0), LeadMember(EFormationRole::Rear, 100.0) };
	}

	const FFormationSlot* LeadSlotOf(const TArray<FFormationSlot>& Slots, EFormationRole Role)
	{
		return Slots.FindByPredicate([Role](const FFormationSlot& Slot) { return Slot.Role == Role; });
	}
}

FORMATION_WALL_LEADERSHIP_TEST(FFormationWallLeadershipPickTest, "PickCornerLeader")
bool FFormationWallLeadershipPickTest::RunTest(const FString&)
{
	using namespace FormationWallLeadershipTestUtil;
	const TArray<FWallLeadershipMember> Three = LeadThree();
	const FVector2D West = FormationWallLeadership::TowardCorner(LeadNormalYaw, LeadWestEdge, LeadLineOrigin);
	const FVector2D East = FormationWallLeadership::TowardCorner(LeadNormalYaw, LeadEastEdge, LeadLineOrigin);
	TestTrue(TEXT("Toward the west corner = -X"), West.Equals(FVector2D(-1.0, 0.0), 1e-6));
	TestTrue(TEXT("Toward the east corner = +X"), East.Equals(FVector2D(1.0, 0.0), 1e-6));
	TestEqual(TEXT("West peek: the west-end man (P) takes it, not the Commander mid-wall"), FormationWallLeadership::PickCornerLeader(Three, LeadWestEdge, West), 1);
	TestEqual(TEXT("East peek: the east-end man (Z) takes it"), FormationWallLeadership::PickCornerLeader(Three, LeadEastEdge, East), 2);
	// Decision 7: the end man cannot -> the next nearest.
	TArray<FWallLeadershipMember> Down = Three;
	Down[1].bAble = false;
	TestEqual(TEXT("West end man knocked down: the next nearest (the Commander) takes it"), FormationWallLeadership::PickCornerLeader(Down, LeadWestEdge, West), 0);
	Down[0].bAble = false;
	TestEqual(TEXT("... and him too: the far man"), FormationWallLeadership::PickCornerLeader(Down, LeadWestEdge, West), 2);
	Down[2].bAble = false;
	TestEqual(TEXT("Nobody able: nobody"), FormationWallLeadership::PickCornerLeader(Down, LeadWestEdge, West), static_cast<int32>(INDEX_NONE));
	// A tie along the wall (one a step out from it): the nearer to the corner point.
	TArray<FWallLeadershipMember> Tie = { LeadMember(EFormationRole::Commander, -300.0), LeadMember(EFormationRole::Right, -305.0) };
	Tie[1].Position.Y = -300.0;
	TestEqual(TEXT("Within the tie band the man nearer the edge point wins"), FormationWallLeadership::PickCornerLeader(Tie, LeadWestEdge, West), 0);
	return true;
}

FORMATION_WALL_LEADERSHIP_TEST(FFormationWallLeadershipOrderTest, "StackOrderAndRoles")
bool FFormationWallLeadershipOrderTest::RunTest(const FString&)
{
	using namespace FormationWallLeadershipTestUtil;
	TArray<FWallLeadershipMember> Four = LeadThree();
	Four.Add(LeadMember(EFormationRole::Left, 200.0));
	const FVector2D West(-1.0, 0.0);
	const TArray<int32> Order = FormationWallLeadership::StackOrder(Four, 1, West);
	TestEqual(TEXT("Three behind the leader"), Order.Num(), 3);
	if (Order.Num() == 3)
	{
		TestTrue(TEXT("In the order they stood from the corner: K, Z, L"), Order[0] == 0 && Order[1] == 2 && Order[2] == 3);
	}
	Four[2].bAble = false;
	const TArray<int32> WithDown = FormationWallLeadership::StackOrder(Four, 1, West);
	TestTrue(TEXT("A knocked-down man stands last in the line"), WithDown.Num() == 3 && WithDown[0] == 0 && WithDown[1] == 3 && WithDown[2] == 2);
	// Decision 5: only the formation leadership moves - the old leader takes the new one's role.
	const TArray<EFormationRole> Roles = FormationWallLeadership::SwapRoles(LeadThree(), 1);
	TestTrue(TEXT("The new leader is K, the old leader takes P, Z keeps his"),
		Roles.Num() == 3 && Roles[1] == EFormationRole::Commander && Roles[0] == EFormationRole::Right && Roles[2] == EFormationRole::Rear);
	const TArray<EFormationRole> Same = FormationWallLeadership::SwapRoles(LeadThree(), 0);
	TestTrue(TEXT("The leader peeking himself: no change"), Same[0] == EFormationRole::Commander && Same[1] == EFormationRole::Right && Same[2] == EFormationRole::Rear);
	return true;
}

FORMATION_WALL_LEADERSHIP_TEST(FFormationWallLeadershipPlanTest, "PlanStackBehindTheNewLeader")
bool FFormationWallLeadershipPlanTest::RunTest(const FString&)
{
	using namespace FormationWallLeadershipTestUtil;
	// The Commander mid-wall, the peek round the WEST corner: P (the west-end man) leads, everyone stacks behind him along the near face.
	const FWallLeadershipPlan Plan = FormationWallLeadership::Plan(LeadWalls(), LeadLineOrigin, LeadNormalYaw, LeadWestEdge, LeadThree());
	TestEqual(TEXT("The west-end man takes the lead"), Plan.Leader, 1);
	TestTrue(TEXT("The corner from the world walls: the wall's west end"), Plan.bWorldEnd && Plan.End.End.Equals(FVector2D(-600.0, 0.0), 0.01));
	TestEqual(TEXT("A stack of three"), Plan.StackSlots.Num(), 3);
	const FFormationSlot* K = LeadSlotOf(Plan.StackSlots, EFormationRole::Commander);
	const FFormationSlot* OldK = LeadSlotOf(Plan.StackSlots, EFormationRole::Right); // the old Commander took P
	const FFormationSlot* Z = LeadSlotOf(Plan.StackSlots, EFormationRole::Rear);
	if (TestTrue(TEXT("Every role placed"), K && OldK && Z))
	{
		TestTrue(TEXT("The new leader at the corner, EdgeInsetCm short of it"), K->Position.Equals(FVector2D(-560.0, -65.0), 0.5));
		TestTrue(TEXT("The old leader second (1 m + the at-end gap behind him)"), OldK->Position.Equals(FVector2D(-430.0, -65.0), 0.5));
		TestTrue(TEXT("Z third, a spacing further back"), Z->Position.Equals(FVector2D(-330.0, -65.0), 0.5));
		for (const FFormationSlot& Slot : Plan.StackSlots)
		{
			TestTrue(TEXT("Nobody past the corner"), Slot.Position.X > -600.0);
			TestTrue(TEXT("Everyone on the near face (the layout's line)"), FMath::IsNearlyEqual(Slot.Position.Y, -65.0, 0.5));
			TestTrue(TEXT("Nobody passes the leader (he is the farthest along)"), Slot.Role == EFormationRole::Commander || Slot.Position.X > K->Position.X + 50.0);
		}
		TestTrue(TEXT("The leader's look goes round the corner (west, at the corner past the face)"), FMath::Abs(FMath::FindDeltaAngleDegrees(K->Yaw, 180.f)) < 40.f);
		TestTrue(TEXT("The trailing end looks back along the wall (east, turned out)"), FMath::Abs(FMath::FindDeltaAngleDegrees(Z->Yaw, 0.f)) < 45.f);
	}
	// Decision 3: then the EAST corner from that stack - the lead moves to the east-end man (Z), the order kept, the old leader last.
	TArray<FWallLeadershipMember> Stacked = { LeadMember(EFormationRole::Right, -430.0), LeadMember(EFormationRole::Commander, -560.0), LeadMember(EFormationRole::Rear, -330.0) };
	const FWallLeadershipPlan East = FormationWallLeadership::Plan(LeadWalls(), FVector2D(-560.0, -65.0), LeadNormalYaw, LeadEastEdge, Stacked);
	TestEqual(TEXT("East peek from the west stack: the east-end man (Z) leads"), East.Leader, 2);
	TestTrue(TEXT("... the old leader (the west corner man) gives up K and takes Z's role"),
		East.Roles.Num() == 3 && East.Roles[2] == EFormationRole::Commander && East.Roles[1] == EFormationRole::Rear && East.Roles[0] == EFormationRole::Right);
	TestTrue(TEXT("... the line behind him in the order they stood: P, then the old leader"), East.Order.Num() == 2 && East.Order[0] == 0 && East.Order[1] == 1);
	const FFormationSlot* EK = LeadSlotOf(East.StackSlots, EFormationRole::Commander);
	const FFormationSlot* EP = LeadSlotOf(East.StackSlots, EFormationRole::Right);
	const FFormationSlot* EZ = LeadSlotOf(East.StackSlots, EFormationRole::Rear);
	if (TestTrue(TEXT("East: every role placed"), EK && EP && EZ))
	{
		TestTrue(TEXT("East: the leader 40 cm short of the east end"), EK->Position.Equals(FVector2D(560.0, -65.0), 0.5));
		TestTrue(TEXT("East: P second, the old leader third"), EP->Position.Equals(FVector2D(430.0, -65.0), 0.5) && EZ->Position.Equals(FVector2D(330.0, -65.0), 0.5));
	}
	return true;
}

FORMATION_WALL_LEADERSHIP_TEST(FFormationWallLeadershipFallbackTest, "PlanFallbacks")
bool FFormationWallLeadershipFallbackTest::RunTest(const FString&)
{
	using namespace FormationWallLeadershipTestUtil;
	// Decision 7: the west end man knocked down - the Commander (next nearest) leads, the down man last in the line.
	TArray<FWallLeadershipMember> Down = LeadThree();
	Down[1].bAble = false;
	const FWallLeadershipPlan Plan = FormationWallLeadership::Plan(LeadWalls(), LeadLineOrigin, LeadNormalYaw, LeadWestEdge, Down);
	TestEqual(TEXT("The next nearest takes it"), Plan.Leader, 0);
	TestTrue(TEXT("Z behind him, the knocked-down man last"), Plan.Order.Num() == 2 && Plan.Order[0] == 2 && Plan.Order[1] == 1);
	TestTrue(TEXT("The leader peeking himself keeps his roles"), Plan.Roles[0] == EFormationRole::Commander && Plan.Roles[1] == EFormationRole::Right);
	// No world walls (a wall the box gatherer does not read): the corner at the peek edge itself.
	const FWallLeadershipPlan Bare = FormationWallLeadership::Plan(TArray<FFormationWall>(), LeadLineOrigin, LeadNormalYaw, LeadWestEdge, LeadThree());
	const FFormationSlot* K = LeadSlotOf(Bare.StackSlots, EFormationRole::Commander);
	TestTrue(TEXT("No world walls: the corner at the edge, still the stack"), !Bare.bWorldEnd && K && FMath::IsNearlyEqual(K->Position.X, -560.0, 0.5));
	// The leader already at the corner (within SlideMin of it): the move is pushed on, the world end still found.
	const TArray<FWallLeadershipMember> AtEnd = { LeadMember(EFormationRole::Commander, -560.0), LeadMember(EFormationRole::Right, -460.0) };
	const FWallLeadershipPlan Near = FormationWallLeadership::Plan(LeadWalls(), FVector2D(-560.0, -65.0), LeadNormalYaw, LeadWestEdge, AtEnd);
	TestTrue(TEXT("Already at the end: he keeps it, the world end found"), Near.Leader == 0 && Near.bWorldEnd);
	return true;
}

FORMATION_WALL_LEADERSHIP_TEST(FFormationWallLeadershipHoldBehindTest, "HoldBehind")
bool FFormationWallLeadershipHoldBehindTest::RunTest(const FString&)
{
	const FVector2D West(-1.0, 0.0);
	// His place 130 cm behind the leader's place; the leader still 300 cm short of his: the place is held 100 cm behind the leader now.
	const FVector2D Held = FormationWallLeadership::HoldBehind(FVector2D(-430.0, -65.0), FVector2D(-430.0, -65.0), FVector2D(-560.0, -65.0), FVector2D(-260.0, -65.0), West);
	TestTrue(TEXT("Held 130 - 30 cm behind where the leader stands"), Held.Equals(FVector2D(-160.0, -65.0), 0.01));
	// The leader at his place (his corner cover ~20 cm behind it): the place itself.
	const FVector2D Free = FormationWallLeadership::HoldBehind(FVector2D(-430.0, -65.0), FVector2D(-430.0, -65.0), FVector2D(-560.0, -65.0), FVector2D(-540.0, -65.0), West);
	TestTrue(TEXT("The leader at his corner: the place unchanged"), Free.Equals(FVector2D(-430.0, -65.0), 0.01));
	// Already behind the man ahead: unchanged.
	const FVector2D Behind = FormationWallLeadership::HoldBehind(FVector2D(-100.0, -65.0), FVector2D(-430.0, -65.0), FVector2D(-560.0, -65.0), FVector2D(-260.0, -65.0), West);
	TestTrue(TEXT("A target behind him stays"), Behind.Equals(FVector2D(-100.0, -65.0), 0.01));
	// The man to keep behind: the nearest able one before him (a knocked-down man is no stop).
	TestEqual(TEXT("The first behind the leader keeps behind the leader"), FormationWallLeadership::AheadInLine({ true, true, true }, 1), 0);
	TestEqual(TEXT("The third behind the second"), FormationWallLeadership::AheadInLine({ true, true, true }, 2), 1);
	TestEqual(TEXT("... or behind the leader when the second is down"), FormationWallLeadership::AheadInLine({ true, false, true }, 2), 0);
	TestEqual(TEXT("The leader keeps behind nobody"), FormationWallLeadership::AheadInLine({ true, true }, 0), static_cast<int32>(INDEX_NONE));
	return true;
}

FORMATION_WALL_LEADERSHIP_TEST(FFormationWallLeadershipReformTest, "ReformFast")
bool FFormationWallLeadershipReformTest::RunTest(const FString&)
{
	// Item 2 (the game designer's bug report 2026-11-02: the re-form behind the new leader took 6-9 s - crouch-walking, the trailing man
	// walking backwards, the last metre a crawl).
	using namespace FormationWallLeadership;
	const FFormationConfig Config;
	TestTrue(TEXT("crouched, 10 m to his place: stands up for the walk"), ReformStands(1000.0, true, 300.0));
	TestFalse(TEXT("crouched, 2 m: stays crouched (a shuffle along the wall)"), ReformStands(200.0, true, 300.0));
	TestFalse(TEXT("already standing: nothing to do"), ReformStands(1000.0, false, 300.0));
	TestFalse(TEXT("reform_stand_m 0 = off"), ReformStands(1000.0, true, 0.0));
	// The man looking back faces his walk while his place is far (move.rear_turn_lag 150 / rear_turn_back 50), with hysteresis.
	TestTrue(TEXT("far: faces his walk"), ReformRearFacesWalk(1000.0, false, Config));
	TestFalse(TEXT("1 m (not turned yet): keeps looking back"), ReformRearFacesWalk(100.0, false, Config));
	TestTrue(TEXT("1 m (turned): still facing his walk (hysteresis)"), ReformRearFacesWalk(100.0, true, Config));
	TestFalse(TEXT("within rear_turn_back: looks back again"), ReformRearFacesWalk(40.0, true, Config));
	// The steered final approach into a wall place (no path follower crawl).
	TestFalse(TEXT("4 m off: the path follower's way"), FormationRuntime::WallFinalApproach(FVector2D(400.0, 0.0), FVector2D::ZeroVector, 220.f).IsSet());
	const TOptional<FormationRuntime::FWallFinalApproach> Far = FormationRuntime::WallFinalApproach(FVector2D(250.0, 0.0), FVector2D::ZeroVector, 220.f);
	TestTrue(TEXT("2.5 m off: steered at his top speed straight at the slot"), Far.IsSet() && FMath::IsNearlyEqual(Far->Speed, 220.f)
		&& Far->Direction.Equals(FVector2D(-1.0, 0.0), 1e-6));
	const TOptional<FormationRuntime::FWallFinalApproach> Near = FormationRuntime::WallFinalApproach(FVector2D(0.0, 20.0), FVector2D::ZeroVector, 220.f);
	TestTrue(TEXT("20 cm off: never slower than the minimum (no crawl)"), Near.IsSet() && FMath::IsNearlyEqual(Near->Speed, FormationRuntime::WallFinalApproachMinSpeed));
	const TOptional<FormationRuntime::FWallFinalApproach> Mid = FormationRuntime::WallFinalApproach(FVector2D(0.0, 50.0), FVector2D::ZeroVector, 220.f);
	TestTrue(TEXT("50 cm off: distance x gain"), Mid.IsSet() && FMath::IsNearlyEqual(Mid->Speed, 50.f * FormationRuntime::WallFinalApproachGain));
	TestFalse(TEXT("within the done radius: the glide's"), FormationRuntime::WallFinalApproach(FVector2D(0.0, 5.0), FVector2D::ZeroVector, 220.f).IsSet());
	return true;
}

FORMATION_WALL_LEADERSHIP_TEST(FFormationWallLeadershipAlongWallTest, "AlongWallKeepsPose")
bool FFormationWallLeadershipAlongWallTest::RunTest(const FString&)
{
	// The game designer's bug 2026-11-03 (L_Gen_Depot_1: the right-end peek after the left-end one at the same 31 m wall - all three stood
	// up and faced their walk, the rear open): a walk along the same wall keeps the crouch and the rear look; leaving the wall does not.
	using namespace FormationWallLeadership;
	using namespace FormationWallLeadershipTestUtil;
	const FFormationConfig Config;
	const FVector2D Place(500.0, -65.0);
	TestTrue(TEXT("31 m along the wall line (on it): along the wall"), ReformAlongWall(FVector2D(-2600.0, -65.0), Place, LeadNormalYaw, 120.0));
	TestTrue(TEXT("a man 1 m off the face line (stepped out a little): still along the wall"), ReformAlongWall(FVector2D(-1000.0, -165.0), Place, LeadNormalYaw, 120.0));
	TestFalse(TEXT("a man in a cover 4 m out in front of the wall: leaves it"), ReformAlongWall(FVector2D(-200.0, -465.0), Place, LeadNormalYaw, 120.0));
	TestFalse(TEXT("reform_along_wall_off_m 0: item 2's rules everywhere"), ReformAlongWall(FVector2D(-2600.0, -65.0), Place, LeadNormalYaw, 0.0));
	TestFalse(TEXT("along the wall, crouched, 31 m: no stand-up"), ReformStands(3100.0, true, 300.0, true));
	TestTrue(TEXT("off the wall, crouched, 4 m: stands up for the walk (item 2)"), ReformStands(400.0, true, 300.0, false));
	TestFalse(TEXT("along the wall, 31 m: the rear-looker keeps looking back"), ReformRearFacesWalk(3100.0, false, Config, true));
	TestFalse(TEXT("along the wall, already turned: looks back again at once"), ReformRearFacesWalk(3100.0, true, Config, true));
	TestTrue(TEXT("off the wall, far: faces his walk (item 2)"), ReformRearFacesWalk(1000.0, false, Config, false));
	return true;
}

FORMATION_WALL_LEADERSHIP_TEST(FFormationWallLeadershipGroupPeekTest, "GroupCornerPeek")
bool FFormationWallLeadershipGroupPeekTest::RunTest(const FString&)
{
	// The game designer's bug 2026-11-03 (solo mode, three box-selected in cover along a wall, a corner peek: the middle man - the selection's
	// leader - side-stepped slowly to the corner round the end man): the man nearest the clicked end takes it, the others stack behind him.
	using namespace FormationWallLeadership;
	// Three along the wall at -100 / 0 / +100 (the middle one is the selection's leader), the walls ends at -300 / +300, stand 40 cm in.
	const TArray<double> Along = { 0.0, -100.0, 100.0 };
	const TArray<bool> Able = { true, true, true };
	const FGroupCornerPeekPlan Left = PlanGroupCornerPeek(Along, Able, -300.0, -260.0, 100.0, 30.0);
	TestEqual(TEXT("left end: the left-end man (index 1) peeks, not the middle one"), Left.Peeker, 1);
	TestEqual(TEXT("left end: towards -X"), Left.Toward, -1.0);
	TestTrue(TEXT("left end: the middle man, then the right one behind him"), Left.Order == TArray<int32>({ 0, 2 }));
	TestTrue(TEXT("left end: places 1 m apart behind his stand point"), Left.PlaceAlong.Num() == 2 && FMath::IsNearlyEqual(Left.PlaceAlong[0], -160.0)
		&& FMath::IsNearlyEqual(Left.PlaceAlong[1], -60.0));
	const FGroupCornerPeekPlan Right = PlanGroupCornerPeek(Along, Able, 300.0, 260.0, 100.0, 30.0);
	TestEqual(TEXT("right end: the right-end man (index 2) peeks"), Right.Peeker, 2);
	TestTrue(TEXT("right end: the middle man, then the left one"), Right.Order == TArray<int32>({ 0, 1 }) && FMath::IsNearlyEqual(Right.PlaceAlong[0], 160.0)
		&& FMath::IsNearlyEqual(Right.PlaceAlong[1], 60.0));
	// Already stacked at the end: nobody moves (within KeepCm).
	const FGroupCornerPeekPlan Kept = PlanGroupCornerPeek({ 250.0, 150.0, 60.0 }, Able, 300.0, 260.0, 100.0, 30.0);
	TestTrue(TEXT("a stack already at the end: the others stay where they are"), Kept.Peeker == 0 && FMath::IsNearlyEqual(Kept.PlaceAlong[0], 150.0)
		&& FMath::IsNearlyEqual(Kept.PlaceAlong[1], 60.0));
	// The end man cannot (knocked down): the next nearest takes it; the unable one stays out of the line.
	const FGroupCornerPeekPlan Down = PlanGroupCornerPeek(Along, { true, false, true }, -300.0, -260.0, 100.0, 30.0);
	TestTrue(TEXT("the left-end man down: the middle man peeks, the right one behind him, the down man not in the line"), Down.Peeker == 0
		&& Down.Order == TArray<int32>({ 2 }));
	TestEqual(TEXT("nobody able: no plan"), PlanGroupCornerPeek(Along, { false, false, false }, 300.0, 260.0, 100.0, 30.0).Peeker, static_cast<int32>(INDEX_NONE));
	return true;
}

FORMATION_WALL_LEADERSHIP_TEST(FFormationWallMoveLeaderTest, "MoveNearestLeads")
bool FFormationWallMoveLeaderTest::RunTest(const FString& Parameters)
{
	using namespace FormationWallLeadership;
	// The game designer's PIE 2026-11-08 (L_Gen_Depot_1): the squad behind a wall, both corners held, the right-corner man leading; a move to
	// the LEFT - he walked round the wall's left end, dragging the next man. Routes (nav) to a point beyond the left end: left man 4 m, middle
	// 9 m, the leader (right corner) 16 m.
	const TArray<double> Routes{ 1600.0, 900.0, 400.0 }; // [0] the leader (right), [1] the middle man, [2] the left-corner man
	const TArray<bool> All{ true, true, true };
	TestEqual(TEXT("the left-corner man, nearest the destination, takes the lead"), PickMoveLeader(Routes, All, 0), 2);
	TestEqual(TEXT("a move to the right: the leader (nearest) keeps it"), PickMoveLeader({ 300.0, 900.0, 1500.0 }, All, 0), 0);
	TestEqual(TEXT("a near tie (within 1.5 m): the leader keeps it - no leader hopping"), PickMoveLeader({ 1000.0, 1200.0, 900.0 }, All, 0), 0);
	TestEqual(TEXT("the nearest can't lead (knocked down): the next nearest"), PickMoveLeader(Routes, { true, true, false }, 0), 1);
	TestEqual(TEXT("the leader can't lead: the nearest able"), PickMoveLeader({ 100.0, 900.0, 400.0 }, { false, true, true }, 0), 2);
	TestEqual(TEXT("no route for the nearest (< 0): skipped"), PickMoveLeader({ 1600.0, 900.0, -1.0 }, All, 0), 1);
	TestEqual(TEXT("nobody able: none"), PickMoveLeader(Routes, { false, false, false }, 0), static_cast<int32>(INDEX_NONE));
	// At a wall: the wall order / layout, a corner peek, the two-corner hold; the open ground keeps its leader.
	TestTrue(TEXT("the two-corner hold is at a wall"), IsAtWallForMoveLead(false, false, true, true));
	TestTrue(TEXT("the wall layout in force"), IsAtWallForMoveLead(false, true, false, false));
	TestTrue(TEXT("the wall order"), IsAtWallForMoveLead(true, false, false, false));
	TestFalse(TEXT("open ground: no hand-over"), IsAtWallForMoveLead(false, false, false, false));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
