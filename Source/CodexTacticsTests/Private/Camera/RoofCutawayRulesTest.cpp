// CodexTactics.Camera.RoofCutaway.* — roofs disappear while operatives are inside (Demo 01 rev 5h, game designer 2026-10-15;
// URoofCutawaySubsystem / RoofCutawayRules): which pieces are cut, the footprint of a graybox building, the interior camera
// zone skip, the restore hysteresis.

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

#endif // WITH_DEV_AUTOMATION_TESTS
