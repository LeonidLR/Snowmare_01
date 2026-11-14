// CodexTactics.LevelGen.Props.LayoutView.* - level authoring stage 1, task C2 (docs/design/levelgen/PROPS_LAYERS.md sections 2.1 and 5):
// FPropsLayoutView - walk layers + Z, the integer blocked test (walls with thickness, footprints, covers, cut walls), doorways (building
// doors + compound gates; the cut's gaps are not doors), the mandatory route and the exclusion shapes. Pure, no world. Inputs: the
// accepted Depot layout (Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1.json), the shared fixture
// (Content/Data/LevelGen/props/fixtures/fixture_small.layout.json) and small inline layouts. UE-only (no Godot reference).

#include "LevelGen/PropsLayoutView.h"
#include "Misc/AutomationTest.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Templates/Function.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

#define PROPS_LAYOUT_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.LevelGen.Props.LayoutView." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace PropsLayoutViewTestLocal
{
	const TCHAR* const GDepot = TEXT("Data/LevelGen/accepted/gen_ref_01_02_depot_1.json");
	const TCHAR* const GFixture = TEXT("Data/LevelGen/props/fixtures/fixture_small.layout.json");

	bool LoadView(FAutomationTestBase& Test, const TCHAR* RelativePath, FPropsLayoutView& Out)
	{
		FString Json;
		if (!Test.TestTrue(FString::Printf(TEXT("%s readable"), RelativePath), FFileHelper::LoadFileToString(Json, *(FPaths::ProjectContentDir() / RelativePath))))
		{
			return false;
		}
		TArray<FString> Errors;
		const bool bOk = FPropsLayoutView::Build(Json, Out, Errors);
		for (const FString& E : Errors)
		{
			Test.AddError(FString::Printf(TEXT("%s: %s"), RelativePath, *E));
		}
		return Test.TestTrue(FString::Printf(TEXT("%s builds"), RelativePath), bOk);
	}

	/** A minimal valid layout around the given walls / covers JSON arrays. */
	FString Inline(const FString& Walls, const FString& Covers = TEXT("[]"), const FString& Extra = FString())
	{
		return FString::Printf(TEXT("{\"schema\":\"codex.levelgen_layout/1\",\"name\":\"inline\",\"bounds\":{\"min\":[-2000,-2000],\"max\":[2000,2000]},")
			TEXT("\"ground_z\":0,\"spawn\":{\"pos\":[0,-1900]},\"end\":[0,1900],\"walls\":%s,\"buildings\":[],\"covers\":%s,\"ladders\":[],")
			TEXT("\"zones\":[],\"routes\":[{\"id\":\"R\",\"mandatory\":true,\"points\":[[0,-1900],[0,1900]]}]%s}"), *Walls, *Covers, *Extra);
	}

	TArray<FString> DoorwayIds(const FPropsLayoutView& View, EPropsDoorwayKind Kind)
	{
		TArray<FString> Ids;
		for (const FPropsDoorway& D : View.GetDoorways())
		{
			if (D.Kind == Kind)
			{
				Ids.Add(D.Id);
			}
		}
		return Ids;
	}
}

// ---- Depot: the C2 acceptance list ----------------------------------------------------------------------------------------------

PROPS_LAYOUT_TEST(FPropsLayoutDepotGateTest, "Depot.CompoundGateOnMandatoryRoute")
bool FPropsLayoutDepotGateTest::RunTest(const FString&)
{
	using namespace PropsLayoutViewTestLocal;
	FPropsLayoutView View;
	if (!LoadView(*this, GDepot, View))
	{
		return false;
	}
	TestEqual(TEXT("one mandatory route"), View.GetMandatoryRouteIds(), TArray<FString>{ TEXT("R_main") });

	// F_compound_N_0 ends at y -6234, F_compound_N_1 starts at -5934: a 300 cm gate centred on (1400, -6084)
	const FPropsDoorway* Gate = View.FindDoorway(TEXT("F_compound_N#g0"));
	if (!TestNotNull(TEXT("compound gate found"), Gate))
	{
		return false;
	}
	TestTrue(TEXT("gate kind"), Gate->Kind == EPropsDoorwayKind::Gate);
	TestEqual(TEXT("gate pos"), Gate->Pos, FIntPoint(1400, -6084));
	TestEqual(TEXT("gate width"), Gate->WidthCm, 300);
	TestEqual(TEXT("gate wall group"), Gate->WallGroup, FString(TEXT("F_compound_N")));
	TestEqual(TEXT("gate along the fence (y)"), Gate->Along, FIntPoint(0, 1));
	TestEqual(TEXT("gate walk layer"), Gate->WalkLayer, FString(TEXT("ground")));
	TestTrue(TEXT("gate is on the mandatory route"), Gate->bOnMandatoryRoute);
	TestTrue(TEXT("R_main passes THROUGH the gate"), Gate->bRouteThrough);
	TestEqual(TEXT("R_main passes through the gate"), Gate->MandatoryRouteDistCm, 0);

	// every compound gate of the Depot: the 300 cm fence gaps and (designer 2026-11-10, gates up to 600 cm) the tractor yard's 500 cm gaps
	TestEqual(TEXT("gates"), DoorwayIds(View, EPropsDoorwayKind::Gate), TArray<FString>{ TEXT("F_boilers_S#g0"), TEXT("F_compound_N#g0"),
		TEXT("F_junkyard_S#g0"), TEXT("F_junkyard_S#g1"), TEXT("F_lot_N#g0"), TEXT("F_lot_S#g0"), TEXT("F_tanks_E#g0"), TEXT("F_tanks_N#g0"),
		TEXT("F_tanks_S#g0") });
	for (const TCHAR* Id : { TEXT("F_tanks_N#g0"), TEXT("F_tanks_S#g0") })
	{
		const FPropsDoorway* Tanks = View.FindDoorway(Id);
		if (TestNotNull(FString::Printf(TEXT("%s found"), Id), Tanks))
		{
			TestEqual(FString::Printf(TEXT("%s width"), Id), Tanks->WidthCm, 500);
			TestEqual(FString::Printf(TEXT("%s centre y"), Id), Tanks->Pos.Y, -2992);
			TestFalse(FString::Printf(TEXT("%s is not on the mandatory route"), Id), Tanks->bOnMandatoryRoute);
		}
	}
	return true;
}

