// CodexTactics.Tactics.ClickPreview.* - R17-3 unified building click (designer 2026-11-17): ClickPreviewRules' pure decisions.
// CodexTactics.Tactics.DoorwayBreach.Entry.* - R17-5 doorway breach entry geometry (DoorwayBreachEntryRules).

#include "Tactics/ClickPreviewRules.h"
#include "Tactics/DoorwayBreachEntryRules.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#define CLICK_PREVIEW_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Tactics.ClickPreview." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

#define BREACH_ENTRY_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Tactics.DoorwayBreach.Entry." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace ClickPreviewTestLocal
{
	FClickPreviewPiece Piece(uint64 Component, uint64 Actor, const FBox& Bounds, bool bFloor = false, FName Building = NAME_None)
	{
		FClickPreviewPiece Out;
		Out.ComponentId = Component;
		Out.ActorId = Actor;
		Out.Bounds = Bounds;
		Out.bFloorLike = bFloor;
		Out.Building = Building;
		return Out;
	}

	/** A 4 m wall along X, 20 cm thick, 3 m high, from X0. */
	FBox ClickWall(float X0, float Y0 = 0.f)
	{
		return FBox(FVector(X0, Y0 - 10.f, 0.f), FVector(X0 + 400.f, Y0 + 10.f, 300.f));
	}
}

CLICK_PREVIEW_TEST(FClickPreviewDecideTest, "Decide")
bool FClickPreviewDecideTest::RunTest(const FString&)
{
	using namespace ClickPreviewRules;
	FClickPreviewClickFacts Facts;
	Facts.bSurface = true;
	TestEqual(TEXT("first click on a wall: the ghost"), DecideClick(Facts), EClickPreviewAction::Arm);
	Facts.bSurface = false;
	TestEqual(TEXT("first click on the ground: the ordinary click"), DecideClick(Facts), EClickPreviewAction::PassThrough);
	Facts.bArmed = true;
	Facts.bOnArmedStructure = true;
	TestEqual(TEXT("second click on the same building: carried out"), DecideClick(Facts), EClickPreviewAction::Execute);
	Facts.bOnArmedStructure = false;
	Facts.bOnGhost = true;
	TestEqual(TEXT("second click on the ghost's feet: carried out"), DecideClick(Facts), EClickPreviewAction::Execute);
	Facts.bOnGhost = false;
	TestEqual(TEXT("a ground click away from it: cancel + the move"), DecideClick(Facts), EClickPreviewAction::CancelAndPass);
	Facts.bSurface = true;
	TestEqual(TEXT("another building while a ghost is up: its ghost instead"), DecideClick(Facts), EClickPreviewAction::Arm);
	Facts.bBypass = true;
	Facts.bOnArmedStructure = true;
	TestEqual(TEXT("Ctrl / Shift / Alt on the same wall: cancel + the ordinary click"), DecideClick(Facts), EClickPreviewAction::CancelAndPass);
	Facts.bArmed = false;
	TestEqual(TEXT("a modifier click with no ghost: the ordinary click"), DecideClick(Facts), EClickPreviewAction::PassThrough);
	FClickPreviewClickFacts Off;
	Off.bEnabled = false;
	Off.bSurface = true;
	TestEqual(TEXT("Codex.Click.Preview 0: the ordinary click"), DecideClick(Off), EClickPreviewAction::PassThrough);
	return true;
}

CLICK_PREVIEW_TEST(FClickPreviewDoubleClickTest, "DoubleClick")
bool FClickPreviewDoubleClickTest::RunTest(const FString&)
{
	using namespace ClickPreviewRules;
	TestTrue(TEXT("0.2 s, 5 px: double"), IsDoubleClick(0.2, 5.f, 0.35f, 30.f));
	TestFalse(TEXT("0.5 s: two clicks"), IsDoubleClick(0.5, 5.f, 0.35f, 30.f));
	TestFalse(TEXT("40 px apart: two clicks"), IsDoubleClick(0.1, 40.f, 0.35f, 30.f));
	TestFalse(TEXT("no arming click"), IsDoubleClick(-1.0, 0.f, 0.35f, 30.f));
	TestTrue(TEXT("the same frame (headless): double"), IsDoubleClick(0.0, 0.f, 0.35f, 30.f));
	return true;
}

