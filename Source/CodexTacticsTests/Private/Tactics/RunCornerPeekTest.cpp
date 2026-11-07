// CodexTactics.Tactics.Cover.RunCornerPeek.* - the run-and-peek click (#55, user request 2026-11-07; Tactics/RunCornerPeekRules): when a
// click on a wall's end is a run-and-peek, which face of the wall he takes (the one facing him), the probe directions, the face width, the
// arrival test and who carries the order out. UE-only (no Godot reference).

#include "Misc/AutomationTest.h"
#include "Tactics/RunCornerPeekRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define RUN_CORNER_PEEK_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Tactics.Cover.RunCornerPeek." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

RUN_CORNER_PEEK_TEST(FRunCornerPeekClickGateTest, "ClickGate")
bool FRunCornerPeekClickGateTest::RunTest(const FString& Parameters)
{
	using namespace RunCornerPeekRules;
	const FClickFacts Open;
	TestTrue(TEXT("real time, from the open, a wall's end: a run-and-peek"), MayRunToPeek(Open));
	const auto With = [](TFunctionRef<void(FClickFacts&)> Change)
	{
		FClickFacts Facts;
		Change(Facts);
		return MayRunToPeek(Facts);
	};
	TestFalse(TEXT("Codex.Cover.RunCornerPeek 0: the old rule"), With([](FClickFacts& F) { F.bEnabled = false; }));
	TestFalse(TEXT("turn-based: its own cover rules (out of scope)"), With([](FClickFacts& F) { F.bTurnBased = true; }));
	TestFalse(TEXT("the tactical pause plans orders: the old cover click"), With([](FClickFacts& F) { F.bPausePlanning = true; }));
	TestFalse(TEXT("Alt (squad-wide) keeps 'take cover at that object'"), With([](FClickFacts& F) { F.bSquadWide = true; }));
	TestFalse(TEXT("the tutorial has not taught the corner peek"), With([](FClickFacts& F) { F.bTutorialLocked = true; }));
	TestFalse(TEXT("a click on a pawn never peeks"), With([](FClickFacts& F) { F.bHitPawn = true; }));
	TestFalse(TEXT("an object keeps its own click meaning"), With([](FClickFacts& F) { F.bHitObject = true; }));
	TestFalse(TEXT("in cover at that object: the in-cover peek / shimmy / wrap decide"), With([](FClickFacts& F) { F.bInCoverAtClickedObject = true; }));
	TestFalse(TEXT("from a wall edge at a cover-switch target: the switch's preview + dash"), With([](FClickFacts& F) { F.bCoverSwitchTarget = true; }));
	return true;
}

RUN_CORNER_PEEK_TEST(FRunCornerPeekNearFaceTest, "NearFace")
bool FRunCornerPeekNearFaceTest::RunTest(const FString& Parameters)
{
	using namespace RunCornerPeekRules;
	// A 30 cm wall along X (y 0..30): its south face looks -Y, its north face +Y.
	TArray<FFace> Faces;
	FFace South;
	South.Normal = FVector2D(0.0, -1.0);
	South.Point = FVector2D(-20.0, 0.0);
	FFace North;
	North.Normal = FVector2D(0.0, 1.0);
	North.Point = FVector2D(-20.0, 30.0);
	Faces.Add(South);
	Faces.Add(North);
	TestEqual(TEXT("running in from the south, 10 m out: the south face"), ChooseNearFace(Faces, FVector2D(300.0, -1000.0)), 0);
	TestEqual(TEXT("from the north: the north face"), ChooseNearFace(Faces, FVector2D(-500.0, 1200.0)), 1);
	TestEqual(TEXT("diagonally from the south-west, past the wall's end: still the south face"), ChooseNearFace(Faces, FVector2D(-900.0, -300.0)), 0);
	// Level with the wall's line beyond its end: both faces score ~0 - the nearer face point wins.
	FFace Far = North;
	Far.Point = FVector2D(-20.0, 30.0);
	TArray<FFace> Level{ South, Far };
	const int32 Pick = ChooseNearFace(Level, FVector2D(-1000.0, 15.0));
	TestTrue(TEXT("level with the wall line: one of the two faces"), Pick == 0 || Pick == 1);
	TestEqual(TEXT("no faces: none"), ChooseNearFace(TArray<FFace>(), FVector2D::ZeroVector), static_cast<int32>(INDEX_NONE));
	TArray<FFace> One{ North };
	TestEqual(TEXT("one face (the far one of a thick building is not probed): it"), ChooseNearFace(One, FVector2D(0.0, -800.0)), 0);
	return true;
}

