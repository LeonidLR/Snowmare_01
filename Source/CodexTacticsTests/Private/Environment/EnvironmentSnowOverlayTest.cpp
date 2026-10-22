#include "Misc/AutomationTest.h"
#include "Environment/SnowOverlayRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Environment phase 4c, the gradual snow cover overlay (docs/systems/environment-weather.md §6 / §12): which static meshes get the
// M_SnowOverlay overlay material, the interior box test, the nearest-first selection and the on / off threshold.

namespace
{
	/** A plain outdoor prop: a 2 m crate in the open. */
	SnowOverlayRules::FSnowTargetTraits SnowOverlayOutdoorCrate()
	{
		SnowOverlayRules::FSnowTargetTraits Traits;
		Traits.bStaticMesh = true;
		Traits.BoundsRadiusCm = 170.f;
		Traits.FootprintCm = 200.f;
		Traits.HeightCm = 200.f;
		return Traits;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnvironmentSnowOverlayFilterTest, "CodexTactics.Environment.SnowOverlay.Filter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnvironmentSnowOverlayFilterTest::RunTest(const FString&)
{
	const SnowOverlayRules::FSnowTargetConfig Config;
	TestTrue(TEXT("an outdoor crate gets snow"), SnowOverlayRules::IsTarget(SnowOverlayOutdoorCrate(), Config));
	TestEqual(TEXT("the opt-out tag is NoSnow"), SnowOverlayRules::OptOutTag(), FName(TEXT("NoSnow")));

	struct FCase
	{
		const TCHAR* Name;
		TFunction<void(SnowOverlayRules::FSnowTargetTraits&)> Change;
		SnowOverlayRules::ESnowTargetVerdict Expected;
	};
	const FCase Cases[] = {
		{ TEXT("no static mesh"), [](SnowOverlayRules::FSnowTargetTraits& T) { T.bStaticMesh = false; }, SnowOverlayRules::ESnowTargetVerdict::NotStaticMesh },
		{ TEXT("instanced / foliage"), [](SnowOverlayRules::FSnowTargetTraits& T) { T.bInstanced = true; }, SnowOverlayRules::ESnowTargetVerdict::Instanced },
		{ TEXT("a pawn's mesh"), [](SnowOverlayRules::FSnowTargetTraits& T) { T.bPawn = true; }, SnowOverlayRules::ESnowTargetVerdict::Pawn },
		{ TEXT("landscape"), [](SnowOverlayRules::FSnowTargetTraits& T) { T.bLandscape = true; }, SnowOverlayRules::ESnowTargetVerdict::Landscape },
		{ TEXT("hidden"), [](SnowOverlayRules::FSnowTargetTraits& T) { T.bVisible = false; }, SnowOverlayRules::ESnowTargetVerdict::Hidden },
		{ TEXT("has its own overlay"), [](SnowOverlayRules::FSnowTargetTraits& T) { T.bHasOverlay = true; }, SnowOverlayRules::ESnowTargetVerdict::HasOverlay },
		{ TEXT("tagged NoSnow"), [](SnowOverlayRules::FSnowTargetTraits& T) { T.bOptOut = true; }, SnowOverlayRules::ESnowTargetVerdict::OptOut },
		{ TEXT("translucent only"), [](SnowOverlayRules::FSnowTargetTraits& T) { T.bTranslucentOnly = true; }, SnowOverlayRules::ESnowTargetVerdict::TranslucentOnly },
		{ TEXT("Nanite (draws no overlay)"), [](SnowOverlayRules::FSnowTargetTraits& T) { T.bNanite = true; }, SnowOverlayRules::ESnowTargetVerdict::Nanite },
		{ TEXT("too small"), [](SnowOverlayRules::FSnowTargetTraits& T) { T.BoundsRadiusCm = 20.f; }, SnowOverlayRules::ESnowTargetVerdict::TooSmall },
		{ TEXT("ground slab / sky mesh"), [](SnowOverlayRules::FSnowTargetTraits& T) { T.FootprintCm = 8000.f; }, SnowOverlayRules::ESnowTargetVerdict::TooLarge },
		{ TEXT("a 30 m floor slab 20 cm thick is ground"), [](SnowOverlayRules::FSnowTargetTraits& T) { T.FootprintCm = 3000.f; T.HeightCm = 20.f; },
			SnowOverlayRules::ESnowTargetVerdict::Ground },
		{ TEXT("inside an interior"), [](SnowOverlayRules::FSnowTargetTraits& T) { T.bInterior = true; }, SnowOverlayRules::ESnowTargetVerdict::Interior },
		{ TEXT("under a roof"), [](SnowOverlayRules::FSnowTargetTraits& T) { T.bCovered = true; }, SnowOverlayRules::ESnowTargetVerdict::Covered },
	};
	for (const FCase& Case : Cases)
	{
		SnowOverlayRules::FSnowTargetTraits Traits = SnowOverlayOutdoorCrate();
		Case.Change(Traits);
		const SnowOverlayRules::ESnowTargetVerdict Verdict = SnowOverlayRules::Classify(Traits, Config);
		TestEqual(*FString::Printf(TEXT("%s -> %s"), Case.Name, SnowOverlayRules::VerdictToString(Case.Expected)), static_cast<int32>(Verdict), static_cast<int32>(Case.Expected));
		TestFalse(*FString::Printf(TEXT("%s is no target"), Case.Name), SnowOverlayRules::IsTarget(Traits, Config));
	}

	// The cheap checks pass for interior / covered meshes (their verdict needs the world queries), not for the others.
	SnowOverlayRules::FSnowTargetTraits Covered = SnowOverlayOutdoorCrate();
	Covered.bCovered = true;
	TestTrue(TEXT("covered passes the cheap checks"), SnowOverlayRules::PassesCheapChecks(Covered, Config));
	SnowOverlayRules::FSnowTargetTraits Small = SnowOverlayOutdoorCrate();
	Small.BoundsRadiusCm = 5.f;
	TestFalse(TEXT("a small mesh fails the cheap checks"), SnowOverlayRules::PassesCheapChecks(Small, Config));

	// Nanite meshes take part when the caller switches them to their fallback mesh.
	SnowOverlayRules::FSnowTargetConfig NaniteFallback;
	NaniteFallback.bAllowNaniteFallback = true;
	SnowOverlayRules::FSnowTargetTraits NaniteCrate = SnowOverlayOutdoorCrate();
	NaniteCrate.bNanite = true;
	TestTrue(TEXT("a Nanite crate gets snow with the fallback allowed"), SnowOverlayRules::IsTarget(NaniteCrate, NaniteFallback));

	// A 30 m building (tall) is no ground; a 10 m flat roof slab is no ground either (below the ground width).
	SnowOverlayRules::FSnowTargetTraits Building = SnowOverlayOutdoorCrate();
	Building.FootprintCm = 3000.f;
	Building.HeightCm = 900.f;
	TestTrue(TEXT("a 30 m building gets snow"), SnowOverlayRules::IsTarget(Building, Config));
	SnowOverlayRules::FSnowTargetTraits RoofSlab = SnowOverlayOutdoorCrate();
	RoofSlab.FootprintCm = 1000.f;
	RoofSlab.HeightCm = 20.f;
	TestTrue(TEXT("a 10 m roof slab gets snow"), SnowOverlayRules::IsTarget(RoofSlab, Config));

	// Thresholds are the config's.
	SnowOverlayRules::FSnowTargetConfig Loose;
	Loose.MinBoundsRadiusCm = 0.f;
	Loose.MaxFootprintCm = 0.f;
	Loose.GroundMinFootprintCm = 1.0e9f;
	SnowOverlayRules::FSnowTargetTraits Huge = SnowOverlayOutdoorCrate();
	Huge.FootprintCm = 1.0e6f;
	Huge.BoundsRadiusCm = 1.f;
	TestTrue(TEXT("no size limits -> a tiny / huge mesh is a target"), SnowOverlayRules::IsTarget(Huge, Loose));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnvironmentSnowOverlayInteriorTest, "CodexTactics.Environment.SnowOverlay.Interior",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnvironmentSnowOverlayInteriorTest::RunTest(const FString&)
{
	// A room 10 x 8 m, 3 m high, centred at (1000, 0, 150), turned 30 degrees.
	const FTransform Zone(FRotator(0.f, 30.f, 0.f), FVector(1000.f, 0.f, 150.f));
	const FVector Half(500.f, 400.f, 150.f);

	auto Box = [](const FVector& Centre, const FVector& Extent, FVector& OutMin, FVector& OutMax)
	{
		OutMin = Centre - Extent;
		OutMax = Centre + Extent;
	};
	FVector Min, Max;
	Box(FVector(1000.f, 0.f, 50.f), FVector(50.f, 50.f, 50.f), Min, Max);
	TestTrue(TEXT("a table in the room is inside"), SnowOverlayRules::IsInsideInterior(Min, Max, Zone, Half));

	Box(FVector(1000.f, 0.f, 320.f), FVector(520.f, 420.f, 20.f), Min, Max);
	TestFalse(TEXT("the roof slab above the room (centre over the top) is outdoor"), SnowOverlayRules::IsInsideInterior(Min, Max, Zone, Half));

	Box(FVector(1000.f, 0.f, 260.f), FVector(500.f, 400.f, 60.f), Min, Max);
	TestFalse(TEXT("a roof reaching 20 cm over the zone top is outdoor with a 10 cm tolerance"), SnowOverlayRules::IsInsideInterior(Min, Max, Zone, Half, 10.f));
	TestTrue(TEXT("... but inside with a generous tolerance"), SnowOverlayRules::IsInsideInterior(Min, Max, Zone, Half, 100.f));

	Box(FVector(1000.f, 0.f, 50.f) + FRotator(0.f, 30.f, 0.f).RotateVector(FVector(480.f, 0.f, 0.f)), FVector(30.f), Min, Max);
	TestTrue(TEXT("near the far wall along the turned X axis is inside"), SnowOverlayRules::IsInsideInterior(Min, Max, Zone, Half));

	Box(FVector(1000.f, 0.f, 50.f) + FRotator(0.f, 30.f, 0.f).RotateVector(FVector(0.f, 450.f, 0.f)), FVector(30.f), Min, Max);
	TestFalse(TEXT("past the side wall along the turned Y axis is outside"), SnowOverlayRules::IsInsideInterior(Min, Max, Zone, Half));

	Box(FVector(3000.f, 0.f, 50.f), FVector(50.f), Min, Max);
	TestFalse(TEXT("a crate far away is outside"), SnowOverlayRules::IsInsideInterior(Min, Max, Zone, Half));

	// A zone box that encloses the whole building: its roof (the highest top inside) stays outdoor, the rest is indoor.
	TestFalse(TEXT("the roof (highest top) is no interior piece"), SnowOverlayRules::IsBelowInteriorRoof(310.0, 310.0));
	TestFalse(TEXT("a parapet 20 cm under the roof top counts as roof"), SnowOverlayRules::IsBelowInteriorRoof(290.0, 310.0));
	TestTrue(TEXT("a table 2 m under the roof is indoor"), SnowOverlayRules::IsBelowInteriorRoof(110.0, 310.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnvironmentSnowOverlayOrderTest, "CodexTactics.Environment.SnowOverlay.Order",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnvironmentSnowOverlayOrderTest::RunTest(const FString&)
{
	TArray<SnowOverlayRules::FSnowCandidate> Candidates;
	const double Distances[] = { 3000.0, 500.0, 9000.0, 500.0, 100.0 };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Distances); ++Index)
	{
		SnowOverlayRules::FSnowCandidate Candidate;
		Candidate.Index = Index * 10;
		Candidate.DistanceSq = FMath::Square(Distances[Index]);
		Candidates.Add(Candidate);
	}
	const TArray<int32> All = SnowOverlayRules::OrderCandidates(Candidates, 0.f, 0);
	TestEqual(TEXT("no limit: every candidate"), All.Num(), 5);
	TestEqual(TEXT("nearest first"), All[0], 40);
	TestEqual(TEXT("ties keep the input order (a)"), All[1], 10);
	TestEqual(TEXT("ties keep the input order (b)"), All[2], 30);
	TestEqual(TEXT("farthest last"), All[4], 20);

	const TArray<int32> Near = SnowOverlayRules::OrderCandidates(Candidates, 6000.f, 0);
	TestEqual(TEXT("MaxDistance drops the 90 m one"), Near.Num(), 4);
	TestFalse(TEXT("... it is not in the list"), Near.Contains(20));

	const TArray<int32> Capped = SnowOverlayRules::OrderCandidates(Candidates, 6000.f, 2);
	TestEqual(TEXT("MaxCount caps the list"), Capped.Num(), 2);
	TestEqual(TEXT("... keeping the nearest"), Capped[0], 40);

	const TArray<int32> Kept = SnowOverlayRules::OrderCandidates(Candidates, 6000.f, 2, /*bKeepAll*/ true);
	TestEqual(TEXT("bKeepAll returns the whole sorted list in range"), Kept.Num(), 4);
	TestEqual(TEXT("empty input"), SnowOverlayRules::OrderCandidates({}, 100.f, 5).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnvironmentSnowOverlayShowTest, "CodexTactics.Environment.SnowOverlay.Show",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnvironmentSnowOverlayShowTest::RunTest(const FString&)
{
	TestFalse(TEXT("no snow, no frost: no overlay pass"), SnowOverlayRules::ShouldShowOverlay(0.f, 0.f));
	TestFalse(TEXT("a hair of snow: still off"), SnowOverlayRules::ShouldShowOverlay(0.005f, 0.004f));
	TestTrue(TEXT("light snow cover: on"), SnowOverlayRules::ShouldShowOverlay(0.05f, 0.f));
	TestTrue(TEXT("frost alone (Clear): on"), SnowOverlayRules::ShouldShowOverlay(0.f, 0.3f));
	TestFalse(TEXT("negative inputs count as zero"), SnowOverlayRules::ShouldShowOverlay(-1.f, 0.005f));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