CLICK_PREVIEW_TEST(FClickPreviewStructureTest, "Structure")
bool FClickPreviewStructureTest::RunTest(const FString&)
{
	using namespace ClickPreviewRules;
	using namespace ClickPreviewTestLocal;
	FClickPreviewStructure Building;
	TestFalse(TEXT("nothing armed: on no structure"), IsOnStructure(Building, Piece(1, 1, ClickWall(0.f))));
	AddPiece(Building, Piece(1, 1, ClickWall(0.f)));
	TestTrue(TEXT("the same wall piece"), IsOnStructure(Building, Piece(1, 1, ClickWall(0.f))));
	// A building corner: the cross wall (another actor) butts against the wall's end.
	const FBox Cross(FVector(400.f, -10.f, 0.f), FVector(420.f, 400.f, 300.f));
	TestTrue(TEXT("the cross wall at the corner (touching) is the same building"), IsOnStructure(Building, Piece(2, 2, Cross)));
	// A lintel over a door gap 80 cm on (touching the next piece's bounds within the tolerance).
	TestTrue(TEXT("the wall piece past a 20 cm gap"), IsOnStructure(Building, Piece(3, 3, ClickWall(420.f))));
	TestFalse(TEXT("another building 2 m away"), IsOnStructure(Building, Piece(4, 4, ClickWall(0.f, 300.f))));
	FClickPreviewStructure Tagged;
	AddPiece(Tagged, Piece(1, 1, ClickWall(0.f), false, FName(TEXT("building=b1"))));
	TestTrue(TEXT("the same building id far along"), IsOnStructure(Tagged, Piece(5, 5, ClickWall(0.f, 2000.f), false, FName(TEXT("building=b1")))));
	// The floor next to / inside the building is a move, even though it touches the wall.
	const FBox Floor(FVector(-500.f, -500.f, -20.f), FVector(900.f, 900.f, 0.f));
	TestFalse(TEXT("the floor beside it (floor-like): a move"), IsOnStructure(Building, Piece(6, 6, Floor, true)));
	// The ground (a huge piece) never joins the structure by its bounds.
	const FBox Ground(FVector(-10000.f, -10000.f, -50.f), FVector(10000.f, 10000.f, 0.f));
	TestTrue(TEXT("a 200 m ground piece is a ground piece"), IsGroundPiece(Piece(7, 7, Ground)));
	TestFalse(TEXT("the ground hit as a wall-like piece: still not the building"), IsOnStructure(Building, Piece(7, 7, Ground)));
	// The wall's own top: floor-like, but the ghost was shown on it.
	FClickPreviewStructure Obstacle;
	AddPiece(Obstacle, Piece(9, 9, ClickWall(0.f), true));
	TestTrue(TEXT("the obstacle's own top again"), IsOnStructure(Obstacle, Piece(9, 9, ClickWall(0.f), true)));
	FClickPreviewStructure GroundArmed;
	AddPiece(GroundArmed, Piece(7, 7, Ground));
	TestFalse(TEXT("a ground piece adds no bounds"), GroundArmed.Bounds.IsValid != 0);
	return true;
}

CLICK_PREVIEW_TEST(FClickPreviewGhostClickTest, "GhostClick")
bool FClickPreviewGhostClickTest::RunTest(const FString&)
{
	using namespace ClickPreviewRules;
	const FVector Feet(100.f, 100.f, 0.f);
	TestTrue(TEXT("the ground 40 cm from the ghost's feet"), IsClickOnGhost(FVector(140.f, 100.f, 2.f), true, Feet));
	TestFalse(TEXT("the ground 80 cm away"), IsClickOnGhost(FVector(180.f, 100.f, 0.f), true, Feet));
	TestFalse(TEXT("a wall face (not the ground)"), IsClickOnGhost(FVector(120.f, 100.f, 50.f), false, Feet));
	TestFalse(TEXT("the floor above (another storey)"), IsClickOnGhost(FVector(110.f, 100.f, 300.f), true, Feet));
	return true;
}

