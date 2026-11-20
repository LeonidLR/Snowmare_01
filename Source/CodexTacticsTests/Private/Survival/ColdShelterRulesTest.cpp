#include "Misc/AutomationTest.h"
#include "AI/PmcDefenceRules.h"
#include "Data/LevelJsonRules.h"
#include "Dom/JsonObject.h"
#include "Environment/EnvironmentRules.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Survival/ColdShelterRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// T-021: the cold indoors on generated maps (designer 2026-11-19): a building's leak from its open openings, the draught near an open
// one, the indoor test from the levelgen layout, the camera zone still winning, the level's weather_strength. UE-only, no Godot reference.

#define COLD_SHELTER_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Survival.ColdShelter." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	FPmcLayoutOpening CsMakeOpening(const TCHAR* Id, const TCHAR* Kind, const FVector2D& Pos, int32 Floor, const TCHAR* State = TEXT(""))
	{
		FPmcLayoutOpening Opening;
		Opening.Id = Id;
		Opening.Kind = Kind;
		Opening.State = State;
		Opening.Pos = Pos;
		Opening.bHasPos = true;
		Opening.Floor = Floor;
		return Opening;
	}

	/**
	 * A 20 m x 10 m hall (X 0..2000, Y 0..1000), 1 storey (roof 300). Two doors on the west wall (index 0, 1) and eight windows on the
	 * south wall (index 2..9, one every 2 m from x = 400). Ground 0. The doors carry an explicit "closed" state (a door with a leaf) unless
	 * bLeafless: a doorway gap with no state, which the designer (2026-11-19) counts as OPEN.
	 */
	FPmcLayoutAccess CsMakeHall(int32 Floors = 1, bool bLeafless = false)
	{
		FPmcLayoutAccess Layout;
		FPmcLayoutBuilding Hall;
		Hall.Id = TEXT("B_hall");
		Hall.Min = FVector2D(0.f, 0.f);
		Hall.Max = FVector2D(2000.f, 1000.f);
		Hall.Floors = Floors;
		Hall.Openings.Add(CsMakeOpening(TEXT("door_a"), TEXT("door"), FVector2D(0.f, 300.f), 0, bLeafless ? TEXT("") : TEXT("closed")));
		Hall.Openings.Add(CsMakeOpening(TEXT("door_b"), TEXT("door"), FVector2D(0.f, 700.f), 0, bLeafless ? TEXT("") : TEXT("closed")));
		for (int32 Index = 0; Index < 8; ++Index)
		{
			Hall.Openings.Add(CsMakeOpening(*FString::Printf(TEXT("win_%d"), Index), TEXT("window"), FVector2D(400.f + 200.f * Index, 0.f), 0));
		}
		Layout.Buildings.Add(Hall);
		return Layout;
	}

	TSharedPtr<FJsonObject> CsParseJson(const FString& Text)
	{
		TSharedPtr<FJsonObject> Root;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
		return Root;
	}

	/** A man's feet standing at (X, Y) on Z. */
	FVector CsFeet(float X, float Y, float Z = 0.f)
	{
		return FVector(X, Y, Z);
	}
}

COLD_SHELTER_TEST(FColdShelterSealedTest, "AllSealedIsAirtight")
bool FColdShelterSealedTest::RunTest(const FString&)
{
	const FColdShelterConfig Config;
	FPmcLayoutAccess Layout = CsMakeHall();
	const FColdShelterSample Inside = ColdShelterRules::SampleLayout(Layout, CsFeet(1000.f, 500.f), Config);
	TestTrue(TEXT("inside the hall"), Inside.IsIndoors());
	TestEqual(TEXT("closed doors + stateless windows = every opening sealed: leak 0"), Inside.Leak, 0.f);
	TestEqual(TEXT("sealed: multiplier 0"), Inside.Multiplier, 0.f);
	TestEqual(TEXT("no draught without an open opening"), Inside.NearestOpenCm, -1.f);
	// Even at the door itself.
	TestEqual(TEXT("sealed: 0 beside a closed door"), ColdShelterRules::SampleLayout(Layout, CsFeet(100.f, 300.f), Config).Multiplier, 0.f);
	// A building with no openings at all is airtight too.
	Layout.Buildings[0].Openings.Reset();
	TestEqual(TEXT("no openings: leak 0"), ColdShelterRules::SampleLayout(Layout, CsFeet(1000.f, 500.f), Config).Leak, 0.f);
	return true;
}