PROPS_LAYOUT_TEST(FPropsLayoutDepotPumpDoorTest, "Depot.PumpDoorNotOnMandatoryRoute")
bool FPropsLayoutDepotPumpDoorTest::RunTest(const FString&)
{
	using namespace PropsLayoutViewTestLocal;
	FPropsLayoutView View;
	if (!LoadView(*this, GDepot, View))
	{
		return false;
	}
	const FPropsDoorway* Pump = View.FindDoorway(TEXT("B_pump#0"));
	if (!TestNotNull(TEXT("B_pump#0 found"), Pump))
	{
		return false;
	}
	TestTrue(TEXT("building door"), Pump->Kind == EPropsDoorwayKind::BuildingDoor);
	TestEqual(TEXT("pos"), Pump->Pos, FIntPoint(-4339, -1900));
	TestEqual(TEXT("outward = -y (the footprint's min-y side)"), Pump->Outward, FIntPoint(0, -1));
	TestEqual(TEXT("along the wall (x)"), Pump->Along, FIntPoint(1, 0));
	TestEqual(TEXT("width from the wall gap"), Pump->WidthCm, 180);
	// designer 2026-11-10: R_main runs along y -2080 past it (180 cm) with a straight vertex at (-4452, -2080) in front of it - passing
	// by is NOT on the route: no crossing of the door's wall line, no reversal / end in front of it
	TestFalse(TEXT("B_pump#0 is not on the mandatory route"), Pump->bOnMandatoryRoute);
	TestFalse(TEXT("B_pump#0: the route does not pass through"), Pump->bRouteThrough);
	TestFalse(TEXT("B_pump#0: the route does not turn at it"), Pump->bRouteTurnsAt);
	TestEqual(TEXT("distance to R_main"), Pump->MandatoryRouteDistCm, 180);

	// the shed door (design 2.1's example): R_main turns back at (797, -7070), 180 cm in front of it - "turns at it" = on the route
	const FPropsDoorway* Shed = View.FindDoorway(TEXT("B_shed#0"));
	if (TestNotNull(TEXT("B_shed#0 found"), Shed))
	{
		TestEqual(TEXT("shed door outward +y"), Shed->Outward, FIntPoint(0, 1));
		TestEqual(TEXT("shed door distance to R_main"), Shed->MandatoryRouteDistCm, 180);
		TestTrue(TEXT("shed door: R_main turns at it"), Shed->bRouteTurnsAt);
		TestFalse(TEXT("shed door: R_main does not pass through"), Shed->bRouteThrough);
		TestTrue(TEXT("shed door is on the mandatory route"), Shed->bOnMandatoryRoute);
	}
	// R_main leaves the ruin through its north door (the leg (-3360, 1815) -> (-2900, 1025) crosses y 1150 at x -2973, 129 cm off centre)
	const FPropsDoorway* Ruin = View.FindDoorway(TEXT("B_ruin#0"));
	if (TestNotNull(TEXT("B_ruin#0 found"), Ruin))
	{
		TestTrue(TEXT("ruin door: R_main passes through"), Ruin->bRouteThrough);
		TestTrue(TEXT("ruin door on the mandatory route"), Ruin->bOnMandatoryRoute);
		TestEqual(TEXT("ruin door distance"), Ruin->MandatoryRouteDistCm, 112);
	}
	// exactly three doorways of the Depot are on R_main
	TArray<FString> OnRoute;
	for (const FPropsDoorway& D : View.GetDoorways())
	{
		if (D.bOnMandatoryRoute)
		{
			OnRoute.Add(D.Id);
		}
	}
	TestEqual(TEXT("doorways on the mandatory route"), OnRoute, TArray<FString>{ TEXT("B_ruin#0"), TEXT("B_shed#0"), TEXT("F_compound_N#g0") });
	return true;
}

PROPS_LAYOUT_TEST(FPropsLayoutDepotCutGapsTest, "Depot.CutGapsAreNotDoors")
bool FPropsLayoutDepotCutGapsTest::RunTest(const FString&)
{
	using namespace PropsLayoutViewTestLocal;
	FPropsLayoutView View;
	if (!LoadView(*this, GDepot, View))
	{
		return false;
	}
	TArray<FIntPoint> CutGapCentres;
	for (const FPropsWallGap& G : View.GetWallGaps())
	{
		if (G.WallKind == TEXT("cut_wall"))
		{
			CutGapCentres.Add(G.Center);
			TestTrue(TEXT("cut gap is a crossing opening (400 / 500 cm)"), G.WidthCm == 400 || G.WidthCm == 500);
		}
	}
	// CUTWALL_N / CUTWALL_S each open at the ramp X_centre and the bridge X_ne; all of them would pass the 80-600 gate width rule
	TestEqual(TEXT("four cut wall gaps"), CutGapCentres.Num(), 4);
	for (const FPropsDoorway& D : View.GetDoorways())
	{
		TestFalse(FString::Printf(TEXT("%s is not in a cut wall"), *D.Id), D.WallGroup.StartsWith(TEXT("CUTWALL")));
		TestFalse(FString::Printf(TEXT("%s is not at a cut gap"), *D.Id), CutGapCentres.Contains(D.Pos));
	}
	// 14 building doors + 9 gates, nothing else
	TestEqual(TEXT("building doors"), DoorwayIds(View, EPropsDoorwayKind::BuildingDoor).Num(), 14);
	TestEqual(TEXT("doorways"), View.GetDoorways().Num(), 23);
	return true;
}

PROPS_LAYOUT_TEST(FPropsLayoutDepotHallDoorsTest, "Depot.HallNorthHasTwoDoors")
bool FPropsLayoutDepotHallDoorsTest::RunTest(const FString&)
{
	using namespace PropsLayoutViewTestLocal;
	FPropsLayoutView View;
	if (!LoadView(*this, GDepot, View))
	{
		return false;
	}
	TArray<const FPropsDoorway*> Hall;
	for (const FPropsDoorway& D : View.GetDoorways())
	{
		if (D.Building == TEXT("B_hall_n"))
		{
			Hall.Add(&D);
		}
	}
	if (!TestEqual(TEXT("B_hall_n has 2 doors"), Hall.Num(), 2))
	{
		return false;
	}
	TestEqual(TEXT("door 0 id"), Hall[0]->Id, FString(TEXT("B_hall_n#0")));
	TestEqual(TEXT("door 0 pos"), Hall[0]->Pos, FIntPoint(3200, -1828));
	TestEqual(TEXT("door 1 id"), Hall[1]->Id, FString(TEXT("B_hall_n#1")));
	TestEqual(TEXT("door 1 pos"), Hall[1]->Pos, FIntPoint(3200, -2060));
	for (const FPropsDoorway* D : Hall)
	{
		TestEqual(TEXT("outward = -x"), D->Outward, FIntPoint(-1, 0));
		TestEqual(TEXT("180 cm opening"), D->WidthCm, 180);
		TestEqual(TEXT("wall group"), D->WallGroup, FString(TEXT("B_hall_n_S")));
		TestFalse(TEXT("not on the mandatory route"), D->bOnMandatoryRoute);
	}
	return true;
}

