// CodexTactics.Formation.LeaderCorner.* - the leader takes the corner himself (game designer decision 2026-11-17, TASK_BOARD F-2 option B;
// Formation/FormationLeaderCornerRules): which free wall end the leader's cover slot claims, the follower places kept behind him instead of
// at that corner (the CoverSmoke flake, Gemini report 075 point 2), and the corner watch of that end handed to the leader. UE-only.

#include "Misc/AutomationTest.h"
#include "Formation/FormationLeaderCornerRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define LEADER_CORNER_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Formation.LeaderCorner." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace LeaderCornerTestLocal
{
	/** A 10 m wall along X, 20 cm thick, its free end B at the origin; the squad on its +Y side. */
	FFormationWall MakeCornerWall(const FVector2D& A, const FVector2D& B, float Thickness = 20.f)
	{
		FFormationWall Wall;
		Wall.A = A;
		Wall.B = B;
		Wall.Thickness = Thickness;
		return Wall;
	}

	TArray<FFormationWall> OneWall()
	{
		return { MakeCornerWall(FVector2D(-1000.0, 0.0), FVector2D(0.0, 0.0)) };
	}

	FFormationSlot CornerMember(EFormationRole Role, const FVector2D& Position)
	{
		FFormationSlot Slot;
		Slot.Role = Role;
		Slot.Position = Position;
		return Slot;
	}

	/** The leader's corner cover slot: 28 cm short of the end (the stand-off), 45 cm off the wall's centre line. */
	const FVector2D CornerSlot(-28.0, 45.0);
}

LEADER_CORNER_TEST(FLeaderCornerClaimTest, "ClaimsTheFreeEndNearHisSlot")
bool FLeaderCornerClaimTest::RunTest(const FString& Parameters)
{
	using namespace FormationLeaderCornerRules;
	using namespace LeaderCornerTestLocal;
	const TArray<FFormationWall> Walls = OneWall();
	const TOptional<FFormationLeaderCorner> Claim = ClaimedEnd(Walls, CornerSlot);
	if (!TestTrue(TEXT("a cover slot 28 cm short of the free end claims it"), Claim.IsSet()))
	{
		return false;
	}
	TestEqual(TEXT("... wall 0"), Claim->Wall, 0);
	TestEqual(TEXT("... its end B"), Claim->End, 1);
	TestTrue(TEXT("... the end point"), Claim->EndPoint.Equals(FVector2D(0.0, 0.0), 0.01));
	TestTrue(TEXT("... along the wall towards the end"), Claim->Along.Equals(FVector2D(1.0, 0.0), 1e-4));
	TestTrue(TEXT("... out to the slot's side"), Claim->Out.Equals(FVector2D(0.0, 1.0), 1e-4));
	TestTrue(TEXT("140 cm short of the end still claims it (~1.5 m)"), ClaimedEnd(Walls, FVector2D(-140.0, 45.0)).IsSet());
	TestFalse(TEXT("160 cm short of the end does not"), ClaimedEnd(Walls, FVector2D(-160.0, 45.0)).IsSet());
	TestFalse(TEXT("mid-wall: no corner"), ClaimedEnd(Walls, FVector2D(-500.0, 45.0)).IsSet());
	TestFalse(TEXT("a slot 2 m off the wall is no cover slot at it"), ClaimedEnd(Walls, FVector2D(-28.0, 200.0)).IsSet());
	const TOptional<FFormationLeaderCorner> Far = ClaimedEnd(Walls, FVector2D(-28.0, -45.0));
	TestTrue(TEXT("the far side's slot claims the same end ..."), Far.IsSet() && Far->End == 1);
	TestTrue(TEXT("... out to that side"), Far.IsSet() && Far->Out.Equals(FVector2D(0.0, -1.0), 1e-4));
	TestTrue(TEXT("the slot just round the corner (40 cm past) claims it"), ClaimedEnd(Walls, FVector2D(40.0, 20.0)).IsSet());
	TestFalse(TEXT("1 m past the end: no claim"), ClaimedEnd(Walls, FVector2D(100.0, 20.0)).IsSet());
	return true;
}

