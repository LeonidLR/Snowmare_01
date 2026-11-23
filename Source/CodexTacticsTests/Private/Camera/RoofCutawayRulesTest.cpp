// CodexTactics.Camera.RoofCutaway.* — roofs disappear while operatives are inside (Demo 01 rev 5h, game designer 2026-10-15;
// URoofCutawaySubsystem / RoofCutawayRules): which pieces are cut, the footprint of a graybox building, the interior camera
// zone skip, the restore hysteresis, the storey rule (designer bug 2026-11-22: every storey above the operative's is cut).

#include "Camera/RoofCutawaySubsystem.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#define ROOF_CUTAWAY_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Camera.RoofCutaway." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

ROOF_CUTAWAY_TEST(FRoofCutawayPieceRuleTest, "PieceRule")
bool FRoofCutawayPieceRuleTest::RunTest(const FString&)
{
	using namespace RoofCutawayRules;
	const FRoofCutawayConfig Config;
	TestEqual(TEXT("~10 Hz"), Config.UpdateIntervalSeconds, 0.1f);
	TestEqual(TEXT("40 cm head clearance"), Config.HeadClearanceCm, 40.f);
	TestEqual(TEXT("0.5 s hysteresis"), Config.RestoreDelaySeconds, 0.5f);

	FRoofPieceTraits Mesh;
	Mesh.bStaticMesh = true;
	// Head 180 cm above the floor at Z 0: a roof slab at 300 is cut, a ceiling at 225 too, one at 215 (closer than 40 cm) not.
	TestTrue(TEXT("roof slab 1.2 m over the head"), ShouldCutPiece(Mesh, 300.f, 180.f, 40.f));
	TestTrue(TEXT("ceiling exactly 40 cm over the head"), ShouldCutPiece(Mesh, 220.f, 180.f, 40.f));
	TestFalse(TEXT("ceiling 35 cm over the head"), ShouldCutPiece(Mesh, 215.f, 180.f, 40.f));
	TestFalse(TEXT("a wall standing on the floor"), ShouldCutPiece(Mesh, 0.f, 180.f, 40.f));

	FRoofPieceTraits Pawn = Mesh;
	Pawn.bPawn = true;
	TestFalse(TEXT("a character / vehicle is never cut"), ShouldCutPiece(Pawn, 300.f, 180.f, 40.f));
	FRoofPieceTraits Terrain = Mesh;
	Terrain.bTerrain = true;
	TestFalse(TEXT("landscape is never cut"), ShouldCutPiece(Terrain, 300.f, 180.f, 40.f));
	FRoofPieceTraits Foliage = Mesh;
	Foliage.bFoliage = true;
	TestFalse(TEXT("foliage is never cut"), ShouldCutPiece(Foliage, 300.f, 180.f, 40.f));
	FRoofPieceTraits Shape;
	TestFalse(TEXT("a trigger box (no static mesh) is never cut"), ShouldCutPiece(Shape, 300.f, 180.f, 40.f));
	FRoofPieceTraits Hidden = Mesh;
	Hidden.bVisible = false;
	TestFalse(TEXT("an invisible piece is left alone"), ShouldCutPiece(Hidden, 300.f, 180.f, 40.f));
	FRoofPieceTraits OptOut = Mesh;
	OptOut.bOptOut = true;
	TestFalse(TEXT("NoRoofCutaway: never"), ShouldCutPiece(OptOut, 300.f, 180.f, 40.f));
	FRoofPieceTraits OptIn = Mesh;
	OptIn.bOptIn = true;
	TestTrue(TEXT("RoofCutaway: forced even below the clearance"), ShouldCutPiece(OptIn, 150.f, 180.f, 40.f));
	OptIn.bOptOut = true;
	TestFalse(TEXT("opt-out wins over opt-in"), ShouldCutPiece(OptIn, 150.f, 180.f, 40.f));
	TestEqual(TEXT("tags"), OptOutTag(), FName(TEXT("NoRoofCutaway")));
	TestEqual(TEXT("tags"), OptInTag(), FName(TEXT("RoofCutaway")));
	return true;
}