PROPS_LAYOUT_TEST(FPropsLayoutDepotLayersTest, "Depot.WalkLayersAndZ")
bool FPropsLayoutDepotLayersTest::RunTest(const FString&)
{
	using namespace PropsLayoutViewTestLocal;
	FPropsLayoutView View;
	if (!LoadView(*this, GDepot, View))
	{
		return false;
	}
	TArray<FString> Names;
	for (const FPropsWalkLayer& L : View.GetLayers())
	{
		Names.Add(L.Name);
	}
	// schema.ts layersOf: no floors inside hollow halls (B_hall_n, B_warehouse, B_steelworks); the ruin keeps its first floor
	TestEqual(TEXT("layers"), Names, TArray<FString>{ TEXT("ground"), TEXT("cut"), TEXT("roof:B_gallery"), TEXT("roof:B_hall_n"),
		TEXT("roof:B_warehouse"), TEXT("floor:B_ruin:1"), TEXT("roof:B_ruin"), TEXT("roof:B_steelworks") });
	const auto Z = [&View](const TCHAR* Name) { const FPropsWalkLayer* L = View.FindLayer(Name); return L ? L->ZCm : MIN_int32; };
	TestEqual(TEXT("ground z"), Z(TEXT("ground")), 0);
	TestEqual(TEXT("cut z"), Z(TEXT("cut")), -300);
	TestEqual(TEXT("ruin floor 1 z"), Z(TEXT("floor:B_ruin:1")), 300);
	TestEqual(TEXT("ruin roof z"), Z(TEXT("roof:B_ruin")), 600);
	TestEqual(TEXT("hall roof z"), Z(TEXT("roof:B_hall_n")), 900);
	TestEqual(TEXT("gallery roof z"), Z(TEXT("roof:B_gallery")), 300);
	TestNull(TEXT("no floor inside the hollow hall"), View.FindLayer(TEXT("floor:B_hall_n:1")));

	// walkability: the cut floor vs the ground over it, a crossing, roof holes and the roof inset
	TestFalse(TEXT("ground over the cut"), View.IsWalkable(FIntPoint(0, 0), TEXT("ground")));
	TestTrue(TEXT("cut floor"), View.IsWalkable(FIntPoint(0, 0), TEXT("cut")));
	TestTrue(TEXT("the ramp X_centre is ground"), View.IsWalkable(FIntPoint(0, -3300), TEXT("ground")));
	TestFalse(TEXT("out of bounds"), View.IsWalkable(FIntPoint(6201, 0), TEXT("ground")));
	TestFalse(TEXT("roof hole"), View.IsWalkable(FIntPoint(-2720, 1720), TEXT("roof:B_ruin")));
	TestFalse(TEXT("roof hole edge (half side 120)"), View.IsWalkable(FIntPoint(-2600, 1720), TEXT("roof:B_ruin")));
	TestTrue(TEXT("roof next to the hole"), View.IsWalkable(FIntPoint(-2599, 1720), TEXT("roof:B_ruin")));
	TestFalse(TEXT("roof inset (50 cm)"), View.IsWalkable(FIntPoint(-2001, 2000), TEXT("roof:B_ruin")));
	TestTrue(TEXT("roof inside the inset"), View.IsWalkable(FIntPoint(-2050, 2000), TEXT("roof:B_ruin")));
	TestFalse(TEXT("floor hole"), View.IsWalkable(FIntPoint(-3260, 1815), TEXT("floor:B_ruin:1")));
	TestFalse(TEXT("unknown layer"), View.IsWalkable(FIntPoint(0, 0), TEXT("floor:B_hall_n:1")));
	return true;
}

PROPS_LAYOUT_TEST(FPropsLayoutDepotBlockedTest, "Depot.Blocked")
bool FPropsLayoutDepotBlockedTest::RunTest(const FString&)
{
	using namespace PropsLayoutViewTestLocal;
	FPropsLayoutView View;
	if (!LoadView(*this, GDepot, View))
	{
		return false;
	}
	const FString Ground(TEXT("ground"));
	TestTrue(TEXT("silo footprint"), View.IsBlocked(FIntPoint(-2562, -3529), Ground, 0));
	TestTrue(TEXT("chimney footprint"), View.IsBlocked(FIntPoint(-1116, 7484), Ground, 0));
	TestTrue(TEXT("non-enterable facade footprint"), View.IsBlocked(FIntPoint(5650, -5800), Ground, 0));
	TestFalse(TEXT("inside the enterable hall"), View.IsBlocked(FIntPoint(4500, -1000), Ground, 60));
	TestTrue(TEXT("rotated wreck"), View.IsBlocked(FIntPoint(-672, 2114), Ground, 0));
	// the cut wall CUTWALL_N at x 358, 150 thick: 75 + clearance
	TestTrue(TEXT("cut wall"), View.IsBlocked(FIntPoint(358, 0), Ground, 0));
	TestTrue(TEXT("cut wall edge"), View.IsBlocked(FIntPoint(433, 0), Ground, 0));
	TestFalse(TEXT("past the cut wall"), View.IsBlocked(FIntPoint(434, 0), Ground, 0));
	TestTrue(TEXT("past the cut wall with clearance"), View.IsBlocked(FIntPoint(434, 0), Ground, 1));
	// the compound gate is open: free for a small object in its middle, blocked once the clearance reaches the posts (150 cm)
	TestFalse(TEXT("gate middle, clearance 100"), View.IsBlocked(FIntPoint(1400, -6084), Ground, 100));
	TestTrue(TEXT("gate middle, clearance 150"), View.IsBlocked(FIntPoint(1400, -6084), Ground, 150));
	// per-floor walls: the ruin's first-floor partition (y 2253) blocks on floor 1 only; the ground partition sits at y 2207
	TestTrue(TEXT("floor 1 partition"), View.IsBlocked(FIntPoint(-2300, 2253), TEXT("floor:B_ruin:1"), 0));
	TestFalse(TEXT("ground under the floor 1 partition"), View.IsBlocked(FIntPoint(-2300, 2253), Ground, 30));
	TestTrue(TEXT("ground partition"), View.IsBlocked(FIntPoint(-2300, 2253), Ground, 31));
	TestTrue(TEXT("floor 1 perimeter"), View.IsBlocked(FIntPoint(-2010, 2000), TEXT("floor:B_ruin:1"), 0));
	TestFalse(TEXT("floor 1 middle"), View.IsBlocked(FIntPoint(-2300, 2700), TEXT("floor:B_ruin:1"), 50));
	// roof parapet (20 cm): blocks within 10 + clearance of the footprint edge
	TestTrue(TEXT("roof parapet"), View.IsBlocked(FIntPoint(-2050, 2700), TEXT("roof:B_ruin"), 40));
	TestFalse(TEXT("roof off the parapet"), View.IsBlocked(FIntPoint(-2050, 2700), TEXT("roof:B_ruin"), 39));
	TestFalse(TEXT("IsFree: over the cut"), View.IsFree(FIntPoint(0, 0), Ground, 0));
	TestTrue(TEXT("IsFree: cut floor"), View.IsFree(FIntPoint(0, 0), TEXT("cut"), 0));
	return true;
}

