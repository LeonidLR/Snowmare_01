// G016: window cover, pure. Own floor and the other, both faces, a solid wall, the corner, the interval, the glass round.
#include "Misc/AutomationTest.h"

#include "Interactables/WindowBreakage.h"
#include "Interactables/WindowOrderRules.h"
#include "Tactics/WindowCoverRules.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using namespace WindowCoverRules;

	FWindowCoverOpening Opening(int32 Floor, uint32 WallId, const FVector& Normal)
	{
		FWindowCoverOpening Out;
		Out.bPresent = true;
		Out.Floor = Floor;
		Out.WallId = WallId;
		Out.WallNormal = Normal;
		Out.AlongMinCm = -50.f;
		Out.AlongMaxCm = 50.f;
		Out.Opening = FBox(FVector(-40.f, -10.f, 100.f), FVector(40.f, 10.f, 240.f));
		Out.BuildingId = TEXT("house");
		return Out;
	}

	FWindowCoverSlotFacts SlotAt(float AlongCm, bool bCorner)
	{
		FWindowCoverSlotFacts Slot;
		Slot.Floor = 0;
		Slot.WallId = 7;
		Slot.WallNormal = FVector::XAxisVector;
		Slot.AlongCm = AlongCm;
		Slot.FirePoint = FVector(0.f, 0.f, 160.f);
		Slot.bAtCorner = bCorner;
		Slot.BuildingId = TEXT("house");
		return Slot;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindowCoverMatchTest, "CodexTactics.Tactics.WindowCover.Match",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindowCoverMatchTest::RunTest(const FString& Parameters)
{
	using namespace WindowCoverRules;
	const FWindowCoverOpening Own = Opening(0, 7, FVector::XAxisVector);
	const FWindowCoverMatch Confirmed = Match(Own, SlotAt(0.f, false));
	TestTrue(TEXT("own floor, in the opening"), Confirmed.bConfirmed && Confirmed.bInsideInterval && Confirmed.Reject == EWindowCoverReject::None);

	FWindowCoverOpening OtherFace = Own;
	OtherFace.WallNormal = -FVector::XAxisVector;
	TestTrue(TEXT("the other face of the same wall"), Match(OtherFace, SlotAt(0.f, false)).bConfirmed);

	FWindowCoverSlotFacts OtherFloor = SlotAt(0.f, false);
	OtherFloor.Floor = 1;
	const FWindowCoverMatch Floor = Match(Own, OtherFloor);
	TestFalse(TEXT("another floor is not this window"), Floor.bConfirmed);
	TestTrue(TEXT("another floor"), Floor.Reject == EWindowCoverReject::OtherFloor);

	FWindowCoverOpening Parallel = Own;
	Parallel.WallNormal = FVector::YAxisVector;
	Parallel.WallId = 8;
	const FWindowCoverMatch Wall = Match(Parallel, SlotAt(0.f, false));
	TestFalse(TEXT("a parallel wall is not this window"), Wall.bConfirmed);
	TestTrue(TEXT("other wall"), Wall.Reject == EWindowCoverReject::OtherWall);

	FWindowCoverOpening Solid;
	const FWindowCoverMatch NoOpening = Match(Solid, SlotAt(0.f, false));
	TestFalse(TEXT("a solid wall has no window"), NoOpening.bConfirmed);
	TestTrue(TEXT("solid"), NoOpening.Reject == EWindowCoverReject::SolidWall);

	const FWindowCoverMatch Corner = Match(Own, SlotAt(0.f, true));
	TestFalse(TEXT("the corner keeps the side peek"), Corner.bConfirmed);
	TestTrue(TEXT("the corner is still in the interval"), Corner.bInsideInterval);
	TestTrue(TEXT("corner fallback"), Corner.Reject == EWindowCoverReject::CornerFallback);

	const FWindowCoverMatch Outside = Match(Own, SlotAt(80.f, false));
	TestFalse(TEXT("past the opening"), Outside.bConfirmed || Outside.bInsideInterval);
	TestTrue(TEXT("outside the interval"), Outside.Reject == EWindowCoverReject::OutsideInterval);
	TestTrue(TEXT("shimmy crosses into the opening"), CrossedOpeningBoundary(PlaceOnInterval(80.f, -50.f, 50.f), PlaceOnInterval(0.f, -50.f, 50.f)));
	TestFalse(TEXT("still inside"), CrossedOpeningBoundary(EWindowIntervalPlace::Inside, PlaceOnInterval(10.f, -50.f, 50.f)));

	FWindowCoverSlotFacts Frame = SlotAt(0.f, false);
	Frame.FirePoint = FVector(0.f, 0.f, 90.f);
	const FWindowCoverMatch Point = Match(Own, Frame);
	TestTrue(TEXT("in the interval but the fire point is on the frame"), Point.bInsideInterval && !Point.bConfirmed);
	TestTrue(TEXT("fire point outside"), Point.Reject == EWindowCoverReject::FirePointOutside);

	TestFalse(TEXT("different buildings"), SameWall(7, 7, FVector::XAxisVector, FVector::XAxisVector, TEXT("a"), TEXT("b")));
	TestTrue(TEXT("shares a floor by tag"), SharesFloor(0, 0, 0.f, 300.f));
	TestFalse(TEXT("another storey by tag"), SharesFloor(0, 1, 0.f, 10.f));
	TestFalse(TEXT("no tags, a storey up"), SharesFloor(INDEX_NONE, INDEX_NONE, 0.f, 300.f));
	TestTrue(TEXT("no tags, the same sill"), SharesFloor(INDEX_NONE, INDEX_NONE, 90.f, 100.f));

	const FVector Normal = FVector::XAxisVector;
	const FVector WallPoint(0.f, 0.f, 0.f);
	const FVector Muzzle = WallPoint + Normal * 45.f;
	TestTrue(TEXT("the muzzle's through-wall ray crosses at the wall"), FirePointOnPlane(Muzzle, Normal, WallPoint).Equals(WallPoint, 0.1f));
	TestTrue(TEXT("a neighbor sill"), IsNeighborSill(FVector(100.f, 0.f, 0.f), FVector::ZeroVector));
	TestFalse(TEXT("a sill further than the landing"), IsNeighborSill(FVector(400.f, 0.f, 0.f), FVector::ZeroVector));
	const float Limit = PlaneLimitCm(FVector(200.f, 15.f, 150.f), Normal);
	TestEqual(TEXT("plane limit is the face margin plus the opening pad"), Limit, WindowBreakage::FaceMarginCm + WindowBreakage::OpeningPadCm);
	TestTrue(TEXT("the sill in the wall"), OnSameWallPlane(FVector(20.f, 0.f, 100.f), WallPoint, Normal, Limit));
	TestFalse(TEXT("a parallel wall past the pad"), OnSameWallPlane(FVector(80.f, 0.f, 100.f), WallPoint, Normal, Limit));

	const FBox Sill(FVector(-60.f, -10.f, 0.f), FVector(60.f, 10.f, 90.f));
	const FBox Pane = WindowBreakage::MakeOpeningBox(Sill);
	TestTrue(TEXT("the opening box is the old literal"), Pane.Min.Equals(FVector(-70.f, -20.f, 95.f), 0.1f) && Pane.Max.Equals(FVector(70.f, 20.f, 230.f), 0.1f));
	// A crouched muzzle (85 cm) aimed at the capsule centre misses the pane. Through the opening centre, it crosses.
	const FVector CrouchMuzzle(120.f, 0.f, 85.f);
	const FVector LowAim(-200.f, 0.f, 58.5f);
	TestFalse(TEXT("a crouch shot at body height misses the opening"), FMath::LineBoxIntersection(Pane, CrouchMuzzle, LowAim, LowAim - CrouchMuzzle));
	const FVector PaneCenter = Pane.GetCenter();
	const FVector Through = (PaneCenter - CrouchMuzzle).GetSafeNormal();
	const FVector FarAim = PaneCenter + Through * WindowBreakage::ThroughLandingCm;
	TestTrue(TEXT("a crouch shot through the opening centre crosses the pane"), FMath::LineBoxIntersection(Pane, CrouchMuzzle, FarAim, FarAim - CrouchMuzzle));

	TestTrue(TEXT("stopped in the opening: crouch"), WantsCrouch(true, false, false, true));
	TestFalse(TEXT("shimmy does not crouch for the window"), WantsCrouch(true, true, false, false));
	TestFalse(TEXT("the corner does not take the window crouch"), WantsCrouch(true, false, true, true));
	TestTrue(TEXT("aim through the wall: inward"), TurnForAim(FVector::ZeroVector, -FVector::XAxisVector * 100.f, Normal) == EWindowCoverTurn::Inward);
	TestTrue(TEXT("aim on the normal's side: outward"), TurnForAim(FVector::ZeroVector, Normal * 100.f, Normal) == EWindowCoverTurn::Outward);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindowCoverShotTest, "CodexTactics.Tactics.WindowCover.Shot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindowCoverShotTest::RunTest(const FString& Parameters)
{
	using namespace WindowCoverRules;
	FWindowShotQuery Query;
	Query.bConfirmed = true;
	Query.bIsShot = false;
	Query.bThroughOpening = true;
	Query.bBreakable = true;
	Query.Glass = EWindowGlassState::Intact;
	const FWindowShotResult Aim = DecideShot(Query);
	TestFalse(TEXT("aim leaves the glass"), Aim.bSpendRound || Aim.bBreakGlass || Aim.bHitTarget);

	Query.bIsShot = true;
	Query.bConfirmed = false;
	TestFalse(TEXT("a shimmy off the sill spends nothing"), DecideShot(Query).bSpendRound);

	Query.bConfirmed = true;
	Query.bHitsFrameOrSolid = true;
	Query.bThroughOpening = false;
	const FWindowShotResult Frame = DecideShot(Query);
	TestTrue(TEXT("the frame blocks"), Frame.bBlocked);
	TestFalse(TEXT("a blocked shot spends no round"), Frame.bSpendRound || Frame.bBreakGlass);

	Query.bHitsFrameOrSolid = false;
	Query.bThroughOpening = true;
	int32 Clip = 30;
	const FWindowShotResult First = DecideShot(Query);
	TestTrue(TEXT("the first round breaks the intact glass"), First.bSpendRound && First.bBreakGlass && !First.bHitTarget);
	Clip -= First.bSpendRound ? 1 : 0;
	TestEqual(TEXT("one round spent on the glass"), Clip, 29);

	Query.Glass = EWindowGlassState::Broken;
	Query.GlassRoundsSpent = 1;
	const FWindowShotResult Second = DecideShot(Query);
	TestTrue(TEXT("the next round may hit the target"), Second.bSpendRound && Second.bHitTarget && !Second.bBreakGlass);
	Clip -= Second.bSpendRound ? 1 : 0;
	TestEqual(TEXT("the target round is another cartridge"), Clip, 28);

	Query.Glass = EWindowGlassState::Missing;
	Query.bBreakable = false;
	Query.GlassRoundsSpent = 0;
	const FWindowShotResult Missing = DecideShot(Query);
	TestTrue(TEXT("missing glass: the round goes to the target"), Missing.bHitTarget && !Missing.bBreakGlass);

	FWindowCoverFighter Leader;
	Leader.Id = 1;
	Leader.bOwnSlotConfirmed = true;
	Leader.bLeader = true;
	FWindowCoverFighter Follower;
	Follower.Id = 2;
	Follower.bOwnSlotConfirmed = false;
	Follower.bLeader = false;
	TestTrue(TEXT("the man at the window takes the pose"), TakesWindowPose(Leader));
	TestFalse(TEXT("the neighbour keeps his own slot"), TakesWindowPose(Follower));
	Leader.bLeader = false;
	Follower.bLeader = true;
	TestTrue(TEXT("giving him the lead does not take the window off the sill"), TakesWindowPose(Leader));
	TestFalse(TEXT("the new leader does not inherit the window"), TakesWindowPose(Follower));
	Leader.bOwnSlotConfirmed = false;
	TestFalse(TEXT("leaving the sill clears the pose"), TakesWindowPose(Leader));

	TestFalse(TEXT("a cover shot does not open a climb"), GrantsPassage());
	TestFalse(TEXT("no window order: WindowOrderRules keeps the pane whole"),
		WindowOrderRules::MayTakeWholeWindow(false, false, WindowOrderRules::Get()));
	TestFalse(TEXT("a follower still does not take a whole window"),
		WindowOrderRules::MayTakeWholeWindow(true, true, WindowOrderRules::Get()));
	return true;
}

#endif