LEADER_CORNER_TEST(FLeaderCornerJointTest, "NoClaimAtAJointOrAProbePiece")
bool FLeaderCornerJointTest::RunTest(const FString& Parameters)
{
	using namespace FormationLeaderCornerRules;
	using namespace LeaderCornerTestLocal;
	TArray<FFormationWall> Walls = OneWall();
	Walls.Add(MakeCornerWall(FVector2D(0.0, -300.0), FVector2D(0.0, 300.0))); // a T at the end: a room's corner, nothing to look round
	TestFalse(TEXT("the end is a joint: no claim"), ClaimedEnd(Walls, CornerSlot).IsSet());
	TArray<FFormationWall> Probe = OneWall();
	Probe[0].bProbeHit = true;
	TestFalse(TEXT("a probe hit's piece has no corners"), ClaimedEnd(Probe, CornerSlot).IsSet());
	TestFalse(TEXT("no walls: no claim"), ClaimedEnd(TArray<FFormationWall>(), CornerSlot).IsSet());
	return true;
}

LEADER_CORNER_TEST(FLeaderCornerPendingFirstTest, "PendingSlotFirst")
bool FLeaderCornerPendingFirstTest::RunTest(const FString& Parameters)
{
	using namespace FormationLeaderCornerRules;
	using namespace LeaderCornerTestLocal;
	const TArray<FFormationWall> Walls = OneWall();
	const TOptional<FFormationLeaderCorner> Pending = ClaimedEndOf(Walls, TOptional<FVector2D>(CornerSlot), TOptional<FVector2D>(FVector2D(-500.0, 45.0)));
	TestTrue(TEXT("in cover mid-wall, on his way to the corner slot: the corner is claimed"), Pending.IsSet() && Pending->Slot.Equals(CornerSlot, 0.01));
	const TOptional<FFormationLeaderCorner> Current = ClaimedEndOf(Walls, TOptional<FVector2D>(FVector2D(-500.0, 45.0)), TOptional<FVector2D>(CornerSlot));
	TestTrue(TEXT("in cover at the corner, a shimmy away still pending: still claimed by the slot he stands in"), Current.IsSet());
	TestFalse(TEXT("no cover slot at all: nothing claimed"), ClaimedEndOf(Walls, TOptional<FVector2D>(), TOptional<FVector2D>()).IsSet());
	return true;
}

LEADER_CORNER_TEST(FLeaderCornerKeepBehindTest, "FollowerPlacesKeptBehindHim")
bool FLeaderCornerKeepBehindTest::RunTest(const FString& Parameters)
{
	using namespace FormationLeaderCornerRules;
	using namespace LeaderCornerTestLocal;
	const TArray<FFormationWall> Walls = OneWall();
	const FFormationLeaderCorner Claim = ClaimedEnd(Walls, CornerSlot).GetValue();
	// CoverSmoke 075 diag3: the leader in cover 107 cm short of the end, shimmying to the corner slot; the column's point place 38 cm short of it.
	// CoverSmoke 075 diag3: the leader in cover 107 cm short of the end, shimmying to the corner slot; the column's point place 38 cm short of it.
	const FVector2D Column(-38.0, 60.0);
	TestTrue(TEXT("the column place between his slot and the end is at the claimed corner"), IsAtClaimedCorner(Claim, Column));
	const FVector2D Kept = KeepBehind(Claim, Column);
	TestTrue(TEXT("... kept at least KeepBackCm behind his slot"), Kept.X <= CornerSlot.X - KeepBackCm + 0.01);
	TestTrue(TEXT("... on his side of the wall, as far out as it was"), FMath::IsNearlyEqual(Kept.Y, 60.0, 0.01));
	const FVector2D Round = KeepBehind(Claim, FVector2D(50.0, 30.0));
	TestTrue(TEXT("a place round the corner goes behind him too"), Round.X <= CornerSlot.X - KeepBackCm + 0.01);
	TestTrue(TEXT("... off the wall's body by the slot offset at least"), Round.Y >= 0.5 * Claim.Thickness + MinOutCm - 0.01);
	// The mirror through his slot: a place far round the corner lands as far behind.
	const FVector2D Near = KeepBehind(Claim, FVector2D(-60.0, 60.0));
	const FVector2D FarAhead = KeepBehind(Claim, FVector2D(150.0, 60.0));
	TestTrue(TEXT("a place farther ahead lands farther behind (a column's order kept)"), FarAhead.X < Near.X - 1.0);
	const FVector2D FarSide = KeepBehind(Claim, FVector2D(-50.0, -60.0));
	TestTrue(TEXT("a place on the wall's far side by the end comes round to his side, behind him"), FarSide.Y > 0.0 && FarSide.X <= CornerSlot.X - KeepBackCm + 0.01);
	const FVector2D Behind(-300.0, 60.0);
	TestTrue(TEXT("a place already behind him is not touched"), KeepBehind(Claim, Behind).Equals(Behind, 0.01));
	const FVector2D Stack(-158.0, 60.0);
	TestTrue(TEXT("a corner stack's place lined up behind his slot is not touched"), KeepBehind(Claim, Stack).Equals(Stack, 0.01));
	const FVector2D Open(-38.0, 400.0);
	TestTrue(TEXT("a place 4 m out from the wall is no corner place"), KeepBehind(Claim, Open).Equals(Open, 0.01));
	TestTrue(TEXT("a place 50 cm behind his slot is still too close"), IsAtClaimedCorner(Claim, FVector2D(-78.0, 60.0)));
	TestFalse(TEXT("... one 1 m behind it is not"), IsAtClaimedCorner(Claim, FVector2D(-128.0, 60.0)));
	return true;
}