PROPS_LAYOUT_TEST(FPropsLayoutDepotExclusionsTest, "Depot.Exclusions")
bool FPropsLayoutDepotExclusionsTest::RunTest(const FString&)
{
	using namespace PropsLayoutViewTestLocal;
	FPropsLayoutView View;
	if (!LoadView(*this, GDepot, View))
	{
		return false;
	}
	FPropsExclusionParams Params;
	Params.RouteMandatoryCm = 260;
	Params.HoleRimCm = 80;
	Params.LadderCm = 300;
	const TArray<FPropsExclusion> Shapes = View.BuildExclusions(Params);
	// R_main = ground, roof:B_ruin, floor:B_ruin:1, ground: one corridor per run
	TArray<FString> RouteLayers;
	for (const FPropsExclusion& S : Shapes)
	{
		if (S.Kind == EPropsExclusionKind::Route)
		{
			TestEqual(TEXT("only the mandatory route (variants 0)"), S.SourceId, FString(TEXT("R_main")));
			RouteLayers.Add(S.WalkLayer);
		}
	}
	TestEqual(TEXT("route corridors"), RouteLayers, TArray<FString>{ TEXT("ground"), TEXT("roof:B_ruin"), TEXT("floor:B_ruin:1"), TEXT("ground") });
	// roof hole rim: half side 120 + rim 80 = a 200 cm half square on the ruin roof only
	TestTrue(TEXT("hole rim"), FPropsLayoutView::IsExcluded(Shapes, FIntPoint(-2520, 1720), TEXT("roof:B_ruin")));
	TestFalse(TEXT("past the hole rim"), FPropsLayoutView::IsExcluded(Shapes, FIntPoint(-2519, 1700), TEXT("roof:B_ruin")));
	// ladder top on the gallery roof (4100, -7810)
	TestTrue(TEXT("ladder top"), FPropsLayoutView::IsExcluded(Shapes, FIntPoint(4400, -7810), TEXT("roof:B_gallery")));
	TestFalse(TEXT("ladder top, other layer"), FPropsLayoutView::IsExcluded(Shapes, FIntPoint(4400, -7810), TEXT("ground")));
	// mandatory corridor along x 693 (the leg (693, -3182) -> (693, 7969)), 260 cm
	TestTrue(TEXT("mandatory corridor"), FPropsLayoutView::IsExcluded(Shapes, FIntPoint(953, 2000), TEXT("ground")));
	TestFalse(TEXT("outside the corridor"), FPropsLayoutView::IsExcluded(Shapes, FIntPoint(954, 2000), TEXT("ground")));
	return true;
}

// ---- the shared fixture ---------------------------------------------------------------------------------------------------------

PROPS_LAYOUT_TEST(FPropsLayoutFixtureDoorwaysTest, "Fixture.Doorways")
bool FPropsLayoutFixtureDoorwaysTest::RunTest(const FString&)
{
	using namespace PropsLayoutViewTestLocal;
	FPropsLayoutView View;
	if (!LoadView(*this, GFixture, View))
	{
		return false;
	}
	TArray<FString> Ids;
	for (const FPropsDoorway& D : View.GetDoorways())
	{
		Ids.Add(D.Id);
	}
	TestEqual(TEXT("doorways sorted by id"), Ids, TArray<FString>{ TEXT("B_shed#0"), TEXT("B_shed#1"), TEXT("F_yard_N#g0") });

	const FPropsDoorway* Gate = View.FindDoorway(TEXT("F_yard_N#g0"));
	if (TestNotNull(TEXT("yard gate"), Gate))
	{
		TestEqual(TEXT("gate pos"), Gate->Pos, FIntPoint(800, 0));
		TestEqual(TEXT("gate width"), Gate->WidthCm, 300);
		TestTrue(TEXT("gate on the mandatory route"), Gate->bOnMandatoryRoute);
		TestTrue(TEXT("R_main passes through the gate (vertex on the gate line)"), Gate->bRouteThrough);
		TestEqual(TEXT("gate outward (none)"), Gate->Outward, FIntPoint(0, 0));
	}
	const FPropsDoorway* West = View.FindDoorway(TEXT("B_shed#0"));
	if (TestNotNull(TEXT("shed door 0"), West))
	{
		TestEqual(TEXT("door 0 outward -x"), West->Outward, FIntPoint(-1, 0));
		TestEqual(TEXT("door 0 width"), West->WidthCm, 120);
		TestEqual(TEXT("door 0 group"), West->WallGroup, FString(TEXT("B_shed_S")));
		TestFalse(TEXT("door 0 off the route"), West->bOnMandatoryRoute);
		TestEqual(TEXT("door 0: R_main leg x 900 is 200 cm away"), West->MandatoryRouteDistCm, 200);
	}
	const FPropsDoorway* North = View.FindDoorway(TEXT("B_shed#1"));
	if (TestNotNull(TEXT("shed door 1"), North))
	{
		TestEqual(TEXT("door 1 outward +y"), North->Outward, FIntPoint(0, 1));
		TestEqual(TEXT("door 1 along x"), North->Along, FIntPoint(1, 0));
		TestEqual(TEXT("door 1 width"), North->WidthCm, 120);
	}
	// the mandatory-route predicate is exact at the tolerance
	TestTrue(TEXT("150 cm from the route"), View.IsNearMandatoryRoute(FIntPoint(1050, -700), TEXT("ground"), 150));
	TestFalse(TEXT("151 cm from the route"), View.IsNearMandatoryRoute(FIntPoint(1051, -700), TEXT("ground"), 150));
	TestFalse(TEXT("no mandatory leg on the roof"), View.IsNearMandatoryRoute(FIntPoint(900, -700), TEXT("roof:B_shed"), 150));
	TestEqual(TEXT("no mandatory leg on the roof: MAX"), View.DistanceToMandatoryRouteCm(FIntPoint(900, -700), TEXT("roof:B_shed")), MAX_int32);
	return true;
}

