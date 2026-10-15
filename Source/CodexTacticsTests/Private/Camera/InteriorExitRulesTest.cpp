// CodexTactics.Camera.InteriorExit.* — leaving a building with a fixed interior camera (rev 5g item 5, user request 2026-10-14,
// InteriorExitRules): edge sampling of the zone box, doorway runs (>= 60 cm), who an EXIT click sends, the player's outside view.

#include "Misc/AutomationTest.h"
#include "Camera/InteriorExitRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define INTERIOR_EXIT_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Camera.InteriorExit." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

INTERIOR_EXIT_TEST(FInteriorExitEdgeSamplesTest, "EdgeSamples")
bool FInteriorExitEdgeSamplesTest::RunTest(const FString&)
{
	using namespace InteriorExitRules;
	FExitScanConfig Config;
	TArray<FVector2D> Points;
	FVector2D Normal;
	// The hut zone: 10.5 x 6.8 m interior -> half extent 525 x 340.
	BuildEdgeSamples(FVector2D(525.f, 340.f), 3, Config, Points, Normal);
	TestTrue(TEXT("+Y edge: outward normal +Y"), Normal.Equals(FVector2D(0.f, 1.f)));
	TestTrue(TEXT("+Y edge: every sample on the edge"), Points.Num() > 0 && Points[0].Y == 340.f && Points.Last().Y == 340.f);
	TestTrue(TEXT("+Y edge: starts a corner margin in"), FMath::IsNearlyEqual(Points[0].X, -525.f + Config.CornerMarginCm, 0.01f));
	TestTrue(TEXT("+Y edge: ends a corner margin before the far corner"), Points.Last().X <= 525.f - Config.CornerMarginCm + 0.01f);
	TestEqual(TEXT("+Y edge: 20 cm spacing over 10.5 m minus the margins"), Points.Num(), 50);
	BuildEdgeSamples(FVector2D(525.f, 340.f), 0, Config, Points, Normal);
	TestTrue(TEXT("-X edge: outward normal -X"), Normal.Equals(FVector2D(-1.f, 0.f)));
	TestTrue(TEXT("-X edge: samples run along Y at X = -525"), Points[0].X == -525.f && Points[0].Y < Points.Last().Y);
	return true;
}

INTERIOR_EXIT_TEST(FInteriorExitDoorRunsTest, "DoorRuns")
bool FInteriorExitDoorRunsTest::RunTest(const FString&)
{
	using namespace InteriorExitRules;
	// wall (5) | 2 m door (10 samples) | wall (3) | a 40 cm gap (2) | wall (2) | a 1 m door at the end (5)
	TArray<bool> Crossable;
	auto Add = [&Crossable](bool bOpen, int32 Count) { for (int32 I = 0; I < Count; ++I) { Crossable.Add(bOpen); } };
	Add(false, 5); Add(true, 10); Add(false, 3); Add(true, 2); Add(false, 2); Add(true, 5);
	const TArray<FDoorRun> Runs = FindDoorRuns(Crossable, 20.f, 60.f);
	if (!TestEqual(TEXT("two doors (the 40 cm gap is none)"), Runs.Num(), 2))
	{
		return false;
	}
	TestTrue(TEXT("the 2 m door"), Runs[0].First == 5 && Runs[0].Last == 14 && FMath::IsNearlyEqual(Runs[0].WidthCm, 200.f));
	TestTrue(TEXT("the door running to the end of the edge"), Runs[1].First == 22 && Runs[1].Last == 26 && FMath::IsNearlyEqual(Runs[1].WidthCm, 100.f));
	TestEqual(TEXT("a closed building: no door"), FindDoorRuns(TArray<bool>{ false, false, false }, 20.f, 60.f).Num(), 0);
	TestEqual(TEXT("exactly 60 cm counts"), FindDoorRuns(TArray<bool>{ true, true, true }, 20.f, 60.f).Num(), 1);
	return true;
}

INTERIOR_EXIT_TEST(FInteriorExitScopeAndViewTest, "ScopeAndView")
bool FInteriorExitScopeAndViewTest::RunTest(const FString&)
{
	using namespace InteriorExitRules;
	TestTrue(TEXT("Solo: the selected operative(s) only"), GetExitOrderScope(true) == EExitOrderScope::Selected);
	TestTrue(TEXT("Group mode: the whole squad"), GetExitOrderScope(false) == EExitOrderScope::Squad);
	TestTrue(TEXT("inside, camera, mode allowed: the interior camera"), ShouldShowZoneCamera(true, true, true, false, false));
	TestFalse(TEXT("the player's outside view (M) keeps the normal camera"), ShouldShowZoneCamera(true, true, true, false, true));
	TestFalse(TEXT("a cinematic owns the camera"), ShouldShowZoneCamera(true, true, true, true, false));
	TestFalse(TEXT("mode not allowed (turn-based by default)"), ShouldShowZoneCamera(true, true, false, false, false));
	TestFalse(TEXT("leader outside"), ShouldShowZoneCamera(false, true, true, false, false));
	TestTrue(TEXT("arrows with the interior camera"), ShouldShowExitArrows(true, false));
	TestFalse(TEXT("no arrows in turn-based combat"), ShouldShowExitArrows(true, true));
	TestFalse(TEXT("no arrows without the interior camera (outside view)"), ShouldShowExitArrows(false, false));
	return true;
}

INTERIOR_EXIT_TEST(FInteriorExitRescanTest, "RescanUntilNavReady")
bool FInteriorExitRescanTest::RunTest(const FString&)
{
	using namespace InteriorExitRules;
	// Fix 2026-10-15: InteriorExitSmoke under load cached "0 doors" found on a navmesh still being built at load.
	TestFalse(TEXT("navmesh still building: wait"), ShouldRescanExits(true, true, 0, 0));
	TestTrue(TEXT("built after a search during the build: once more"), ShouldRescanExits(false, true, 2, 3));
	TestTrue(TEXT("nothing found: search again"), ShouldRescanExits(false, false, 0, 0));
	TestTrue(TEXT("nothing found: still within the cap"), ShouldRescanExits(false, false, 0, MaxRescans - 1));
	TestFalse(TEXT("nothing found after the cap: believe it (a building without doors)"), ShouldRescanExits(false, false, 0, MaxRescans));
	TestFalse(TEXT("doors found on a built navmesh: keep them"), ShouldRescanExits(false, false, 1, 0));
	TestTrue(TEXT("the cap covers several seconds"), MaxRescans * RescanIntervalSeconds >= 5.f);
	return true;
}

#undef INTERIOR_EXIT_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