COLD_SHELTER_TEST(FColdShelterLeaflessDoorTest, "LeaflessDoorwayIsOpen")
bool FColdShelterLeaflessDoorTest::RunTest(const FString&)
{
	// Designer decision 2026-11-19: a doorway gap with no door leaf (no state) is OPEN, with a draught; windows stay sealed.
	const FColdShelterConfig Config;
	FPmcLayoutAccess Layout = CsMakeHall(1, true);
	FPmcLayoutBuilding& Hall = Layout.Buildings[0];
	const TArray<bool> Open = ColdShelterRules::InitialOpenFlags(Hall);
	TestEqual(TEXT("10 openings"), Open.Num(), 10);
	TestTrue(TEXT("both leafless doors are open"), Open[0] && Open[1]);
	bool bAnyWindowOpen = false;
	for (int32 Index = 2; Index < Open.Num(); ++Index)
	{
		bAnyWindowOpen |= Open[Index];
	}
	TestFalse(TEXT("stateless windows stay sealed"), bAnyWindowOpen);
	const float Leak = 6.f / 14.f;
	TestEqual(TEXT("2 leafless doors + 8 windows: leak 6/14"), ColdShelterRules::ComputeLeak(Hall, Open, Config), Leak, 1.e-5f);

	const FColdShelterSample Near = ColdShelterRules::SampleLayout(Layout, CsFeet(200.f, 300.f), Config);
	TestEqual(TEXT("sampled leak 6/14"), Near.Leak, Leak, 1.e-5f);
	TestEqual(TEXT("2 m from a leafless doorway: outdoor cold (draught)"), Near.Multiplier, 1.f);
	const FColdShelterSample Far = ColdShelterRules::SampleLayout(Layout, CsFeet(1900.f, 500.f), Config);
	TestEqual(TEXT("far wall: the base only"), Far.Multiplier, Leak, 1.e-5f);

	// An explicit door state wins: closed / locked = a leaf, sealed; kicked = open.
	TestFalse(TEXT("an explicit closed door is sealed"), ColdShelterRules::IsOpeningInitiallyOpen(CsMakeOpening(TEXT("d"), TEXT("door"), FVector2D::ZeroVector, 0, TEXT("closed"))));
	TestFalse(TEXT("a locked door is sealed"), ColdShelterRules::IsOpeningInitiallyOpen(CsMakeOpening(TEXT("d"), TEXT("door"), FVector2D::ZeroVector, 0, TEXT("locked"))));
	TestTrue(TEXT("a kicked door is open"), ColdShelterRules::IsOpeningInitiallyOpen(CsMakeOpening(TEXT("d"), TEXT("door"), FVector2D::ZeroVector, 0, TEXT("kicked"))));
	// Only DOORS: a stateless window / loophole is sealed, IsStateOpen itself stays false for an empty state.
	TestFalse(TEXT("a stateless window is sealed"), ColdShelterRules::IsOpeningInitiallyOpen(CsMakeOpening(TEXT("w"), TEXT("window"), FVector2D::ZeroVector, 0)));
	TestFalse(TEXT("a stateless loophole is sealed"), ColdShelterRules::IsOpeningInitiallyOpen(CsMakeOpening(TEXT("l"), TEXT("loophole"), FVector2D::ZeroVector, 0)));
	TestFalse(TEXT("an empty state text alone is not open"), ColdShelterRules::IsStateOpen(TEXT("")));
	return true;
}