PROPS_LAYOUT_TEST(FPropsLayoutFixtureGeometryTest, "Fixture.LayersAndBlocked")
bool FPropsLayoutFixtureGeometryTest::RunTest(const FString&)
{
	using namespace PropsLayoutViewTestLocal;
	FPropsLayoutView View;
	if (!LoadView(*this, GFixture, View))
	{
		return false;
	}
	TArray<FString> Names;
	for (const FPropsWalkLayer& L : View.GetLayers())
	{
		Names.Add(L.Name);
	}
	// the fixture has a cut block with an empty polygon: the layer is listed (schema.ts layersOf) but nowhere walkable
	TestEqual(TEXT("layers"), Names, TArray<FString>{ TEXT("ground"), TEXT("cut"), TEXT("roof:B_shed") });
	TestFalse(TEXT("empty cut"), View.IsWalkable(FIntPoint(0, 0), TEXT("cut")));
	const FPropsWalkLayer* Roof = View.FindLayer(TEXT("roof:B_shed"));
	if (TestNotNull(TEXT("shed roof"), Roof))
	{
		TestEqual(TEXT("roof z"), Roof->ZCm, 300);
		TestEqual(TEXT("roof level"), Roof->Level, 1);
	}
	const FString Ground(TEXT("ground"));
	// the fence F_yard_N (x 800, 10 thick): half 5 + clearance
	TestTrue(TEXT("on the fence"), View.IsBlocked(FIntPoint(805, 500), Ground, 0));
	TestFalse(TEXT("next to the fence"), View.IsBlocked(FIntPoint(806, 500), Ground, 0));
	TestTrue(TEXT("next to the fence, clearance 1"), View.IsBlocked(FIntPoint(806, 500), Ground, 1));
	// the gate (y -150..150): its posts are 150 cm from the centre
	TestFalse(TEXT("gate centre, clearance 145 (posts 150 cm away, + 5 half thickness)"), View.IsBlocked(FIntPoint(800, 0), Ground, 144));
	TestTrue(TEXT("gate centre, clearance 145"), View.IsBlocked(FIntPoint(800, 0), Ground, 145));
	// the shed walls (30 thick): half 15
	TestTrue(TEXT("shed wall"), View.IsBlocked(FIntPoint(1115, -800), Ground, 0));
	TestFalse(TEXT("inside the shed"), View.IsBlocked(FIntPoint(1116, -800), Ground, 0));
	TestFalse(TEXT("the shed is enterable: its inside is free"), View.IsBlocked(FIntPoint(1500, -600), Ground, 100));
	// the shed roof: inset 50, parapet 20
	TestFalse(TEXT("roof inset"), View.IsWalkable(FIntPoint(1149, -600), TEXT("roof:B_shed")));
	TestTrue(TEXT("roof"), View.IsWalkable(FIntPoint(1150, -600), TEXT("roof:B_shed")));
	TestTrue(TEXT("roof parapet"), View.IsBlocked(FIntPoint(1150, -600), TEXT("roof:B_shed"), 40));
	TestFalse(TEXT("roof off the parapet"), View.IsBlocked(FIntPoint(1150, -600), TEXT("roof:B_shed"), 39));
	TestFalse(TEXT("bounds"), View.IsWalkable(FIntPoint(2001, 0), Ground));
	TestTrue(TEXT("zone Z_yard"), View.IsInZone(FIntPoint(300, 0), TEXT("Z_yard")));
	TestFalse(TEXT("not in Z_open"), View.IsInZone(FIntPoint(300, 0), TEXT("Z_open")));
	return true;
}

PROPS_LAYOUT_TEST(FPropsLayoutFixtureExclusionsTest, "Fixture.Exclusions")
bool FPropsLayoutFixtureExclusionsTest::RunTest(const FString&)
{
	using namespace PropsLayoutViewTestLocal;
	FPropsLayoutView View;
	if (!LoadView(*this, GFixture, View))
	{
		return false;
	}
	FPropsExclusionParams Params;
	Params.SpawnCm = 1500;
	Params.EndCm = 500;
	Params.LadderCm = 300;
	Params.DoorCm = 250;
	Params.RouteMandatoryCm = 260;
	const TArray<FPropsExclusion> Shapes = View.BuildExclusions(Params);
	TArray<EPropsExclusionKind> Kinds;
	for (const FPropsExclusion& S : Shapes)
	{
		Kinds.Add(S.Kind);
	}
	TestEqual(TEXT("shapes in the fixed order"), Kinds, TArray<EPropsExclusionKind>{ EPropsExclusionKind::Spawn, EPropsExclusionKind::End,
		EPropsExclusionKind::LadderFoot, EPropsExclusionKind::LadderTop, EPropsExclusionKind::Door, EPropsExclusionKind::Door,
		EPropsExclusionKind::Door, EPropsExclusionKind::Route });

	// each shape kind alone, so its edges are tested without the others overlapping
	const auto Only = [&View](TFunctionRef<void(FPropsExclusionParams&)> Set)
	{
		FPropsExclusionParams P;
		Set(P);
		return View.BuildExclusions(P);
	};
	const TArray<FPropsExclusion> Spawn = Only([](FPropsExclusionParams& P) { P.SpawnCm = 1500; });
	const TArray<FPropsExclusion> Ladder = Only([](FPropsExclusionParams& P) { P.LadderCm = 300; });
	const TArray<FPropsExclusion> Door = Only([](FPropsExclusionParams& P) { P.DoorCm = 250; });
	const TArray<FPropsExclusion> Route = Only([](FPropsExclusionParams& P) { P.RouteMandatoryCm = 260; });
	const auto In = [](const TArray<FPropsExclusion>& S, FIntPoint P, const TCHAR* Layer) { return FPropsLayoutView::IsExcluded(S, P, Layer); };

	// spawn (0, -1800), 1500 cm, every layer
	TestTrue(TEXT("spawn disk edge"), In(Spawn, FIntPoint(-1500, -1800), TEXT("ground")));
	TestFalse(TEXT("past the spawn disk"), In(Spawn, FIntPoint(-1501, -1800), TEXT("ground")));
	TestTrue(TEXT("spawn disk on the roof layer too"), In(Spawn, FIntPoint(-1500, -1800), TEXT("roof:B_shed")));
	// ladder foot = a capsule (1090, -350) -> (990, -350) on the ground; the top (1200, -350) is a disk on the roof
	TestTrue(TEXT("ladder foot capsule, side"), In(Ladder, FIntPoint(1090, -650), TEXT("ground")));
	TestTrue(TEXT("ladder foot capsule, end"), In(Ladder, FIntPoint(990, -650), TEXT("ground")));
	TestFalse(TEXT("past the ladder foot"), In(Ladder, FIntPoint(990, -651), TEXT("ground")));
	TestTrue(TEXT("ladder top on the roof"), In(Ladder, FIntPoint(1500, -350), TEXT("roof:B_shed")));
	TestFalse(TEXT("past the ladder top"), In(Ladder, FIntPoint(1501, -350), TEXT("roof:B_shed")));
	TestFalse(TEXT("ladder top is not on the ground"), In(Ladder, FIntPoint(1500, -350), TEXT("ground")));
	// doorways: 250 cm disks on the doorway's layer
	TestTrue(TEXT("shed north door"), In(Door, FIntPoint(1500, 50), TEXT("ground")));
	TestFalse(TEXT("past the shed north door"), In(Door, FIntPoint(1500, 51), TEXT("ground")));
	TestFalse(TEXT("shed north door, roof layer"), In(Door, FIntPoint(1500, 50), TEXT("roof:B_shed")));
	TestTrue(TEXT("yard gate"), In(Door, FIntPoint(800, 250), TEXT("ground")));
	// the mandatory corridor: R_main leg (900, -1500) -> (900, 0), 260 cm
	TestTrue(TEXT("corridor edge"), In(Route, FIntPoint(640, -700), TEXT("ground")));
	TestFalse(TEXT("past the corridor"), In(Route, FIntPoint(639, -700), TEXT("ground")));
	TestFalse(TEXT("corridor is ground only"), In(Route, FIntPoint(900, -700), TEXT("roof:B_shed")));
	return true;
}