ROOF_CUTAWAY_TEST(FRoofCutawayFootprintTest, "Footprint")
bool FRoofCutawayFootprintTest::RunTest(const FString&)
{
	using namespace RoofCutawayRules;
	// A 6 x 6 m roof slab.
	const FBox Roof(FVector(-300.f, -300.f, 290.f), FVector(300.f, 300.f, 310.f));
	TestTrue(TEXT("a ceiling under it"), OverlapsFootprint(Roof, FBox(FVector(-200.f, -200.f, 250.f), FVector(200.f, 200.f, 260.f)), 10.f));
	TestTrue(TEXT("an upper floor sticking out on one side"), OverlapsFootprint(Roof, FBox(FVector(100.f, -250.f, 400.f), FVector(700.f, 250.f, 420.f)), 10.f));
	TestFalse(TEXT("the neighbour's roof touching the edge (5 cm)"), OverlapsFootprint(Roof, FBox(FVector(295.f, -300.f, 290.f), FVector(900.f, 300.f, 310.f)), 10.f));
	TestFalse(TEXT("a roof beside it"), OverlapsFootprint(Roof, FBox(FVector(400.f, -300.f, 290.f), FVector(900.f, 300.f, 310.f)), 10.f));
	TestTrue(TEXT("a hut is building-sized"), IsBuildingSized(Roof, 6000.f));
	TestFalse(TEXT("a 100 m slab over the level is not"), IsBuildingSized(FBox(FVector(-5000.f, -300.f, 0.f), FVector(5000.f, 300.f, 10.f)), 6000.f));
	TestFalse(TEXT("no box"), IsBuildingSized(FBox(ForceInit), 6000.f));
	return true;
}

ROOF_CUTAWAY_TEST(FRoofCutawayZoneAndRestoreTest, "ZoneAndRestore")
bool FRoofCutawayZoneAndRestoreTest::RunTest(const FString&)
{
	using namespace RoofCutawayRules;
	TestTrue(TEXT("no interior camera: cut"), ShouldCutForOperative(false));
	TestFalse(TEXT("inside a zone whose fixed camera is the view: its own camera sees the interior"), ShouldCutForOperative(true));
	// (The M outside view hides the zone camera: IsZoneCameraShown false -> cut, checked by RoofCutawaySmoke.)
	TestFalse(TEXT("0.3 s after he left: still cut"), ShouldRestore(10.0, 10.3, 0.5f));
	TestTrue(TEXT("0.5 s after he left: restored"), ShouldRestore(10.0, 10.5, 0.5f));
	TestTrue(TEXT("long gone"), ShouldRestore(10.0, 20.0, 0.5f));
	return true;
}

ROOF_CUTAWAY_TEST(FRoofCutawayHiddenShadowTest, "HiddenShadow")
bool FRoofCutawayHiddenShadowTest::RunTest(const FString&)
{
	using namespace RoofCutawayRules;
	// Designer 2026-11-22: interiors must not stay dark - a cut roof / storey casts no shadow (Codex.RoofCutaway.HiddenShadow 0).
	TestFalse(TEXT("default: a cut piece casts no shadow"), CastsShadowWhileCut(true, false));
	TestTrue(TEXT("HiddenShadow 1 (the old rule): it keeps its shadow"), CastsShadowWhileCut(true, true));
	TestFalse(TEXT("a piece that never cast one does not start to"), CastsShadowWhileCut(false, true));
	TestFalse(TEXT("... nor with the default"), CastsShadowWhileCut(false, false));
	// Restore puts the piece's own setting back whatever it was while cut (URoofCutawaySubsystem::RestorePiece; RoofCutawaySmoke).
	return true;
}

ROOF_CUTAWAY_TEST(FRoofCutawayFollowSelectedTest, "FollowSelected")
bool FRoofCutawayFollowSelectedTest::RunTest(const FString&)
{
	using namespace RoofCutawayRules;
	// Bug report 2026-11-02 item 8: 2 men on a roof, 1 under it - the roof follows the man the player works with (the squad leader).
	TestTrue(TEXT("the selected man under a roof: his roof is cut"), ShouldEvaluateOperative(true, true));
	TestFalse(TEXT("a team-mate under the roof while the selected man stands on it: no cut for him"), ShouldEvaluateOperative(false, true));
	TestTrue(TEXT("Codex.RoofCutaway.FollowSelected 0 (the old rule): every operative counts"), ShouldEvaluateOperative(false, false));
	TestTrue(TEXT("... the selected one too"), ShouldEvaluateOperative(true, false));
	return true;
}