COLD_SHELTER_TEST(FColdShelterLeakTest, "LeakWeights")
bool FColdShelterLeakTest::RunTest(const FString&)
{
	const FColdShelterConfig Config;
	FPmcLayoutAccess Layout = CsMakeHall();
	FPmcLayoutBuilding& Hall = Layout.Buildings[0];
	TArray<bool> Open = ColdShelterRules::InitialOpenFlags(Hall);
	TestEqual(TEXT("10 openings"), Open.Num(), 10);
	TestEqual(TEXT("2 doors x 3 + 8 windows x 1 = 14 of weight"), ColdShelterRules::ComputeLeak(Hall, Open, Config), 0.f);
	Open[2] = true; // one window broken
	TestEqual(TEXT("one window of 2 doors + 8 windows: 1/14"), ColdShelterRules::ComputeLeak(Hall, Open, Config), 1.f / 14.f, 1.e-5f);
	Open[2] = false;
	Open[0] = true;
	Open[1] = true;
	TestEqual(TEXT("both doors kicked: 6/14"), ColdShelterRules::ComputeLeak(Hall, Open, Config), 6.f / 14.f, 1.e-5f);
	for (bool& Flag : Open)
	{
		Flag = true;
	}
	TestEqual(TEXT("everything open: 1"), ColdShelterRules::ComputeLeak(Hall, Open, Config), 1.f);
	TestEqual(TEXT("base = leak * leak_to_cold"), ColdShelterRules::GetBaseMultiplier(6.f / 14.f, Config), 6.f / 14.f, 1.e-5f);
	FColdShelterConfig Half = Config;
	Half.LeakToCold = 0.5f;
	TestEqual(TEXT("leak_to_cold 0.5 halves the base"), ColdShelterRules::GetBaseMultiplier(1.f, Half), 0.5f);

	// Breach 4, loophole 0.5, roof hole = breach.
	FPmcLayoutBuilding Wall;
	Wall.Openings.Add(CsMakeOpening(TEXT("breach"), TEXT("breach"), FVector2D::ZeroVector, 0, TEXT("breached")));
	Wall.Openings.Add(CsMakeOpening(TEXT("slit"), TEXT("loophole"), FVector2D::ZeroVector, 0));
	Wall.Openings.Add(CsMakeOpening(TEXT("hole"), TEXT("roof_hole"), FVector2D::ZeroVector, 0));
	Wall.Openings.Add(CsMakeOpening(TEXT("glass"), TEXT("window"), FVector2D::ZeroVector, 0));
	TArray<bool> WallOpen = ColdShelterRules::InitialOpenFlags(Wall);
	TestTrue(TEXT("a breached breach is open, the rest are sealed"), WallOpen[0] && !WallOpen[1] && !WallOpen[2] && !WallOpen[3]);
	TestEqual(TEXT("breach 4 of 4 + 0.5 + 4 + 1"), ColdShelterRules::ComputeLeak(Wall, WallOpen, Config), 4.f / 9.5f, 1.e-5f);
	return true;
}

COLD_SHELTER_TEST(FColdShelterStatesTest, "OpeningStates")
bool FColdShelterStatesTest::RunTest(const FString&)
{
	for (const TCHAR* Open : { TEXT("open"), TEXT("broken"), TEXT("kicked"), TEXT("breached"), TEXT("ajar"), TEXT("destroyed") })
	{
		TestTrue(*FString::Printf(TEXT("%s lets the cold in"), Open), ColdShelterRules::IsStateOpen(Open));
	}
	for (const TCHAR* Sealed : { TEXT("closed"), TEXT("locked"), TEXT("boarded"), TEXT("glazed"), TEXT("sealed"), TEXT(""), TEXT("nonsense") })
	{
		TestFalse(*FString::Printf(TEXT("'%s' is sealed"), Sealed), ColdShelterRules::IsStateOpen(Sealed));
	}
	return true;
}