RUN_CORNER_PEEK_TEST(FRunCornerPeekFacesTest, "FaceProbesAndWidth")
bool FRunCornerPeekFacesTest::RunTest(const FString& Parameters)
{
	using namespace RunCornerPeekRules;
	const TArray<FVector2D> Directions = ProbeDirections(FVector2D(0.0, -2.0), 8);
	TestEqual(TEXT("eight probe directions"), Directions.Num(), 8);
	TestTrue(TEXT("the first is the clicked face's normal (unit)"), Directions[0].Equals(FVector2D(0.0, -1.0), 1e-6));
	TestTrue(TEXT("the fifth is the opposite side (the far face)"), Directions[4].Equals(FVector2D(0.0, 1.0), 1e-6));
	TestTrue(TEXT("the third / seventh are the wall's ends"), FMath::IsNearlyEqual(FMath::Abs(Directions[2].X), 1.0, 1e-6) && FMath::IsNearlyEqual(Directions[2].X, -Directions[6].X, 1e-6));
	bool bUnit = true;
	for (const FVector2D& Each : Directions)
	{
		bUnit &= FMath::IsNearlyEqual(Each.Size(), 1.0, 1e-6);
	}
	TestTrue(TEXT("all unit vectors"), bUnit);
	TestEqual(TEXT("a zero primary still probes"), ProbeDirections(FVector2D::ZeroVector, 4).Num(), 4);
	TestEqual(TEXT("count < 1: the primary alone"), ProbeDirections(FVector2D(1.0, 0.0), 0).Num(), 1);

	TestTrue(TEXT("a long wall (one end open): wide"), IsFaceWideEnough(true, 20.f, false, 0.f));
	TestTrue(TEXT("a 2.5 m container side: wide"), IsFaceWideEnough(true, 60.f, true, 190.f));
	TestFalse(TEXT("a 30 cm end face: no run-and-peek face"), IsFaceWideEnough(true, 15.f, true, 15.f));
	TestFalse(TEXT("a 60 cm post"), IsFaceWideEnough(true, 30.f, true, 30.f));
	TestTrue(TEXT("exactly the minimum"), IsFaceWideEnough(true, 50.f, true, 50.f));

	TArray<FVector2D> Known{ FVector2D(0.0, -1.0) };
	TestFalse(TEXT("the same face again (a probe from the diagonal)"), IsNewFace(Known, FVector2D(0.05, -1.0)));
	TestTrue(TEXT("the far face is new"), IsNewFace(Known, FVector2D(0.0, 1.0)));
	TestTrue(TEXT("an end face is new"), IsNewFace(Known, FVector2D(1.0, 0.0)));
	TestFalse(TEXT("a zero normal is no face"), IsNewFace(Known, FVector2D::ZeroVector));
	return true;
}

RUN_CORNER_PEEK_TEST(FRunCornerPeekArrivalTest, "Arrival")
bool FRunCornerPeekArrivalTest::RunTest(const FString& Parameters)
{
	using namespace RunCornerPeekRules;
	TestTrue(TEXT("armed, the ordered corner slot: the peek starts at once"), IsPeekArrival(true, true, 0.f));
	TestTrue(TEXT("within the cover walk's arrival tolerance"), IsPeekArrival(true, true, 140.f));
	TestFalse(TEXT("beyond it"), IsPeekArrival(true, true, 160.f));
	TestFalse(TEXT("another wall (a later order took him elsewhere)"), IsPeekArrival(true, false, 0.f));
	TestFalse(TEXT("not armed (any later order disarms it)"), IsPeekArrival(false, true, 0.f));
	return true;
}

RUN_CORNER_PEEK_TEST(FRunCornerPeekGroupTest, "Group")
bool FRunCornerPeekGroupTest::RunTest(const FString& Parameters)
{
	using namespace RunCornerPeekRules;
	TestTrue(TEXT("solo mode, one selected: single"), DecideGroup(false, true, true, true, 0) == EGroup::Single);
	TestTrue(TEXT("solo mode, a box selection with him: the box selection"), DecideGroup(true, true, true, true, 0) == EGroup::BoxSelection);
	TestTrue(TEXT("a box selection, Codex.Squad.GroupCornerPeek 0: single"), DecideGroup(true, true, false, true, 0) == EGroup::Single);
	TestTrue(TEXT("a box selection without him: not his group"), DecideGroup(true, false, true, true, 0) == EGroup::Single);
	TestTrue(TEXT("formation with followers that join a cover order: formation"), DecideGroup(false, false, true, false, 2) == EGroup::Formation);
	TestTrue(TEXT("formation, nobody joins (all on their own orders): single"), DecideGroup(false, false, true, false, 0) == EGroup::Single);
	TestTrue(TEXT("solo mode never orders the formation"), DecideGroup(false, true, true, true, 3) == EGroup::Single);
	return true;
}

#undef RUN_CORNER_PEEK_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