CLICK_PREVIEW_TEST(FClickPreviewLowerHitTest, "LowerHighHit")
bool FClickPreviewLowerHitTest::RunTest(const FString&)
{
	using namespace ClickPreviewRules;
	TestTrue(TEXT("a wall face 4 m up a two-storey building: traced again at the chest"), ShouldLowerWallHit(400.f, 0.f, 0.f));
	TestFalse(TEXT("a wall face at 1.2 m stays"), ShouldLowerWallHit(120.f, 0.f, 0.f));
	TestFalse(TEXT("a roof / top hit stays (the wall side rules read it)"), ShouldLowerWallHit(400.f, 1.f, 0.f));
	TestTrue(TEXT("measured from his feet (he stands on a 2 m platform; the face 4 m up)"), ShouldLowerWallHit(600.f, 0.05f, 200.f));
	TestTrue(TEXT("the same face lower"), IsSameFaceLower(FVector2D(0.f, 1.f), FVector2D(0.05f, 1.f), 0.f));
	TestFalse(TEXT("another face lower (a pillar's side)"), IsSameFaceLower(FVector2D(0.f, 1.f), FVector2D(1.f, 0.f), 0.f));
	TestFalse(TEXT("a slope lower"), IsSameFaceLower(FVector2D(0.f, 1.f), FVector2D(0.f, 1.f), 0.6f));
	return true;
}

CLICK_PREVIEW_TEST(FClickPreviewPortalTest, "RayThroughPortal")
bool FClickPreviewPortalTest::RunTest(const FString&)
{
	using namespace ClickPreviewRules;
	// A 120 cm door in a wall along X at Y = 0, outward +Y; the camera up and out (+Y, +Z).
	const FVector Center(0.f, 0.f, 0.f);
	const FVector Tangent(1.f, 0.f, 0.f);
	const FVector Outward(0.f, 1.f, 0.f);
	const FVector Eye(0.f, 1500.f, 1500.f);
	FVector Point;
	TestTrue(TEXT("through the opening onto the floor inside"), RayThroughPortal(Eye, FVector(0.f, -100.f, 0.f), Center, Tangent, Outward, 120.f, 210.f, 40.f, 50.f, Point));
	TestTrue(TEXT("... crossing the plane under the lintel"), Point.Z > 0.f && Point.Z < 210.f && FMath::Abs(Point.Y) < 1.f);
	TestTrue(TEXT("the threshold itself"), RayThroughPortal(Eye, FVector(30.f, 10.f, 0.f), Center, Tangent, Outward, 120.f, 210.f, 40.f, 50.f, Point));
	TestTrue(TEXT("the jamb wall's face 30 cm past the opening's edge (Zone B: +40 cm)"),
		RayThroughPortal(Eye, FVector(90.f, 10.f, 100.f), Center, Tangent, Outward, 120.f, 210.f, 40.f, 50.f, Point));
	TestFalse(TEXT("the wall's face 60 cm past the edge (Zone A: wall cover)"),
		RayThroughPortal(Eye, FVector(120.f, 10.f, 100.f), Center, Tangent, Outward, 120.f, 210.f, 40.f, 50.f, Point));
	TestFalse(TEXT("the lintel above the opening"), RayThroughPortal(Eye, FVector(0.f, 10.f, 260.f), Center, Tangent, Outward, 120.f, 210.f, 40.f, 50.f, Point));
	TestFalse(TEXT("the ground in front of the door, 2 m out"), RayThroughPortal(Eye, FVector(0.f, 200.f, 0.f), Center, Tangent, Outward, 120.f, 210.f, 40.f, 50.f, Point));
	TestFalse(TEXT("a far floor inside seen past the wall beside the door"), RayThroughPortal(Eye, FVector(600.f, -400.f, 0.f), Center, Tangent, Outward, 120.f, 210.f, 40.f, 50.f, Point));
	// Review 2026-11-17 (finding 2): only the opening itself arms the breach - the plane, not a ray through it onto the floor beyond.
	TestTrue(TEXT("arming: the threshold (10 cm out) is at the portal plane"), IsAtPortalPlane(FVector(30.f, 10.f, 0.f), Center, Outward, 50.f));
	TestTrue(TEXT("arming: a jamb face is at the portal plane"), IsAtPortalPlane(FVector(90.f, 10.f, 100.f), Center, Outward, 50.f));
	TestFalse(TEXT("arming: the room's floor 1 m in (a move through the doorway) is not"), IsAtPortalPlane(FVector(0.f, -100.f, 0.f), Center, Outward, 50.f));
	TestFalse(TEXT("arming: the yard 2 m out is not"), IsAtPortalPlane(FVector(0.f, 200.f, 0.f), Center, Outward, 50.f));
	return true;
}