COLD_SHELTER_TEST(FColdShelterDraughtTest, "DraughtNearOpenOpening")
bool FColdShelterDraughtTest::RunTest(const FString&)
{
	const FColdShelterConfig Config;
	const float Base = 0.3f;
	TestEqual(TEXT("1 at 0 m"), ColdShelterRules::GetDraughtMultiplier(0.f, Base, Config), 1.f);
	TestEqual(TEXT("1 at 2 m"), ColdShelterRules::GetDraughtMultiplier(200.f, Base, Config), 1.f);
	TestEqual(TEXT("1 at the full radius (3.5 m)"), ColdShelterRules::GetDraughtMultiplier(350.f, Base, Config), 1.f);
	TestEqual(TEXT("the base at 10 m"), ColdShelterRules::GetDraughtMultiplier(1000.f, Base, Config), Base);
	TestEqual(TEXT("the base beyond 10 m"), ColdShelterRules::GetDraughtMultiplier(2500.f, Base, Config), Base);
	// Linear: half way (6.75 m) is the mean of 1 and the base.
	TestEqual(TEXT("linear between"), ColdShelterRules::GetDraughtMultiplier(675.f, Base, Config), 0.5f * (1.f + Base), 1.e-4f);

	// In the hall: both doors kicked (leak 6/14), a man 2 m from a door, then at the far wall.
	FPmcLayoutAccess Layout = CsMakeHall();
	Layout.Buildings[0].Openings[0].State = TEXT("kicked");
	Layout.Buildings[0].Openings[1].State = TEXT("broken");
	const float Leak = 6.f / 14.f;
	const FColdShelterSample Near = ColdShelterRules::SampleLayout(Layout, CsFeet(200.f, 300.f), Config);
	TestEqual(TEXT("leak"), Near.Leak, Leak, 1.e-5f);
	TestEqual(TEXT("base = the leak"), Near.Base, Leak, 1.e-5f);
	TestEqual(TEXT("2 m from the kicked door: outdoor cold"), Near.Multiplier, 1.f);
	TestEqual(TEXT("nearest open opening 2 m"), Near.NearestOpenCm, 200.f, 0.01f);
	const FColdShelterSample Far = ColdShelterRules::SampleLayout(Layout, CsFeet(1900.f, 500.f), Config);
	TestEqual(TEXT("far wall (19 m): the base only"), Far.Multiplier, Leak, 1.e-5f);
	TestTrue(TEXT("warmer at the far wall than at the door"), Far.Multiplier < Near.Multiplier);
	const FColdShelterSample Mid = ColdShelterRules::SampleLayout(Layout, CsFeet(675.f, 300.f), Config);
	TestEqual(TEXT("6.75 m from the door: half way between 1 and the base"), Mid.Multiplier, 0.5f * (1.f + Leak), 1.e-3f);
	// A sealed window near a kicked door is no draught of its own: the window row is on the south wall.
	TestEqual(TEXT("final = max(base, strongest draught)"), Near.Multiplier, FMath::Max(Near.Base, 1.f));
	return true;
}