LEADER_CORNER_TEST(FLeaderCornerSlotsTest, "SlotsKeepTheLeaderAndLooks")
bool FLeaderCornerSlotsTest::RunTest(const FString& Parameters)
{
	using namespace FormationLeaderCornerRules;
	using namespace LeaderCornerTestLocal;
	const FFormationLeaderCorner Claim = ClaimedEnd(OneWall(), CornerSlot).GetValue();
	const FVector2D Leader(-107.0, 45.0);
	TArray<FFormationSlot> Slots = { CornerMember(EFormationRole::Commander, Leader), CornerMember(EFormationRole::Right, FVector2D(-38.0, 60.0)),
		CornerMember(EFormationRole::Rear, FVector2D(-400.0, 60.0)) };
	Slots[1].Yaw = 33.f;
	const int32 Moved = KeepSlotsBehind(Claim, Slots);
	TestEqual(TEXT("one place moved"), Moved, 1);
	TestTrue(TEXT("... never onto the leader where he stands (KeepBackCm behind him)"), Slots[1].Position.X <= Leader.X - KeepBackCm + 0.01);
	TestTrue(TEXT("the leader's own slot is his"), Slots[0].Position.Equals(Leader, 0.01));
	TestTrue(TEXT("the moved place keeps its look"), FMath::IsNearlyEqual(Slots[1].Yaw, 33.f));
	TestTrue(TEXT("the place behind stays"), Slots[2].Position.Equals(FVector2D(-400.0, 60.0), 0.01));
	// The wall layout round the leader at his corner slot: the end-side man mirrored would land on the man behind - he goes on past him.
	TArray<FFormationSlot> Layout = { CornerMember(EFormationRole::Commander, CornerSlot), CornerMember(EFormationRole::Right, FVector2D(-10.0, 60.0)),
		CornerMember(EFormationRole::Left, FVector2D(-150.0, 60.0)) };
	TestEqual(TEXT("the wall layout: the end-side place moved"), KeepSlotsBehind(Claim, Layout), 1);
	TestTrue(TEXT("... the other place stays"), Layout[2].Position.Equals(FVector2D(-150.0, 60.0), 0.01));
	TestTrue(TEXT("... the moved one KeepBackCm behind it, not on it"), FMath::IsNearlyEqual(Layout[1].Position.X, -150.0 - KeepBackCm, 0.01)
		&& FVector2D::Distance(Layout[1].Position, Layout[2].Position) >= KeepBackCm - 0.01);
	return true;
}