BREACH_ENTRY_TEST(FBreachEntryOrderTest, "Order")
bool FBreachEntryOrderTest::RunTest(const FString&)
{
	using namespace DoorwayBreachEntryRules;
	// The door at the origin in a wall along X, outward +Y: entering = -Y, the entering man's right hand = +X (UE: Z up, X forward, Y right).
	const FVector Center(0.f, 0.f, 0.f);
	const FVector Outward(0.f, 1.f, 0.f);
	TestTrue(TEXT("entering goes -Y"), EntryDirection(Outward).Equals(FVector(0.f, -1.f, 0.f), 0.01f));
	TestTrue(TEXT("its right hand is +X"), EntryRight(Outward).Equals(FVector(1.f, 0.f, 0.f), 0.01f));
	const TArray<EDoorwayBreachSlotRole> Roles = { EDoorwayBreachSlotRole::LeftJamb, EDoorwayBreachSlotRole::RightJamb, EDoorwayBreachSlotRole::CenterPoint,
		EDoorwayBreachSlotRole::RearSecurity };
	// Index 0 stands at +X (the entering man's right), index 1 at -X (his left).
	const TArray<FVector> Positions = { FVector(90.f, 45.f, 0.f), FVector(-90.f, 45.f, 0.f), FVector(0.f, 220.f, 0.f), FVector(60.f, 350.f, 0.f) };
	const TArray<FDoorwayBreachEntryMove> Moves = PlanEntry(Center, Outward, 120.f, Roles, Positions);
	TestEqual(TEXT("three men go in, rear security holds outside"), Moves.Num(), 3);
	if (Moves.Num() == 3)
	{
		TestEqual(TEXT("man 1: the right-hand jamb man"), Moves[0].Participant, 0);
		TestEqual(TEXT("... first"), Moves[0].DelaySeconds, 0.f);
		TestTrue(TEXT("... hooks right (+X) inside (-Y)"), Moves[0].Destination.X > 60.f && Moves[0].Destination.Y < -100.f);
		TestEqual(TEXT("man 2: the left-hand jamb man"), Moves[1].Participant, 1);
		TestEqual(TEXT("... 0.3 s later"), Moves[1].DelaySeconds, StaggerSeconds);
		TestTrue(TEXT("... hooks left (-X)"), Moves[1].Destination.X < -60.f && Moves[1].Destination.Y < -100.f);
		TestEqual(TEXT("man 3: the centre man"), Moves[2].Participant, 2);
		TestTrue(TEXT("... drives straight in 2.5 m and crouches"), Moves[2].Destination.Equals(FVector(0.f, -CentreDepthCm, 0.f), 0.1f) && Moves[2].bCrouch);
		TestTrue(TEXT("man 1 looks across to the deep left corner"), Moves[0].LookDirection.X < 0.f && Moves[0].LookDirection.Y < 0.f);
	}
	// Solo: the one jamb man goes in on his own side.
	const TArray<FDoorwayBreachEntryMove> Solo = PlanEntry(Center, Outward, 120.f, { EDoorwayBreachSlotRole::LeftJamb }, { FVector(90.f, 45.f, 0.f) });
	TestTrue(TEXT("solo: one move on his side"), Solo.Num() == 1 && Solo[0].Destination.X > 60.f && Solo[0].DelaySeconds == 0.f);
	TestEqual(TEXT("mismatched input: no plan"), PlanEntry(Center, Outward, 120.f, Roles, { FVector::ZeroVector }).Num(), 0);
	return true;
}