COLD_SHELTER_TEST(FColdShelterStoreyTest, "DraughtSameStoreyOnly")
bool FColdShelterStoreyTest::RunTest(const FString&)
{
	const FColdShelterConfig Config;
	FPmcLayoutAccess Layout = CsMakeHall(3);
	Layout.Buildings[0].Openings[0].State = TEXT("open"); // a ground-floor door
	Layout.Buildings[0].Openings.Add(CsMakeOpening(TEXT("win_up"), TEXT("window"), FVector2D(1500.f, 0.f), 1, TEXT("broken")));
	const float Leak = ColdShelterRules::ComputeLeak(Layout.Buildings[0], ColdShelterRules::InitialOpenFlags(Layout.Buildings[0]), Config);
	TestTrue(TEXT("both count in the leak"), Leak > 3.f / 15.f);
	const FColdShelterSample GroundByDoor = ColdShelterRules::SampleLayout(Layout, CsFeet(100.f, 300.f, 0.f), Config);
	TestEqual(TEXT("ground floor, 1 m from the open door: 1"), GroundByDoor.Multiplier, 1.f);
	const FColdShelterSample UpstairsAboveDoor = ColdShelterRules::SampleLayout(Layout, CsFeet(100.f, 300.f, 300.f), Config);
	TestEqual(TEXT("first floor straight above the door: the door is on another storey, base only"), UpstairsAboveDoor.Multiplier, Leak, 1.e-5f);
	const FColdShelterSample UpstairsByWindow = ColdShelterRules::SampleLayout(Layout, CsFeet(1500.f, 200.f, 300.f), Config);
	TestEqual(TEXT("first floor beside the broken upper window: 1"), UpstairsByWindow.Multiplier, 1.f);
	TestEqual(TEXT("storey index of 0 / 300 / 280 (ramp) / 600"), ColdShelterRules::GetStoreyIndex(Layout, 0.f, Config) * 1000
		+ ColdShelterRules::GetStoreyIndex(Layout, 300.f, Config) * 100 + ColdShelterRules::GetStoreyIndex(Layout, 280.f, Config) * 10
		+ ColdShelterRules::GetStoreyIndex(Layout, 600.f, Config), 0 * 1000 + 1 * 100 + 1 * 10 + 2);
	return true;
}

COLD_SHELTER_TEST(FColdShelterInsideTest, "InsideOutsideUnderRoof")
bool FColdShelterInsideTest::RunTest(const FString&)
{
	const FColdShelterConfig Config;
	const FPmcLayoutAccess Layout = CsMakeHall(); // roof slab at 300
	TestTrue(TEXT("inside, on the floor"), ColdShelterRules::SampleLayout(Layout, CsFeet(1000.f, 500.f, 0.f), Config).IsIndoors());
	TestFalse(TEXT("outside the footprint (east)"), ColdShelterRules::SampleLayout(Layout, CsFeet(2100.f, 500.f, 0.f), Config).IsIndoors());
	TestEqual(TEXT("outside: multiplier 1"), ColdShelterRules::SampleLayout(Layout, CsFeet(2100.f, 500.f, 0.f), Config).Multiplier, 1.f);
	TestEqual(TEXT("outside: building none"), ColdShelterRules::SampleLayout(Layout, CsFeet(-50.f, 500.f, 0.f), Config).Building, (int32)INDEX_NONE);
	TestFalse(TEXT("on the roof slab: outside (feet at the roof Z)"), ColdShelterRules::SampleLayout(Layout, CsFeet(1000.f, 500.f, 300.f), Config).IsIndoors());
	TestFalse(TEXT("above the roof"), ColdShelterRules::SampleLayout(Layout, CsFeet(1000.f, 500.f, 450.f), Config).IsIndoors());
	TestTrue(TEXT("just under the ceiling (the margin is 30 cm)"), ColdShelterRules::SampleLayout(Layout, CsFeet(1000.f, 500.f, 260.f), Config).IsIndoors());
	TestFalse(TEXT("far below the ground (a trench under the footprint)"), ColdShelterRules::SampleLayout(Layout, CsFeet(1000.f, 500.f, -300.f), Config).IsIndoors());
	TestTrue(TEXT("a sunken threshold within 1 m"), ColdShelterRules::SampleLayout(Layout, CsFeet(1000.f, 500.f, -50.f), Config).IsIndoors());

	// A rotated building: a point inside the unrotated box only after the rotation.
	FPmcLayoutAccess Rotated = CsMakeHall();
	Rotated.Buildings[0].RotationDeg = 90.f;
	// Centre (1000, 500); the box turned 90 deg covers X 500..1500, Y -500..1500.
	TestTrue(TEXT("rotated: inside after the turn"), ColdShelterRules::SampleLayout(Rotated, CsFeet(1000.f, 1400.f), Config).IsIndoors());
	TestFalse(TEXT("rotated: the old corner is outside now"), ColdShelterRules::SampleLayout(Rotated, CsFeet(1900.f, 500.f), Config).IsIndoors());
	return true;
}