ROOF_CUTAWAY_TEST(FRoofCutawayStoreyTagsTest, "StoreyTags")
bool FRoofCutawayStoreyTagsTest::RunTest(const FString&)
{
	using namespace RoofCutawayRules;
	// The level generator's actor tags (build_levelgen_map.py) and kit_dress's component tags name the building and the storey.
	FString Building;
	int32 Storey = 0;
	const TArray<FName> Wall = { TEXT("LevelGen"), TEXT("interior"), TEXT("building=S04_rubble_escape"), TEXT("floor=2"), TEXT("structure=partition") };
	TestTrue(TEXT("a generated wall names its building"), ParseStoreyTags(Wall, Building, Storey));
	TestEqual(TEXT("... the building id"), Building, FString(TEXT("S04_rubble_escape")));
	TestEqual(TEXT("... its storey"), Storey, 2);
	const TArray<FName> Slab = { TEXT("LevelGen"), TEXT("floor_slab"), TEXT("building=S04_rubble_escape"), TEXT("floor=1") };
	TestTrue(TEXT("a floor slab"), ParseStoreyTags(Slab, Building, Storey));
	TestEqual(TEXT("floor_slab is no floor=<n> tag"), Storey, 1);
	TestTrue(TEXT("floor_slab is a slab tag"), IsSlabTag(TEXT("floor_slab")));
	TestTrue(TEXT("roof_slab too"), IsSlabTag(TEXT("roof_slab")));
	TestFalse(TEXT("a balcony slab is not (a man under a balcony stays outside)"), IsSlabTag(TEXT("balcony_slab")));
	const TArray<FName> Ramp = { TEXT("LevelGen"), TEXT("stair=st1"), TEXT("switchback"), TEXT("building=S04_rubble_escape") };
	TestTrue(TEXT("a piece with a building but no storey"), ParseStoreyTags(Ramp, Building, Storey));
	TestEqual(TEXT("... has no storey (placed by its bottom)"), Storey, static_cast<int32>(INDEX_NONE));
	const TArray<FName> Kit = { TEXT("LevelGen"), TEXT("KitDress"), TEXT("kit_building=S04_rubble_escape"), TEXT("kit_series=panel_1-464") };
	TestFalse(TEXT("the kit actor's kit_building= is not building="), ParseStoreyTags(Kit, Building, Storey));
	const TArray<FName> Bad = { TEXT("building=B_x"), TEXT("floor=top") };
	TestTrue(TEXT("a non-numeric floor tag"), ParseStoreyTags(Bad, Building, Storey));
	TestEqual(TEXT("... gives no storey"), Storey, static_cast<int32>(INDEX_NONE));
	return true;
}