// ---- inline layouts: exact integer geometry and strict reading ------------------------------------------------------------------

PROPS_LAYOUT_TEST(FPropsLayoutIntegerGeometryTest, "IntegerGeometry")
bool FPropsLayoutIntegerGeometryTest::RunTest(const FString&)
{
	using namespace PropsLayoutViewTestLocal;
	// a 15 cm wall (half 7.5) and a 400 x 100 cover rotated by 30 deg at (1000, 1000)
	const FString Json = Inline(TEXT("[{\"id\":\"W_odd\",\"a\":[-1000,-500],\"b\":[-1000,500],\"thickness\":15,\"kind\":\"wall\"}]"),
		TEXT("[{\"id\":\"C\",\"pos\":[1000,1000],\"yaw\":30,\"size\":[400,100,100],\"kind\":\"wreck\"}]"));
	FPropsLayoutView View;
	TArray<FString> Errors;
	if (!TestTrue(TEXT("builds"), FPropsLayoutView::Build(Json, View, Errors)) || !TestTrue(TEXT("no errors"), Errors.IsEmpty()))
	{
		return false;
	}
	const FString Ground(TEXT("ground"));
	TestTrue(TEXT("7 cm from a 15 cm wall"), View.IsBlocked(FIntPoint(-993, 0), Ground, 0));
	TestFalse(TEXT("8 cm from a 15 cm wall"), View.IsBlocked(FIntPoint(-992, 0), Ground, 0));
	TestTrue(TEXT("past the wall end, diagonal 3-4-5"), View.IsBlocked(FIntPoint(-1003, 504), Ground, 2)); // dist 5 <= 7.5 + 2
	TestFalse(TEXT("past the wall end, out of reach"), View.IsBlocked(FIntPoint(-1006, 508), Ground, 2));  // dist 10 > 9.5

	// local (190, 0) -> world (165, 95) + centre: inside along the long axis; a wrong rotation direction would miss it
	TestTrue(TEXT("rotated cover, long axis"), View.IsBlocked(FIntPoint(1165, 1095), Ground, 0));
	// local (215.1, 0): out at clearance 15, in at 16
	TestFalse(TEXT("rotated cover end, clearance 15"), View.IsBlocked(FIntPoint(1186, 1108), Ground, 15));
	TestTrue(TEXT("rotated cover end, clearance 16"), View.IsBlocked(FIntPoint(1186, 1108), Ground, 16));
	// local (0, 55.6): out at clearance 5, in at 6
	TestFalse(TEXT("rotated cover side, clearance 5"), View.IsBlocked(FIntPoint(972, 1048), Ground, 5));
	TestTrue(TEXT("rotated cover side, clearance 6"), View.IsBlocked(FIntPoint(972, 1048), Ground, 6));
	return true;
}