COLD_SHELTER_TEST(FColdShelterZoneWinsTest, "CameraZoneStillWins")
bool FColdShelterZoneWinsTest::RunTest(const FString&)
{
	const FColdShelterConfig Config;
	FPmcLayoutAccess Layout = CsMakeHall();
	const FColdShelterSample Sealed = ColdShelterRules::SampleLayout(Layout, CsFeet(1000.f, 500.f), Config);
	const FColdShelterSample Outside = ColdShelterRules::SampleLayout(Layout, CsFeet(5000.f, 500.f), Config);
	TestEqual(TEXT("a camera zone (Blizzard 2.5) beats the sealed hall"), ColdShelterRules::ResolveZoneMultiplier(true, 2.5f, Sealed, 1.f), 2.5f);
	TestEqual(TEXT("a Closed camera zone (0) beats the open outside"), ColdShelterRules::ResolveZoneMultiplier(true, 0.f, Outside, 1.f), 0.f);
	TestEqual(TEXT("no zone, in the sealed hall: 0"), ColdShelterRules::ResolveZoneMultiplier(false, 1.f, Sealed, 1.f), 0.f);
	TestEqual(TEXT("no zone, outside: the wave's multiplier unchanged (hand-made maps / today)"), ColdShelterRules::ResolveZoneMultiplier(false, 1.f, Outside, 1.5f), 1.5f);
	// G012 (2026-11-20): the indoor multiplier is a share of the outdoor cold - by an open door exactly the street, a storm wave included.
	FColdShelterSample ByDoor;
	ByDoor.Building = 0;
	ByDoor.Leak = 0.5f;
	ByDoor.Base = 0.5f;
	ByDoor.Multiplier = 1.f;
	TestEqual(TEXT("by an open door in a 2.5 storm wave: 2.5, as on the street (was 1)"), ColdShelterRules::ResolveZoneMultiplier(false, 1.f, ByDoor, 2.5f), 2.5f);
	TestEqual(TEXT("by an open door in a calm 1.0 wave: 1"), ColdShelterRules::ResolveZoneMultiplier(false, 1.f, ByDoor, 1.f), 1.f);
	FColdShelterSample Deep = ByDoor;
	Deep.Multiplier = 0.5f;
	TestEqual(TEXT("deep inside a half-leaking building in a 2.0 wave: half the street (1.0)"), ColdShelterRules::ResolveZoneMultiplier(false, 1.f, Deep, 2.f), 1.f);
	TestEqual(TEXT("a sealed hall stays 0 in a storm wave"), ColdShelterRules::ResolveZoneMultiplier(false, 1.f, Sealed, 2.5f), 0.f);
	TestEqual(TEXT("a warm 0 wave: 0 indoors too"), ColdShelterRules::ResolveZoneMultiplier(false, 1.f, ByDoor, 0.f), 0.f);
	return true;
}