ROOF_CUTAWAY_TEST(FRoofCutawayStoreyRuleTest, "StoreyRule")
bool FRoofCutawayStoreyRuleTest::RunTest(const FString&)
{
	using namespace RoofCutawayRules;
	const FRoofCutawayConfig Config;
	TestEqual(TEXT("50 cm storey tolerance"), Config.StoreyToleranceCm, 50.f);
	TestEqual(TEXT("2 m keep-inside"), Config.StoreyKeepInsideCm, 200.f);
	// A 3-storey khrushchevka on ground 0: walls of each storey stand on its base (300 cm storeys), a slab's bottom 20 cm under it,
	// lintels at +220; the roof (storey 3) is the roof slab + parapets.
	TMap<int32, TArray<float>> Bottoms;
	Bottoms.Add(0, { 0.f, 0.f, 0.f, 0.f, 220.f, -5.f });
	Bottoms.Add(1, { 280.f, 300.f, 300.f, 300.f, 520.f });
	Bottoms.Add(2, { 580.f, 600.f, 600.f, 820.f });
	Bottoms.Add(3, { 880.f, 900.f, 900.f });
	const TArray<FStoreyBase> Bases = StoreyBasesFromBottoms(Bottoms);
	TestEqual(TEXT("four storeys incl. the roof"), Bases.Num(), 4);
	if (Bases.Num() != 4)
	{
		return false;
	}
	TestEqual(TEXT("ground floor base"), Bases[0].BaseZ, 0.f);
	TestEqual(TEXT("storey 1 base (the slab bottom and the lintel do not move it)"), Bases[1].BaseZ, 300.f);
	TestEqual(TEXT("storey 2 base"), Bases[2].BaseZ, 600.f);
	TestEqual(TEXT("the roof"), Bases[3].BaseZ, 900.f);
	TestEqual(TEXT("sorted by storey"), Bases[3].Storey, 3);

	TestEqual(TEXT("feet on the ground floor"), StoreyAt(Bases, 0.f, 50.f), 0);
	TestEqual(TEXT("on the half landing (+150): still the ground floor"), StoreyAt(Bases, 150.f, 50.f), 0);
	TestEqual(TEXT("40 cm under the landing: already storey 1"), StoreyAt(Bases, 260.f, 50.f), 1);
	TestEqual(TEXT("60 cm under it: not yet"), StoreyAt(Bases, 240.f, 50.f), 0);
	TestEqual(TEXT("on storey 1"), StoreyAt(Bases, 300.f, 50.f), 1);
	TestEqual(TEXT("on storey 2"), StoreyAt(Bases, 605.f, 50.f), 2);
	TestEqual(TEXT("on the roof"), StoreyAt(Bases, 900.f, 50.f), 3);
	TestEqual(TEXT("a basement / the street below the plinth"), StoreyAt(Bases, -100.f, 50.f), static_cast<int32>(INDEX_NONE));

	// The designer's rule: ground floor -> 1, 2 and the roof hidden; storey 1 -> 2 and the roof hidden, 0 and 1 visible.
	TestFalse(TEXT("ground floor: own storey stays"), ShouldCutStorey(0, 0));
	TestTrue(TEXT("ground floor: storey 1 hidden"), ShouldCutStorey(1, 0));
	TestTrue(TEXT("ground floor: storey 2 hidden"), ShouldCutStorey(2, 0));
	TestTrue(TEXT("ground floor: the roof hidden"), ShouldCutStorey(3, 0));
	TestFalse(TEXT("storey 1: the ground floor stays"), ShouldCutStorey(0, 1));
	TestFalse(TEXT("storey 1: own storey stays"), ShouldCutStorey(1, 1));
	TestTrue(TEXT("storey 1: storey 2 hidden"), ShouldCutStorey(2, 1));
	TestTrue(TEXT("storey 1: the roof hidden"), ShouldCutStorey(3, 1));
	TestFalse(TEXT("on the roof: nothing above"), ShouldCutStorey(3, 3));
	TestFalse(TEXT("no storey: never"), ShouldCutStorey(INDEX_NONE, 0));
	TestFalse(TEXT("outside any building: never"), ShouldCutStorey(2, INDEX_NONE));
	return true;
}

ROOF_CUTAWAY_TEST(FRoofCutawayStoreyInsideTest, "StoreyInside")
bool FRoofCutawayStoreyInsideTest::RunTest(const FString&)
{
	using namespace RoofCutawayRules;
	// A 12 x 20 m slab strip.
	const FBox Slab(FVector(1300.f, 2600.f, 280.f), FVector(2500.f, 4600.f, 300.f));
	TestEqual(TEXT("over the slab: 0"), DistanceToBox2D(Slab, FVector2D(1900.0, 3600.0)), 0.f);
	TestEqual(TEXT("1 m east of it"), DistanceToBox2D(Slab, FVector2D(2600.0, 3600.0)), 100.f, 0.01f);
	TestEqual(TEXT("off the corner (3-4-5)"), DistanceToBox2D(Slab, FVector2D(2800.0, 5000.0)), 500.f, 0.01f);
	TestTrue(TEXT("under a slab: inside"), IsInsideBuilding(0.f, false, 200.f));
	TestFalse(TEXT("1.5 m off the slabs, coming from the street: outside"), IsInsideBuilding(150.f, false, 200.f));
	TestTrue(TEXT("1.5 m off the slabs (a stair hole) while he was inside: still inside"), IsInsideBuilding(150.f, true, 200.f));
	TestFalse(TEXT("2.5 m out of the door: outside"), IsInsideBuilding(250.f, true, 200.f));

	TestTrue(TEXT("a stair ramp over the slabs is claimed"), ClaimsUntaggedPiece(Slab, FBox(FVector(1400.f, 3000.f, 0.f), FVector(1530.f, 3400.f, 150.f)), 50.f));
	TestTrue(TEXT("a facade piece on the outer wall face (centre 30 cm outside)"), ClaimsUntaggedPiece(Slab, FBox(FVector(2510.f, 3000.f, 300.f), FVector(2550.f, 3300.f, 600.f)), 50.f));
	TestFalse(TEXT("a street lamp 3 m away"), ClaimsUntaggedPiece(Slab, FBox(FVector(2800.f, 3000.f, 0.f), FVector(2820.f, 3020.f, 600.f)), 50.f));
	TestFalse(TEXT("the ground plane under the whole level"), ClaimsUntaggedPiece(Slab, FBox(FVector(-10000.f, -10000.f, -10.f), FVector(10000.f, 10000.f, 0.f)), 50.f));
	TestFalse(TEXT("no slabs"), ClaimsUntaggedPiece(FBox(ForceInit), FBox(FVector(0.f), FVector(10.f)), 50.f));
	return true;
}

