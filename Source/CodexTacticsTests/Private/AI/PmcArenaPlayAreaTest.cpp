#include "Misc/AutomationTest.h"
#include "AI/PmcArenaPlayArea.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

// fix/arena-no-outer-strip 2026-11-10 (the game designer): the PMC arena's play area - spawns / squad slots / objectives never in the
// Depot's outer strip between the perimeter wall and the map edge. Pure rules (AI/PmcArenaPlayArea.h).

namespace PmcArenaPlayAreaTest
{
	/** The Depot's data (L_PmcArenaDepot.json): the layout bounds as the inclusion box, the sliver behind the west fence excluded. */
	FPmcArenaPlayArea DepotPlayArea()
	{
		FPmcArenaPlayArea Area;
		Area.bHasInclude = true;
		Area.Include = FBox2D(FVector2D(-6200.f, -8850.f), FVector2D(6200.f, 8850.f));
		Area.Exclude.Add(FBox2D(FVector2D(-6200.f, -3900.f), FVector2D(-6040.f, 8330.f)));
		Area.ExcludeIds.Add(TEXT("west_strip"));
		return Area;
	}

	TSharedPtr<FJsonObject> ParseArenaJson(const FString& Text)
	{
		TSharedPtr<FJsonObject> Json;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json);
		return Json;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcArenaPlayAreaRuleTest, "CodexTactics.AI.PmcArena.PlayArea.Rule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcArenaPlayAreaRuleTest::RunTest(const FString&)
{
	using namespace PmcArenaPlayAreaTest;
	const FPmcArenaPlayArea Area = DepotPlayArea();
	// Inside: the depot's yards, any height (a roof counts by its XY).
	TestTrue(TEXT("the centre is inside"), PmcArenaPlayAreaRules::IsInside(Area, FVector(0.f, 0.f, 0.f)));
	TestTrue(TEXT("a roof inside (Z ignored)"), PmcArenaPlayAreaRules::IsInside(Area, FVector(3000.f, 2000.f, 900.f)));
	TestTrue(TEXT("the start corner inside"), PmcArenaPlayAreaRules::IsInside(Area, FVector(2356.f, -8700.f, 0.f)));
	TestTrue(TEXT("the west yard south of the fence's end is inside"), PmcArenaPlayAreaRules::IsInside(Area, FVector(-6100.f, -5000.f, 0.f)));
	// Outside: the outer ground ring (the builder's 10 m margin) on every side, and the strip behind the west fence.
	TestFalse(TEXT("west ring"), PmcArenaPlayAreaRules::IsInside(Area, FVector(-7100.f, 0.f, 0.f)));
	TestFalse(TEXT("east ring"), PmcArenaPlayAreaRules::IsInside(Area, FVector(7100.f, 0.f, 0.f)));
	TestFalse(TEXT("north ring"), PmcArenaPlayAreaRules::IsInside(Area, FVector(0.f, 9800.f, 0.f)));
	TestFalse(TEXT("south ring"), PmcArenaPlayAreaRules::IsInside(Area, FVector(0.f, -9800.f, 0.f)));
	TestFalse(TEXT("a ring corner"), PmcArenaPlayAreaRules::IsInside(Area, FVector(-7000.f, 9700.f, 0.f)));
	TestFalse(TEXT("behind the west fence"), PmcArenaPlayAreaRules::IsInside(Area, FVector(-6120.f, 2000.f, 0.f)));
	// Boundary: the inclusion box is closed (its edge is inside), an exclusion box is closed too (its edge is out, and it wins where both meet).
	TestTrue(TEXT("on the inclusion edge"), PmcArenaPlayAreaRules::IsInside(Area, FVector(6200.f, 0.f, 0.f)));
	TestTrue(TEXT("on the inclusion corner"), PmcArenaPlayAreaRules::IsInside(Area, FVector(6200.f, 8850.f, 0.f)));
	TestFalse(TEXT("just past the inclusion edge"), PmcArenaPlayAreaRules::IsInside(Area, FVector(6200.5f, 0.f, 0.f)));
	TestFalse(TEXT("on the exclusion edge (inner side)"), PmcArenaPlayAreaRules::IsInside(Area, FVector(-6040.f, 0.f, 0.f)));
	TestTrue(TEXT("just past the exclusion edge"), PmcArenaPlayAreaRules::IsInside(Area, FVector(-6039.5f, 0.f, 0.f)));
	TestFalse(TEXT("the exclusion's end on the inclusion edge"), PmcArenaPlayAreaRules::IsInside(Area, FVector(-6200.f, 8330.f, 0.f)));
	TestTrue(TEXT("just past the exclusion's north end"), PmcArenaPlayAreaRules::IsInside(Area, FVector(-6100.f, 8331.f, 0.f)));
	// Unrestricted: everything is inside; exclusions alone (no inclusion box) leave the rest of the map.
	const FPmcArenaPlayArea None;
	TestFalse(TEXT("an empty area restricts nothing"), None.IsRestricted());
	TestTrue(TEXT("an empty area accepts any point"), PmcArenaPlayAreaRules::IsInside(None, FVector(1.0e6f, -1.0e6f, 0.f)));
	FPmcArenaPlayArea ExcludeOnly;
	ExcludeOnly.Exclude.Add(FBox2D(FVector2D(0.f, 0.f), FVector2D(100.f, 100.f)));
	TestTrue(TEXT("exclusions only: restricted"), ExcludeOnly.IsRestricted());
	TestFalse(TEXT("exclusions only: inside the box is out"), PmcArenaPlayAreaRules::IsInside(ExcludeOnly, FVector2D(50.f, 50.f)));
	TestTrue(TEXT("exclusions only: far away is in"), PmcArenaPlayAreaRules::IsInside(ExcludeOnly, FVector2D(50000.f, 50.f)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcArenaPlayAreaSampleBoundsTest, "CodexTactics.AI.PmcArena.PlayArea.SampleBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcArenaPlayAreaSampleBoundsTest::RunTest(const FString&)
{
	using namespace PmcArenaPlayAreaTest;
	const FBox Nav(FVector(-7200.f, -9850.f, -400.f), FVector(7200.f, 9850.f, 1200.f));
	const FBox Clipped = PmcArenaPlayAreaRules::SampleBounds(DepotPlayArea(), Nav);
	TestTrue(TEXT("clipped to the inclusion box (min)"), (FVector2D(Clipped.Min.X, Clipped.Min.Y)).Equals(FVector2D(-6200.f, -8850.f), 0.01));
	TestTrue(TEXT("clipped to the inclusion box (max)"), (FVector2D(Clipped.Max.X, Clipped.Max.Y)).Equals(FVector2D(6200.f, 8850.f), 0.01));
	TestTrue(TEXT("the navmesh heights kept"), (FVector2D(Clipped.Min.Z, Clipped.Max.Z)).Equals(FVector2D(-400.f, 1200.f), 0.01));
	const FBox Same = PmcArenaPlayAreaRules::SampleBounds(FPmcArenaPlayArea(), Nav);
	TestTrue(TEXT("no inclusion box: the navmesh bounds"), Same.Min.Equals(Nav.Min) && Same.Max.Equals(Nav.Max));
	FPmcArenaPlayArea Away;
	Away.bHasInclude = true;
	Away.Include = FBox2D(FVector2D(50000.f, 50000.f), FVector2D(51000.f, 51000.f));
	const FBox Disjoint = PmcArenaPlayAreaRules::SampleBounds(Away, Nav);
	TestTrue(TEXT("no overlap: the navmesh bounds (the samplers reject and fall back)"), Disjoint.Min.Equals(Nav.Min) && Disjoint.Max.Equals(Nav.Max));
	TestFalse(TEXT("invalid navmesh bounds stay invalid"), static_cast<bool>(PmcArenaPlayAreaRules::SampleBounds(DepotPlayArea(), FBox(ForceInit)).IsValid));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcArenaPlayAreaJsonTest, "CodexTactics.AI.PmcArena.PlayArea.Json",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcArenaPlayAreaJsonTest::RunTest(const FString&)
{
	using namespace PmcArenaPlayAreaTest;
	{
		const TSharedPtr<FJsonObject> Arena = ParseArenaJson(TEXT(R"({ "objective_radius_m": 6 })"));
		FPmcArenaPlayArea Area;
		TArray<FString> Errors;
		TestTrue(TEXT("no play_area: clean"), Arena.IsValid() && PmcArenaPlayAreaRules::ReadJson(*Arena, Area, Errors));
		TestFalse(TEXT("no play_area: unrestricted"), Area.IsRestricted());
	}
	{
		// The corners in any order; ids read, a missing id named by its index.
		const TSharedPtr<FJsonObject> Arena = ParseArenaJson(TEXT(R"({ "play_area": {
			"include_box_cm": { "min": [6200, 8850], "max": [-6200, -8850] },
			"exclude_boxes_cm": [ { "id": "west_strip", "min": [-6200, -3900], "max": [-6040, 8330] }, { "min": [0, 0], "max": [10, 10] } ] } })"));
		FPmcArenaPlayArea Area;
		TArray<FString> Errors;
		TestTrue(TEXT("a full play_area: clean"), Arena.IsValid() && PmcArenaPlayAreaRules::ReadJson(*Arena, Area, Errors));
		TestTrue(TEXT("the inclusion box read"), Area.bHasInclude);
		TestTrue(TEXT("the inclusion box normalised (min)"), (Area.Include.Min).Equals(FVector2D(-6200.f, -8850.f), 0.01));
		TestTrue(TEXT("the inclusion box normalised (max)"), (Area.Include.Max).Equals(FVector2D(6200.f, 8850.f), 0.01));
		TestEqual(TEXT("two exclusion boxes"), Area.Exclude.Num(), 2);
		TestEqual(TEXT("the first id"), Area.ExcludeIds.IsValidIndex(0) ? Area.ExcludeIds[0] : FString(), FString(TEXT("west_strip")));
		TestEqual(TEXT("a missing id = exclude_<index>"), Area.ExcludeIds.IsValidIndex(1) ? Area.ExcludeIds[1] : FString(), FString(TEXT("exclude_1")));
		TestFalse(TEXT("the read area works: the outer strip is out"), PmcArenaPlayAreaRules::IsInside(Area, FVector(-7100.f, 0.f, 0.f)));
		TestTrue(TEXT("Describe names the boxes"), PmcArenaPlayAreaRules::Describe(Area).Contains(TEXT("west_strip")));
	}
	{
		// Malformed entries are skipped (a warning each), the good ones still load.
		const TSharedPtr<FJsonObject> Arena = ParseArenaJson(TEXT(R"({ "play_area": {
			"include_box_cm": { "min": [0, 0], "max": [0, 500] },
			"exclude_boxes_cm": [ { "min": [0, 0] }, 7, { "min": [1, 2, 3], "max": [4, 5] }, { "min": [0, 0], "max": [100, 100] } ] } })"));
		FPmcArenaPlayArea Area;
		TArray<FString> Errors;
		TestFalse(TEXT("malformed entries: not clean"), Arena.IsValid() && PmcArenaPlayAreaRules::ReadJson(*Arena, Area, Errors));
		TestFalse(TEXT("a zero-width inclusion box is ignored"), Area.bHasInclude);
		TestEqual(TEXT("only the good exclusion box loads"), Area.Exclude.Num(), 1);
		TestEqual(TEXT("one error per bad entry"), Errors.Num(), 4);
	}
	return true;
}

#endif