COLD_SHELTER_TEST(FColdShelterParseTest, "LayoutParse")
bool FColdShelterParseTest::RunTest(const FString&)
{
	const FString Json = TEXT(R"({
		"ground_z": 0,
		"buildings": [
			{ "id": "B_a", "footprint": [[0, 0], [1000, 800]], "floors": 2,
			  "doors": [{ "pos": [0, 300], "floor": 0, "wall": "B_a_W" }],
			  "openings": [
				{ "id": "B_a_door", "kind": "door", "pos": [0, 300], "floor": 0, "state": "Kicked" },
				{ "id": "B_a_win", "kind": "window", "pos": [400, 0], "floor": 1 }
			  ],
			  "roof_holes": [[200, 300], [700, 300]] },
			{ "id": "B_legacy", "footprint": [[2000, 0], [2500, 500]], "floors": 1,
			  "doors": [{ "pos": [2000, 100], "floor": 0 }] }
		]
	})");
	FPmcLayoutAccess Layout;
	TArray<FString> Errors;
	if (!TestTrue(TEXT("parses"), PmcDefenceRules::ParseLayout(Json, Layout, Errors)) || !TestEqual(TEXT("2 buildings"), Layout.Buildings.Num(), 2))
	{
		return false;
	}
	const FPmcLayoutBuilding& A = Layout.Buildings[0];
	// openings[] wins over doors[] (not doubled) + 2 roof holes.
	TestEqual(TEXT("door + window + 2 roof holes"), A.Openings.Num(), 4);
	TestEqual(TEXT("door kind"), A.Openings[0].Kind, FString(TEXT("door")));
	TestEqual(TEXT("state lower-cased"), A.Openings[0].State, FString(TEXT("kicked")));
	TestTrue(TEXT("door position"), A.Openings[0].bHasPos && A.Openings[0].Pos.Equals(FVector2D(0.f, 300.f)));
	TestEqual(TEXT("window storey"), A.Openings[1].Floor, 1);
	TestEqual(TEXT("roof hole kind"), A.Openings[2].Kind, FString(TEXT("roof_hole")));
	TestEqual(TEXT("a roof hole is open"), A.Openings[2].State, FString(TEXT("open")));
	TestEqual(TEXT("on the top storey"), A.Openings[2].Floor, 1);
	TestTrue(TEXT("hole position"), A.Openings[3].bHasPos && A.Openings[3].Pos.Equals(FVector2D(700.f, 300.f)));
	const FPmcLayoutBuilding& Old = Layout.Buildings[1];
	TestEqual(TEXT("a layout with doors[] only: the door is an opening"), Old.Openings.Num(), 1);
	TestEqual(TEXT("legacy door kind"), Old.Openings[0].Kind, FString(TEXT("door")));
	const FColdShelterConfig Config;
	// kicked door (3) + 2 roof holes (4 + 4) of 3 + 1 + 4 + 4.
	TestEqual(TEXT("building A leak"), ColdShelterRules::ComputeLeak(A, ColdShelterRules::InitialOpenFlags(A), Config), 11.f / 12.f, 1.e-5f);
	// Designer 2026-11-19: a legacy doors[] entry has no state = a doorway with no leaf = OPEN (was sealed before the decision).
	TestEqual(TEXT("the legacy stateless door is open: leak 1"), ColdShelterRules::ComputeLeak(Old, ColdShelterRules::InitialOpenFlags(Old), Config), 1.f);
	return true;
}

COLD_SHELTER_TEST(FColdShelterConfigTest, "ConfigFromJson")
bool FColdShelterConfigTest::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> Json = CsParseJson(TEXT(R"({
		"comment": "ignored", "weights": { "_comment": "x", "door": 5, "window": 2, "breach": 6 },
		"leak_to_cold": 0.8, "draught_full_m": 2, "draught_fade_m": 8, "_comment_draught_full_m": "ignored" })"));
	if (!TestTrue(TEXT("json"), Json.IsValid()))
	{
		return false;
	}
	const FColdShelterConfig Config = ColdShelterRules::ConfigFromJson(*Json);
	TestEqual(TEXT("door"), Config.WeightDoor, 5.f);
	TestEqual(TEXT("window"), Config.WeightWindow, 2.f);
	TestEqual(TEXT("loophole keeps its default"), Config.WeightLoophole, FColdShelterConfig().WeightLoophole);
	TestEqual(TEXT("breach"), Config.WeightBreach, 6.f);
	TestEqual(TEXT("leak_to_cold"), Config.LeakToCold, 0.8f);
	TestEqual(TEXT("draught_full_m in cm"), Config.DraughtFullCm, 200.f);
	TestEqual(TEXT("draught_fade_m in cm"), Config.DraughtFadeCm, 800.f);
	const TSharedPtr<FJsonObject> Inverted = CsParseJson(TEXT(R"({ "draught_full_m": 9, "draught_fade_m": 3 })"));
	const FColdShelterConfig Fixed = ColdShelterRules::ConfigFromJson(*Inverted);
	TestTrue(TEXT("a fade shorter than the full radius is lifted to it"), Fixed.DraughtFadeCm >= Fixed.DraughtFullCm);

	// The shipped file parses and has the designer's numbers.
	FString Text;
	if (FFileHelper::LoadFileToString(Text, *(FPaths::ProjectContentDir() / TEXT("Data/Survival/cold_shelter.json"))))
	{
		const TSharedPtr<FJsonObject> File = CsParseJson(Text);
		if (TestTrue(TEXT("cold_shelter.json is valid JSON"), File.IsValid()))
		{
			const FColdShelterConfig Shipped = ColdShelterRules::ConfigFromJson(*File);
			TestEqual(TEXT("door 3"), Shipped.WeightDoor, 3.f);
			TestEqual(TEXT("window 1"), Shipped.WeightWindow, 1.f);
			TestEqual(TEXT("breach 4"), Shipped.WeightBreach, 4.f);
			TestEqual(TEXT("leak_to_cold 1"), Shipped.LeakToCold, 1.f);
			TestEqual(TEXT("3.5 m"), Shipped.DraughtFullCm, 350.f);
			TestEqual(TEXT("10 m"), Shipped.DraughtFadeCm, 1000.f);
		}
	}
	return true;
}