ROOF_CUTAWAY_TEST(FRoofCutawayCursorSkipTest, "Cursor.SkipsHiddenStoreys")
bool FRoofCutawayCursorSkipTest::RunTest(const FString&)
{
	// Designer bug 2026-11-22 (dressed showcase): the cursor ray passes what the storey cutaway hides for the selected man.
	using namespace RoofCutawayRules;
	const FRoofCutawayConfig Config;
	const float Slack = Config.CursorCeilingSlackCm;
	// He is on storey 0 (base 0), the storey above starts at 300.
	TestTrue(TEXT("a cut piece is always passed"), ShouldSkipCursorHit(true, INDEX_NONE, INDEX_NONE, 0.f, 300.f, false, Slack));
	TestTrue(TEXT("storey 1 of his building (hidden, a kit map's grey proxy too)"), ShouldSkipCursorHit(false, 1, 0, 310.f, 300.f, false, Slack));
	TestTrue(TEXT("the roof storey of his building"), ShouldSkipCursorHit(false, 2, 0, 610.f, 300.f, false, Slack));
	TestFalse(TEXT("his own floor"), ShouldSkipCursorHit(false, 0, 0, 5.f, 300.f, false, Slack));
	TestFalse(TEXT("a wall face of his storey at chest height"), ShouldSkipCursorHit(false, 0, 0, 120.f, 300.f, false, Slack));
	TestTrue(TEXT("the top of a wall of his storey under the cut storey"), ShouldSkipCursorHit(false, 0, 0, 290.f, 300.f, false, Slack));
	TestFalse(TEXT("the top of the stair flight up (visible)"), ShouldSkipCursorHit(false, 0, 0, 295.f, 300.f, true, Slack));
	TestFalse(TEXT("a storey below his (visible through the stair hole)"), ShouldSkipCursorHit(false, 0, 1, 5.f, 600.f, false, Slack));
	TestFalse(TEXT("another building (no storey of his)"), ShouldSkipCursorHit(false, INDEX_NONE, 0, 900.f, 300.f, false, Slack));
	TestFalse(TEXT("he is in no building"), ShouldSkipCursorHit(false, 1, INDEX_NONE, 310.f, TNumericLimits<float>::Max(), false, Slack));
	TestFalse(TEXT("on the roof storey nothing is passed"), ShouldSkipCursorHit(false, 2, 2, 650.f, TNumericLimits<float>::Max(), false, Slack));
	return true;
}

ROOF_CUTAWAY_TEST(FRoofCutawayInteriorFloorTest, "Cursor.InteriorFloor")
bool FRoofCutawayInteriorFloorTest::RunTest(const FString&)
{
	// Designer bug 2026-11-22: a click on a visible floor inside a building beats the cover hug on its wall.
	using namespace RoofCutawayRules;
	TestTrue(TEXT("the floor of a ground-floor flat"), IsInteriorFloorHit(1.f, 0.f, 2.f, 0.f, 620.f, 50.f));
	TestTrue(TEXT("the floor of storey 1"), IsInteriorFloorHit(1.f, 0.f, 305.f, 0.f, 620.f, 50.f));
	TestFalse(TEXT("a wall face"), IsInteriorFloorHit(0.f, 0.f, 120.f, 0.f, 620.f, 50.f));
	TestFalse(TEXT("the street beside the house"), IsInteriorFloorHit(1.f, 35.f, 0.f, 0.f, 620.f, 50.f));
	TestFalse(TEXT("the roof top (outside)"), IsInteriorFloorHit(1.f, 0.f, 620.f, 0.f, 620.f, 50.f));
	TestFalse(TEXT("a pit under the house"), IsInteriorFloorHit(1.f, 0.f, -200.f, 0.f, 620.f, 50.f));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