BREACH_ENTRY_TEST(FBreachEntryRoomClampTest, "RoomClamp")
bool FBreachEntryRoomClampTest::RunTest(const FString&)
{
	using namespace DoorwayBreachEntryRules;
	// A 150 cm street door. The right-hand return is 140 cm off the axis (S01 d20's east wall). The unclamped hook
	// (half width 75 + HookSideCm 100 = 175) sits in that wall, and the move is unreachable.
	const FVector Center(0.f, 0.f, 0.f);
	const FVector Outward(0.f, 1.f, 0.f);
	const FDoorwayBreachRoom Room = MakeRoom(800.f, 140.f, 400.f, 0.f);
	const TArray<FDoorwayBreachEntryMove> Moves = PlanEntry(Center, Outward, 150.f,
		{ EDoorwayBreachSlotRole::RightJamb, EDoorwayBreachSlotRole::LeftJamb, EDoorwayBreachSlotRole::CenterPoint },
		{ FVector(90.f, 45.f, 0.f), FVector(-90.f, 45.f, 0.f), FVector(0.f, 220.f, 0.f) }, Room);
	TestEqual(TEXT("three men go in"), Moves.Num(), 3);
	if (Moves.Num() == 3)
	{
		const float RightLimit = 140.f - HookWallClearCm;
		TestTrue(TEXT("right hook stops short of the return wall and past the centre man"),
			FMath::IsNearlyEqual(Moves[0].Destination.X, RightLimit, 0.1f)
			&& FMath::IsNearlyEqual(Moves[0].Destination.Y, -(CentreDepthCm + HookPastCentreCm), 0.1f));
		TestTrue(TEXT("left hook keeps the full offset on a wide side"), Moves[1].Destination.X < -(75.f + HookSideCm - 1.f));
		TestTrue(TEXT("centre still drives 2.5 m in a deep room"), Moves[2].Destination.Equals(FVector(0.f, -CentreDepthCm, 0.f), 0.1f));
	}
	return true;
}

BREACH_ENTRY_TEST(FBreachEntryClosetFileTest, "ClosetFile")
bool FBreachEntryClosetFileTest::RunTest(const FString&)
{
	using namespace DoorwayBreachEntryRules;
	// S01 d20's janitor closet: 4 m deep, the returns 100 cm and 150 cm off the axis (a 150 cm door). Both unclamped hooks
	// (175 cm) are in the wall, and both wall-cleared hooks (30 cm and 80 cm) stand in the centre man's path.
	const FVector Center(0.f, 0.f, 0.f);
	const FVector Outward(0.f, 1.f, 0.f);
	const FDoorwayBreachRoom Room = MakeRoom(400.f, 100.f, 150.f, 0.f);
	const TArray<FDoorwayBreachEntryMove> Moves = PlanEntry(Center, Outward, 150.f,
		{ EDoorwayBreachSlotRole::RightJamb, EDoorwayBreachSlotRole::LeftJamb, EDoorwayBreachSlotRole::CenterPoint },
		{ FVector(90.f, 45.f, 0.f), FVector(-90.f, 45.f, 0.f), FVector(0.f, 220.f, 0.f) }, Room);
	TestEqual(TEXT("three men go in"), Moves.Num(), 3);
	if (Moves.Num() == 3)
	{
		const float Far = 400.f - HookWallClearCm;
		const float RightLateral = 100.f - HookWallClearCm;
		const float LeftLateral = 150.f - HookWallClearCm;
		TestTrue(TEXT("the first hook takes the far wall, inside the right return"),
			FMath::IsNearlyEqual(Moves[0].Destination.X, RightLateral, 0.1f) && FMath::IsNearlyEqual(Moves[0].Destination.Y, -Far, 0.1f));
		TestTrue(TEXT("the second hook steps back a body, inside the left return"),
			FMath::IsNearlyEqual(Moves[1].Destination.X, -LeftLateral, 0.1f) && FMath::IsNearlyEqual(Moves[1].Destination.Y, -(Far - HookPastCentreCm), 0.1f));
		const float CentreDepth = Far - HookPastCentreCm - HookPastCentreCm;
		TestTrue(TEXT("the centre man stops a body short of the nearer hook, still inside"),
			Moves[2].Destination.Equals(FVector(0.f, -CentreDepth, 0.f), 0.1f) && CentreDepth > InsideMinDepthCm);
		TestTrue(TEXT("neither hook stands in his path"),
			FVector::Dist2D(Moves[0].Destination, Moves[2].Destination) > HookLaneCm
			&& FVector::Dist2D(Moves[1].Destination, Moves[2].Destination) > HookLaneCm);
	}
	return true;
}