LEADER_CORNER_TEST(FLeaderCornerWatchTest, "LeaderTakesTheCornerWatch")
bool FLeaderCornerWatchTest::RunTest(const FString& Parameters)
{
	using namespace FormationLeaderCornerRules;
	using namespace LeaderCornerTestLocal;
	const FFormationConfig Config;
	const TArray<FFormationWall> Walls = OneWall();
	// Walking towards the end (heading 0), the right flanker closer to it: the plain watch is his.
	const FVector2D Leader(-280.0, 45.0);
	const TArray<FFormationSlot> Members = { CornerMember(EFormationRole::Commander, Leader), CornerMember(EFormationRole::Right, FVector2D(-150.0, 60.0)) };
	const FFormationCornerResult Plain = Watch(Walls, Leader, 0.f, Members, FFormationCornerState(), TOptional<FFormationLeaderCorner>(), Config);
	if (!TestTrue(TEXT("without a claim the follower nearest the end watches it (the plain rule)"), Plain.State.Left.IsSet()
		&& Plain.State.Left->Role == EFormationRole::Right))
	{
		return false;
	}
	const TOptional<FFormationLeaderCorner> Claim = ClaimedEnd(Walls, CornerSlot);
	const FFormationCornerResult Claimed = Watch(Walls, Leader, 0.f, Members, FFormationCornerState(), Claim, Config);
	TestTrue(TEXT("with his cover slot at that end the watch is the leader's"), Claimed.State.Left.IsSet() && Claimed.State.Left->Role == EFormationRole::Commander);
	TestEqual(TEXT("... one look"), Claimed.Looks.Num(), 1);
	TestTrue(TEXT("... the leader's, at the corner"), Claimed.Looks.Num() == 1 && Claimed.Looks[0].Role == EFormationRole::Commander
		&& FMath::Abs(FMath::FindDeltaAngleDegrees(Claimed.Looks[0].Yaw, 0.f)) < 30.f);
	const FFormationCornerResult Held = Watch(Walls, Leader, 0.f, Members, Plain.State, Claim, Config);
	TestTrue(TEXT("a follower already watching it hands it over"), Held.State.Left.IsSet() && Held.State.Left->Role == EFormationRole::Commander);
	const FFormationCornerResult Again = Watch(Walls, Leader, 0.f, Members, Claimed.State, TOptional<FFormationLeaderCorner>(), Config);
	TestTrue(TEXT("the claim over: the leader keeps the watch he has (the plain rule's memory)"), Again.State.Left.IsSet() && Again.State.Left->Role == EFormationRole::Commander);
	return true;
}

LEADER_CORNER_TEST(FLeaderCornerOtherSideTest, "OtherSideWatchStaysAFollowers")
bool FLeaderCornerOtherSideTest::RunTest(const FString& Parameters)
{
	using namespace FormationLeaderCornerRules;
	using namespace LeaderCornerTestLocal;
	const FFormationConfig Config;
	// A corridor exit: two walls, two free ends (left (0, 0), right (0, 200)). The plain watch: the right flanker the left end, the leader the right.
	const TArray<FFormationWall> Walls = { MakeCornerWall(FVector2D(-1000.0, 0.0), FVector2D(0.0, 0.0)), MakeCornerWall(FVector2D(-1000.0, 200.0), FVector2D(0.0, 200.0)) };
	const FVector2D Leader(-200.0, 150.0);
	const TArray<FFormationSlot> Members = { CornerMember(EFormationRole::Commander, Leader), CornerMember(EFormationRole::Right, FVector2D(-100.0, 30.0)),
		CornerMember(EFormationRole::Rear, FVector2D(-400.0, 100.0)) };
	const FFormationCornerResult Plain = Watch(Walls, Leader, 0.f, Members, FFormationCornerState(), TOptional<FFormationLeaderCorner>(), Config);
	if (!TestTrue(TEXT("plain: the right flanker the left end, the leader the right end"), Plain.State.Left.IsSet() && Plain.State.Right.IsSet()
		&& Plain.State.Left->Role == EFormationRole::Right && Plain.State.Right->Role == EFormationRole::Commander))
	{
		return false;
	}
	const TOptional<FFormationLeaderCorner> Claim = ClaimedEnd(Walls, CornerSlot);
	if (!TestTrue(TEXT("his slot at the left end claims it"), Claim.IsSet() && Claim->Wall == 0))
	{
		return false;
	}
	const FFormationCornerResult Claimed = Watch(Walls, Leader, 0.f, Members, FFormationCornerState(), Claim, Config);
	TestTrue(TEXT("the claimed left end: the leader"), Claimed.State.Left.IsSet() && Claimed.State.Left->Role == EFormationRole::Commander);
	TestTrue(TEXT("the right end: a follower (the leader cannot watch both)"), Claimed.State.Right.IsSet() && Claimed.State.Right->Role != EFormationRole::Commander);
	TestEqual(TEXT("two looks"), Claimed.Looks.Num(), 2);
	return true;
}

#endif