PROPS_LAYOUT_TEST(FPropsLayoutStrictReadTest, "StrictRead")
bool FPropsLayoutStrictReadTest::RunTest(const FString&)
{
	using namespace PropsLayoutViewTestLocal;
	AddExpectedMessage(TEXT("Props layout view:"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0, false); // Build logs each error
	{
		FPropsLayoutView View;
		TArray<FString> Errors;
		TestFalse(TEXT("invalid JSON"), FPropsLayoutView::Build(FString(TEXT("{ nope")), View, Errors));
		TestFalse(TEXT("foreign schema"), FPropsLayoutView::Build(FString(TEXT("{\"schema\":\"codex.formation_layout/1\"}")), View, Errors));
		TestTrue(TEXT("refused view is empty"), View.GetLayers().IsEmpty());
	}
	{
		// a wall with a wrong-typed end and one longer than the exact-geometry limit: both reported and skipped, the good wall kept
		const FString Json = Inline(TEXT("[{\"id\":\"W_bad\",\"a\":[0,0],\"b\":\"x\"},{\"id\":\"W_long\",\"a\":[-15001,0],\"b\":[15001,0]},")
			TEXT("{\"id\":\"W_ok\",\"a\":[500,-100],\"b\":[500,100],\"thickness\":10}]"));
		FPropsLayoutView View;
		TArray<FString> Errors;
		TestFalse(TEXT("bad entries fail the build"), FPropsLayoutView::Build(Json, View, Errors));
		TestEqual(TEXT("two errors"), Errors.Num(), 2);
		TestTrue(TEXT("good wall kept"), View.IsBlocked(FIntPoint(500, 0), TEXT("ground"), 0));
		TestFalse(TEXT("bad wall skipped"), View.IsBlocked(FIntPoint(0, 0), TEXT("ground"), 0));
	}
	{
		// keys are matched case-insensitively by FJsonObject (TryGetField): "ID" / "A" / "B" read like id / a / b
		const FString Json = Inline(TEXT("[{\"ID\":\"W_caps\",\"A\":[500,-100],\"B\":[500,100],\"Thickness\":10}]"));
		FPropsLayoutView View;
		TArray<FString> Errors;
		TestTrue(TEXT("mixed-case keys"), FPropsLayoutView::Build(Json, View, Errors));
		TestTrue(TEXT("mixed-case wall read"), View.IsBlocked(FIntPoint(500, 0), TEXT("ground"), 0));
	}
	return true;
}

// ---- designer 2026-11-10: "on the mandatory route" = through or turns at; gates up to 600 cm ---------------------------------

namespace PropsLayoutViewTestLocal
{
	/** Building B [[0,0],[1000,1000]] with a 180 cm door at (500, 0) (outward -y, along x) and one mandatory route. */
	FString DoorLayout(const FString& RoutePoints, const FString& ExtraWalls = FString())
	{
		return FString::Printf(TEXT("{\"schema\":\"codex.levelgen_layout/1\",\"name\":\"door\",\"bounds\":{\"min\":[-2000,-2000],\"max\":[2000,2000]},")
			TEXT("\"ground_z\":0,\"spawn\":{\"pos\":[-1900,-1900]},\"end\":[1900,1900],\"walls\":[")
			TEXT("{\"id\":\"B_S_0\",\"a\":[0,0],\"b\":[410,0],\"thickness\":30,\"kind\":\"building\",\"building\":\"B\"},")
			TEXT("{\"id\":\"B_S_1\",\"a\":[590,0],\"b\":[1000,0],\"thickness\":30,\"kind\":\"building\",\"building\":\"B\"}%s],")
			TEXT("\"buildings\":[{\"id\":\"B\",\"footprint\":[[0,0],[1000,1000]],\"floors\":1,\"enterable\":true,\"doors\":[{\"pos\":[500,0],\"floor\":0}]}],")
			TEXT("\"covers\":[],\"ladders\":[],\"zones\":[],\"routes\":[{\"id\":\"R\",\"mandatory\":true,\"points\":%s}]}"), *ExtraWalls, *RoutePoints);
	}
}

PROPS_LAYOUT_TEST(FPropsLayoutDoorOnRouteRuleTest, "Doorways.OnRouteThroughOrTurnsAt")
bool FPropsLayoutDoorOnRouteRuleTest::RunTest(const FString&)
{
	using namespace PropsLayoutViewTestLocal;
	struct FCase
	{
		const TCHAR* Name;
		const TCHAR* Points;
		bool bThrough;
		bool bTurns;
	};
	// door (500, 0), opening 180 -> reach H = 90 + 60 = 150 along the wall; turn reach 250 in front (outward = -y)
	const FCase Cases[] = {
		{ TEXT("passing by 100 cm in front"), TEXT("[[-500,-100],[1500,-100]]"), false, false },
		{ TEXT("reverses 240 cm in front"), TEXT("[[-500,-500],[500,-240],[-500,-500]]"), false, true },
		{ TEXT("reverses 250 cm in front (inclusive)"), TEXT("[[-500,-500],[450,-250],[-500,-500]]"), false, true },
		{ TEXT("reverses 260 cm in front"), TEXT("[[-500,-500],[500,-260],[-500,-500]]"), false, false },
		{ TEXT("reverses 151 cm to the side"), TEXT("[[-500,-500],[651,-100],[-500,-500]]"), false, false },
		{ TEXT("ends 200 cm in front"), TEXT("[[-500,-500],[550,-200]]"), false, true },
		{ TEXT("starts 100 cm in front"), TEXT("[[500,-100],[-1500,-100]]"), false, true },
		{ TEXT("90 degree turn in front is not a reversal"), TEXT("[[-500,-200],[500,-200],[500,-1500]]"), false, false },
		{ TEXT("reverses behind the door (inside)"), TEXT("[[200,500],[500,100],[200,500]]"), false, false },
		{ TEXT("straight through the door"), TEXT("[[500,-500],[500,500]]"), true, false },
		{ TEXT("diagonal through"), TEXT("[[400,-300],[600,300]]"), true, false },
		{ TEXT("crossing 150 cm off centre"), TEXT("[[650,-500],[650,500]]"), true, false },
		{ TEXT("crossing 151 cm off centre"), TEXT("[[651,-500],[651,500]]"), false, false },
		{ TEXT("a vertex on the wall line counts as through"), TEXT("[[500,-500],[500,0],[1500,500]]"), true, false },
		{ TEXT("a leg lying on the wall line does not cross it"), TEXT("[[-500,0],[-100,0]]"), false, false },
	};
	for (const FCase& C : Cases)
	{
		FPropsLayoutView View;
		TArray<FString> Errors;
		if (!TestTrue(FString::Printf(TEXT("%s: builds"), C.Name), FPropsLayoutView::Build(DoorLayout(C.Points), View, Errors)))
		{
			continue;
		}
		const FPropsDoorway* Door = View.FindDoorway(TEXT("B#0"));
		if (!TestNotNull(FString::Printf(TEXT("%s: door"), C.Name), Door))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s: width"), C.Name), Door->WidthCm, 180);
		TestEqual(FString::Printf(TEXT("%s: through"), C.Name), Door->bRouteThrough, C.bThrough);
		TestEqual(FString::Printf(TEXT("%s: turns at"), C.Name), Door->bRouteTurnsAt, C.bTurns);
		TestEqual(FString::Printf(TEXT("%s: on route"), C.Name), Door->bOnMandatoryRoute, C.bThrough || C.bTurns);
	}

	// a gate has no outward side: a reversal on either side within the reach counts
	const FString GateWalls = TEXT(",{\"id\":\"F_g_0\",\"a\":[-1500,-1000],\"b\":[-1500,-150],\"thickness\":10,\"kind\":\"fence\"},")
		TEXT("{\"id\":\"F_g_1\",\"a\":[-1500,150],\"b\":[-1500,1000],\"thickness\":10,\"kind\":\"fence\"}");
	for (const TCHAR* Points : { TEXT("[[-1900,-900],[-1700,0],[-1900,-900]]"), TEXT("[[-1000,-900],[-1300,0],[-1000,-900]]") })
	{
		FPropsLayoutView View;
		TArray<FString> Errors;
		if (TestTrue(TEXT("gate layout builds"), FPropsLayoutView::Build(DoorLayout(Points, GateWalls), View, Errors)))
		{
			const FPropsDoorway* G = View.FindDoorway(TEXT("F_g#g0"));
			if (TestNotNull(TEXT("gate"), G))
			{
				TestTrue(FString::Printf(TEXT("gate: reversal 200 cm away on either side (%s)"), Points), G->bRouteTurnsAt);
				TestFalse(FString::Printf(TEXT("gate: no crossing (%s)"), Points), G->bRouteThrough);
			}
		}
	}
	return true;
}

PROPS_LAYOUT_TEST(FPropsLayoutGateWidthRuleTest, "Doorways.GatesUpTo600")
bool FPropsLayoutGateWidthRuleTest::RunTest(const FString&)
{
	using namespace PropsLayoutViewTestLocal;
	const FString Json = Inline(TEXT("[")
		TEXT("{\"id\":\"F_a_0\",\"a\":[0,-2000],\"b\":[0,-300],\"thickness\":10,\"kind\":\"fence\"},")
		TEXT("{\"id\":\"F_a_1\",\"a\":[0,300],\"b\":[0,2000],\"thickness\":10,\"kind\":\"fence\"},")
		TEXT("{\"id\":\"F_b_0\",\"a\":[500,-2000],\"b\":[500,-301],\"thickness\":10,\"kind\":\"fence\"},")
		TEXT("{\"id\":\"F_b_1\",\"a\":[500,300],\"b\":[500,2000],\"thickness\":10,\"kind\":\"fence\"},")
		TEXT("{\"id\":\"CUTWALL_X_0\",\"a\":[1000,-2000],\"b\":[1000,-250],\"thickness\":20,\"kind\":\"cut_wall\"},")
		TEXT("{\"id\":\"CUTWALL_X_1\",\"a\":[1000,250],\"b\":[1000,2000],\"thickness\":20,\"kind\":\"cut_wall\"}]"));
	FPropsLayoutView View;
	TArray<FString> Errors;
	if (!TestTrue(TEXT("builds"), FPropsLayoutView::Build(Json, View, Errors)))
	{
		return false;
	}
	TestEqual(TEXT("600 cm fence gap is a gate, 601 and the cut's 500 are not"), DoorwayIds(View, EPropsDoorwayKind::Gate), TArray<FString>{ TEXT("F_a#g0") });

	FPropsLayoutConfig Narrow;
	Narrow.GateMaxGapCm = 400;
	FPropsLayoutView NarrowView;
	TestTrue(TEXT("builds with a narrower config"), FPropsLayoutView::Build(Json, NarrowView, Errors, Narrow));
	TestEqual(TEXT("the width rule comes from the config"), DoorwayIds(NarrowView, EPropsDoorwayKind::Gate).Num(), 0);
	return true;
}

PROPS_LAYOUT_TEST(FPropsLayoutConfigFromBalanceTest, "Config.FromBalance")
bool FPropsLayoutConfigFromBalanceTest::RunTest(const FString&)
{
	FString Json;
	if (!TestTrue(TEXT("props_balance.json readable"), FFileHelper::LoadFileToString(Json, *(FPaths::ProjectContentDir() / TEXT("Data/Balance/props_balance.json")))))
	{
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	if (!TestTrue(TEXT("valid JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) && Root.IsValid()))
	{
		return false;
	}
	FPropsLayoutConfig Config;
	Config.GateMinGapCm = 1;
	Config.GateMaxGapCm = 1;
	Config.DoorThroughSlackCm = 1;
	Config.DoorTurnReachCm = 1;
	Config.DoorFallbackWidthCm = 1;
	TArray<FString> Errors;
	TestTrue(TEXT("layout_view reads"), FPropsLayoutView::ReadConfig(*Root, Config, Errors));
	const FPropsLayoutConfig Defaults;
	TestEqual(TEXT("gate_min_gap_cm = default"), Config.GateMinGapCm, Defaults.GateMinGapCm);
	TestEqual(TEXT("gate_max_gap_cm = default"), Config.GateMaxGapCm, Defaults.GateMaxGapCm);
	TestEqual(TEXT("designer: gates up to 600 cm"), Config.GateMaxGapCm, 600);
	TestEqual(TEXT("door_through_slack_cm = default"), Config.DoorThroughSlackCm, Defaults.DoorThroughSlackCm);
	TestEqual(TEXT("door_turn_reach_cm = default"), Config.DoorTurnReachCm, Defaults.DoorTurnReachCm);
	TestEqual(TEXT("door_fallback_width_cm = default"), Config.DoorFallbackWidthCm, Defaults.DoorFallbackWidthCm);

	TSharedPtr<FJsonObject> Bad;
	TestTrue(TEXT("bad block parses"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(
		TEXT("{\"layout_view\":{\"gate_max_gap_cm\":\"wide\",\"door_turn_reach_cm\":-5}}")), Bad) && Bad.IsValid());
	if (Bad.IsValid())
	{
		FPropsLayoutConfig Kept;
		TArray<FString> BadErrors;
		TestFalse(TEXT("bad values are errors"), FPropsLayoutView::ReadConfig(*Bad, Kept, BadErrors));
		TestEqual(TEXT("two errors"), BadErrors.Num(), 2);
		TestEqual(TEXT("default kept on a wrong type"), Kept.GateMaxGapCm, Defaults.GateMaxGapCm);
		TestEqual(TEXT("default kept on a negative value"), Kept.DoorTurnReachCm, Defaults.DoorTurnReachCm);
	}
	return true;
}

PROPS_LAYOUT_TEST(FPropsLayoutTrainBuildingsTest, "CombatBuildings.TrainArenasOpeningsAndExclusions")
bool FPropsLayoutTrainBuildingsTest::RunTest(const FString&)
{
	using namespace PropsLayoutViewTestLocal;
	for (const TCHAR* Path : {
		TEXT("Data/LevelGen/accepted/L_Gen_Train_1.json"),
		TEXT("Data/LevelGen/accepted/L_Gen_Train_2.json"),
		TEXT("Data/LevelGen/accepted/L_Gen_Train_3.json") })
	{
		FPropsLayoutView View;
		if (!LoadView(*this, Path, View))
		{
			return false;
		}
	}

	// L_Gen_Train_1: GetSightBlockers("ground") splits window B_house_win_south_f0_1 at [450, -867]
	FPropsLayoutView Train1;
	if (!LoadView(*this, TEXT("Data/LevelGen/accepted/L_Gen_Train_1.json"), Train1))
	{
		return false;
	}
	TArray<FPropsSightSegment> GroundBlockers;
	Train1.GetSightBlockers(TEXT("ground"), GroundBlockers);
	TestFalse(TEXT("ground sight passes through window B_house_win_south_f0_1 [450,-867]"),
		FPropsLayoutView::IsSightBlocked(FIntPoint(300, -867), FIntPoint(600, -867), GroundBlockers));
	TestTrue(TEXT("solid wall pier near [450,-1150] blocks sight"),
		FPropsLayoutView::IsSightBlocked(FIntPoint(300, -1150), FIntPoint(600, -1150), GroundBlockers));

	TArray<FPropsSightSegment> Floor1Blockers;
	Train1.GetSightBlockers(TEXT("floor:B_house:1"), Floor1Blockers);
	TestFalse(TEXT("floor 1 sight passes through B_house_win_south_f1_1 [450,-867]"),
		FPropsLayoutView::IsSightBlocked(FIntPoint(300, -867), FIntPoint(600, -867), Floor1Blockers));

	// L_Gen_Train_2: B_tower hatch [-1220, -1630] excluded on roof:B_tower
	FPropsLayoutView Train2;
	if (!LoadView(*this, TEXT("Data/LevelGen/accepted/L_Gen_Train_2.json"), Train2))
	{
		return false;
	}
	FPropsExclusionParams Params;
	Params.HoleRimCm = 50;
	const TArray<FPropsExclusion> Ex2 = Train2.BuildExclusions(Params);
	TestTrue(TEXT("B_tower hatch center excluded on roof:B_tower"),
		FPropsLayoutView::IsExcluded(Ex2, FIntPoint(-1220, -1630), TEXT("roof:B_tower")));

	// L_Gen_Train_3: B_catwalk floors_detail catwalk gap [1450..1800]x[2200..2550] excluded on floor:B_catwalk:1
	FPropsLayoutView Train3;
	if (!LoadView(*this, TEXT("Data/LevelGen/accepted/L_Gen_Train_3.json"), Train3))
	{
		return false;
	}
	const TArray<FPropsExclusion> Ex3 = Train3.BuildExclusions(Params);
	TestTrue(TEXT("B_catwalk gap center excluded on floor:B_catwalk:1"),
		FPropsLayoutView::IsExcluded(Ex3, FIntPoint(1625, 2375), TEXT("floor:B_catwalk:1")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