COLD_SHELTER_TEST(FColdShelterWeatherStrengthTest, "WeatherStrength")
bool FColdShelterWeatherStrengthTest::RunTest(const FString&)
{
	// weather_strength: 1 + (blended - 1) * strength.
	TestEqual(TEXT("strength 1 = today's blended value"), EnvironmentRules::ApplyWeatherStrength(2.f, 1.f), 2.f);
	TestEqual(TEXT("strength 1 on a mild day"), EnvironmentRules::ApplyWeatherStrength(0.7f, 1.f), 0.7f, 1.e-6f);
	TestEqual(TEXT("strength 0: no weather effect"), EnvironmentRules::ApplyWeatherStrength(2.f, 0.f), 1.f);
	TestEqual(TEXT("strength 0 on a mild day"), EnvironmentRules::ApplyWeatherStrength(0.7f, 0.f), 1.f);
	TestEqual(TEXT("strength 2: twice the deviation"), EnvironmentRules::ApplyWeatherStrength(2.f, 2.f), 3.f);
	TestEqual(TEXT("strength 2 on a mild day (never below 0)"), EnvironmentRules::ApplyWeatherStrength(0.7f, 2.f), 0.4f, 1.e-6f);
	TestEqual(TEXT("clamped at 0"), EnvironmentRules::ApplyWeatherStrength(0.f, 2.f), 0.f);
	TestEqual(TEXT("a strength over 2 is clamped to 2"), EnvironmentRules::ApplyWeatherStrength(2.f, 9.f), 3.f);
	TestEqual(TEXT("a negative strength is clamped to 0"), EnvironmentRules::ApplyWeatherStrength(2.f, -1.f), 1.f);

	// The level JSON key.
	const auto Parse = [](const TCHAR* Text)
	{
		TSharedPtr<FJsonObject> Root;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(Text)), Root);
		return LevelJsonRules::ParseEnvironmentSettings(*Root);
	};
	TestEqual(TEXT("absent: 1"), Parse(TEXT("{}")).WeatherStrength, 1.f);
	TestEqual(TEXT("0"), Parse(TEXT(R"({"weather_strength": 0})")).WeatherStrength, 0.f);
	TestEqual(TEXT("1.5"), Parse(TEXT(R"({"weather_strength": 1.5})")).WeatherStrength, 1.5f);
	TestEqual(TEXT("clamped to 2"), Parse(TEXT(R"({"weather_strength": 7})")).WeatherStrength, 2.f);
	TestEqual(TEXT("clamped to 0"), Parse(TEXT(R"({"weather_strength": -3})")).WeatherStrength, 0.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