BREACH_ENTRY_TEST(FBreachEntryInsideTest, "InsideRoom")
bool FBreachEntryInsideTest::RunTest(const FString&)
{
	using namespace DoorwayBreachEntryRules;
	const FVector Center(0.f, 0.f, 0.f);
	const FVector Outward(0.f, 1.f, 0.f);
	// A 5 m deep room, its right-hand wall (+X, seen entering) 3 m off the axis, the left one 1.5 m (review 2026-11-17, finding 3: the room's
	// own bounds, not a 15 x 24 m box).
	const FDoorwayBreachRoom Room = MakeRoom(500.f, 300.f, 150.f, 0.f);
	TestTrue(TEXT("3 m inside"), IsInsideRoom(FVector(0.f, -300.f, 0.f), Center, Outward, Room));
	TestTrue(TEXT("inside, by the right-hand wall"), IsInsideRoom(FVector(290.f, -200.f, 0.f), Center, Outward, Room));
	TestTrue(TEXT("the far wall's own face"), IsInsideRoom(FVector(0.f, -505.f, 120.f), Center, Outward, Room));
	TestFalse(TEXT("on the threshold"), IsInsideRoom(FVector(0.f, -10.f, 0.f), Center, Outward, Room));
	TestFalse(TEXT("outside"), IsInsideRoom(FVector(0.f, 300.f, 0.f), Center, Outward, Room));
	TestFalse(TEXT("behind the far wall (the yard behind the building)"), IsInsideRoom(FVector(0.f, -700.f, 0.f), Center, Outward, Room));
	TestFalse(TEXT("past the left-hand wall (the next room)"), IsInsideRoom(FVector(-400.f, -200.f, 0.f), Center, Outward, Room));
	TestFalse(TEXT("the floor above (another storey)"), IsInsideRoom(FVector(0.f, -300.f, 320.f), Center, Outward, Room));
	TestFalse(TEXT("a ground 1 m below the threshold"), IsInsideRoom(FVector(0.f, -300.f, -100.f), Center, Outward, Room));
	TestFalse(TEXT("no measured room: nothing is inside"), IsInsideRoom(FVector(0.f, -300.f, 0.f), Center, Outward, FDoorwayBreachRoom()));
	const FDoorwayBreachRoom Open = MakeRoom(-1.f, -1.f, 20.f, 0.f);
	TestTrue(TEXT("no wall within reach: the maximum depth / side; a reveal is no wall (the minimum)"), Open.DepthCm == RoomMaxDepthCm && Open.RightCm == RoomMaxSideCm
		&& Open.LeftCm == RoomMinCm);
	TestEqual(TEXT("the side of a man at +X seen entering: right"), SideOf(FVector(90.f, 45.f, 0.f), Center, Outward), 1);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
