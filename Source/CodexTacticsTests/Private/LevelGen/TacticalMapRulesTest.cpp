// CodexTactics.LevelGen.TacticalMap.* - TACTICAL_MAP P1 (docs/design/TACTICAL_MAP.md, designer 2026-11-17: "bots read the terrain"):
// the runtime file codex.tactical_map/1 (Content/Data/LevelGen/tactical/<Map>.tactical.json, written by Tools/TrainingGround
// src/levelgen/tacticalMap.ts), the query settings codex.tactical_runtime/1 and every query of TacticalMapRules (zones near, danger,
// ambush / overwatch / window / drop spots, chokepoint on a path, detour) plus the seeded ladder-only roof picker. Pure, no world.
// UE-only (no Godot reference).

#include "LevelGen/TacticalMapRules.h"
#include "Misc/AutomationTest.h"

#include "Algo/Reverse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

#define TACTICAL_MAP_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.LevelGen.TacticalMap." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace TacticalMapTestLocal
{
	// Three places on the X axis (A only way 0.8, B narrow 0.4 with a 25 m detour, C canyon 0.3 with a 90 m one), a ladder-only drop
	// spot by B, a window far east looking -Y, two marksman roofs far away, three hiding spots along A (HS_3 stealth-only).
	const TCHAR* const Fixture = TEXT(R"json({
		"schema": "codex.tactical_map/1", "map": "L_Gen_T", "layout": "t", "geometry_hash": "abc", "analysis_version": 1, "analysis_source": "layout",
		"conditions": { "weather": "clear", "time_of_day": "day" }, "ground_z": 0,
		"thresholds": { "drop_reach_cm": 400, "window_range_m": 60, "sniper_range_m": 110, "reveal_close_m": 12, "detour_critical_m": 40, "min_score": 0.25, "ladder_only_factor": 0.65 },
		"chokepoints": [
			{ "id": "CP_A", "kind": "only_way", "tags": ["narrow", "only_way"], "score": 0.8, "layer": "ground", "pos": [500, 0, 0],
			  "line": [[0, 0, 0], [1000, 0, 0]], "poly": [[0, -200], [1000, -200], [1000, 200], [0, 200]], "length_m": 10, "width_m": 2.5, "lane_m": null,
			  "route": "mandatory", "alternative": "none", "detour_m": null, "hiding_spots": ["HS_1"], "reveal_m": 4, "reveal_stealth_m": null },
			{ "id": "CP_B", "kind": "narrow", "tags": ["narrow"], "score": 0.4, "layer": "ground", "pos": [3500, 0, 0],
			  "poly": [[3000, -200], [4000, -200], [4000, 200], [3000, 200]], "alternative": "detour", "detour_m": 25 },
			{ "id": "CP_C", "kind": "canyon", "tags": ["canyon"], "score": 0.3, "layer": "ground", "pos": [6500, 0, 0],
			  "poly": [[6000, -200], [7000, -200], [7000, 200], [6000, 200]], "alternative": "detour", "detour_m": 90 }
		],
		"drop_spots": [
			{ "id": "DA_1", "surface": "roof:B_1", "kind": "roof", "layer": "roof:B_1", "edge": [3500, 700, 300], "land": [3500, 350, 0], "drop_cm": 300, "gap_cm": 100,
			  "access": "ladder", "ladder_only": true, "jumpers": ["cutter"], "line": "route:R", "places": ["CP_B"] }
		],
		"windows": [
			{ "id": "W_1", "building": "B_9", "floor": 1, "layer": "floor:B_9:1", "kind": "window", "pos": [20000, 3000, 300], "stand": [20000, 3060, 300], "yaw": -90,
			  "covered_m": 40, "min_dist_m": 10, "places": [] }
		],
		"overwatch_roofs": [
			{ "building": "B_R", "layer": "roof:B_R", "level": 3, "z": 900, "height_cm": 900, "range_m": 110, "access": ["stair:s"], "access_kind": "stairs", "ladder_only": false,
			  "spots": [{ "pos": [10000, 10000, 900], "facing_yaw": -90, "covered_m": 1500, "longest_los_m": 100 }], "places": [] },
			{ "building": "B_S", "layer": "roof:B_S", "level": 2, "z": 600, "height_cm": 600, "range_m": 50, "access": ["ladder:l"], "access_kind": "ladder", "ladder_only": true,
			  "spots": [{ "pos": [10000, -10000, 600], "facing_yaw": 90, "covered_m": 200, "longest_los_m": 50 }], "places": [] }
		],
		"hiding_spots": [
			{ "id": "HS_1", "layer": "ground", "pos": [900, 400, 0], "facing_yaw": -90, "reveal_normal_m": 4, "reveal_stealth_m": 3, "stealth_only": false, "blind": false,
			  "rush_m": 4, "cover": "cover:C", "cover_height_cm": 120, "threatens": { "line": "route:R", "route": "R", "pos": [900, 0, 0] }, "place": "CP_A" },
			{ "id": "HS_2", "layer": "ground", "pos": [100, 400, 0], "facing_yaw": -90, "reveal_normal_m": 4, "reveal_stealth_m": 3, "stealth_only": false, "blind": true,
			  "rush_m": 4, "cover": "cover:D", "cover_height_cm": 120, "threatens": { "line": "route:R", "pos": [100, 0, 0] } },
			{ "id": "HS_3", "layer": "ground", "pos": [950, -400, 0], "facing_yaw": 90, "reveal_normal_m": 30, "reveal_stealth_m": 3, "stealth_only": true, "blind": false,
			  "rush_m": 4, "cover": "wall:W", "cover_height_cm": 300, "threatens": { "line": "route:R", "pos": [950, 0, 0] }, "place": "CP_A" }
		],
		"ladder_only_roofs": [
			{ "building": "B_1", "layer": "roof:B_1", "z": 300, "center": [3500, 900, 300], "poly": [[3000, 700], [4000, 700], [4000, 1100], [3000, 1100]],
			  "ladders": ["LAD_B_1"], "ladder_tops": [[3100, 800, 300]], "overwatch": false, "drop_spots": ["DA_1"] }
		]
	})json");

	FTacticalMapFile ParseTacticalOk(FAutomationTestBase& Test, const FString& Json)
	{
		FTacticalMapFile File;
		TArray<FString> Errors;
		Test.TestTrue(TEXT("parses"), TacticalMapRules::Parse(Json, File, Errors));
		for (const FString& Error : Errors)
		{
			Test.AddInfo(Error);
		}
		return File;
	}

	TArray<FString> Ids(const FTacticalMapFile& File, TConstArrayView<int32> Indices)
	{
		TArray<FString> Out;
		for (const int32 Index : Indices)
		{
			Out.Add(File.Chokepoints[Index].Id);
		}
		return Out;
	}

	TArray<FString> HidingIds(const FTacticalMapFile& File, TConstArrayView<FTacticalPick> Picks)
	{
		TArray<FString> Out;
		for (const FTacticalPick& Pick : Picks)
		{
			Out.Add(File.HidingSpots[Pick.Index].Id);
		}
		return Out;
	}
}

TACTICAL_MAP_TEST(FTacticalMapParseTest, "Parse")
bool FTacticalMapParseTest::RunTest(const FString&)
{
	using namespace TacticalMapTestLocal;
	const FTacticalMapFile File = ParseTacticalOk(*this, Fixture);
	TestEqual(TEXT("map"), File.Map, FString(TEXT("L_Gen_T")));
	TestEqual(TEXT("hash"), File.GeometryHash, FString(TEXT("abc")));
	TestEqual(TEXT("3 places"), File.Chokepoints.Num(), 3);
	TestEqual(TEXT("1 drop spot"), File.DropSpots.Num(), 1);
	TestEqual(TEXT("1 window"), File.Windows.Num(), 1);
	TestEqual(TEXT("2 roofs"), File.OverwatchRoofs.Num(), 2);
	TestEqual(TEXT("3 hiding spots"), File.HidingSpots.Num(), 3);
	TestEqual(TEXT("1 ladder-only roof"), File.LadderOnlyRoofs.Num(), 1);
	TestEqual(TEXT("ladder-only factor"), File.Thresholds.LadderOnlyFactor, 0.65f);
	if (File.Chokepoints.Num() == 3 && File.Windows.Num() == 1 && File.HidingSpots.Num() == 3 && File.DropSpots.Num() == 1)
	{
		const FTacticalChokepoint& A = File.Chokepoints[0];
		TestEqual(TEXT("CP_A kind"), A.Kind, FName(TEXT("only_way")));
		TestEqual(TEXT("CP_A tags"), A.Tags.Num(), 2);
		TestEqual(TEXT("CP_A poly"), A.Poly.Num(), 4);
		TestEqual(TEXT("null lane = -1"), A.LaneM, -1.f);
		TestEqual(TEXT("null detour = -1"), A.DetourM, -1.f);
		TestEqual(TEXT("CP_B detour"), File.Chokepoints[1].DetourM, 25.f);
		TestEqual(TEXT("window stand"), File.Windows[0].Stand, FVector(20000.0, 3060.0, 300.0));
		TestEqual(TEXT("window yaw"), File.Windows[0].Yaw, -90.f);
		TestTrue(TEXT("HS_3 stealth-only"), File.HidingSpots[2].bStealthOnly);
		TestEqual(TEXT("HS_1 threat point"), File.HidingSpots[0].ThreatLocation, FVector(900.0, 0.0, 0.0));
		TestTrue(TEXT("DA_1 ladder-only"), File.DropSpots[0].bLadderOnly);
		TestEqual(TEXT("DA_1 edge on the roof"), File.DropSpots[0].Edge.Z, 300.0);
		TestEqual(TEXT("roof spot z"), File.OverwatchRoofs[0].Spots[0].Location.Z, 900.0);
	}
	return true;
}

TACTICAL_MAP_TEST(FTacticalMapParseRejectsTest, "ParseRejects")
bool FTacticalMapParseRejectsTest::RunTest(const FString&)
{
	FTacticalMapFile File;
	TArray<FString> Errors;
	TestFalse(TEXT("not JSON"), TacticalMapRules::Parse(TEXT("{ nope"), File, Errors));
	TestFalse(TEXT("another schema"), TacticalMapRules::Parse(TEXT(R"({ "schema": "codex.mission_goals/1" })"), File, Errors));
	TestFalse(TEXT("a newer schema"), TacticalMapRules::Parse(TEXT(R"({ "schema": "codex.tactical_map/2" })"), File, Errors));
	Errors.Reset();
	TestTrue(TEXT("empty lists parse"), TacticalMapRules::Parse(TEXT(R"({ "schema": "codex.tactical_map/1", "chokepoints": [], "drop_spots": [], "windows": [],
		"overwatch_roofs": [], "hiding_spots": [], "ladder_only_roofs": [] })"), File, Errors));
	TestTrue(TEXT("empty map"), File.IsEmpty());
	TestEqual(TEXT("no errors"), Errors.Num(), 0);
	Errors.Reset();
	TestTrue(TEXT("a bad item is skipped, the rest kept"), TacticalMapRules::Parse(TEXT(R"({ "schema": "codex.tactical_map/1",
		"chokepoints": [{ "id": "", "pos": [0, 0, 0] }, { "id": "CP_1", "pos": [1, 2, 3], "score": 0.5 }, 7] })"), File, Errors));
	TestEqual(TEXT("one place kept"), File.Chokepoints.Num(), 1);
	TestTrue(TEXT("errors name the skipped items and the missing lists"), Errors.Num() >= 3);
	// the empty map answers "nothing here"
	const FTacticalMapFile Empty;
	const FTacticalRuntimeConfig Config;
	TestEqual(TEXT("no danger"), TacticalMapRules::DangerAt(Empty, Config, FVector::ZeroVector, ETacticalSide::Squad).Danger, 0.f);
	TestEqual(TEXT("no ambush spots"), TacticalMapRules::BestAmbushSpots(Empty, Config, FVector::ZeroVector, FVector(1000.0, 0.0, 0.0), 3).Num(), 0);
	TestFalse(TEXT("no chokepoint"), TacticalMapRules::IsChokepoint(Empty, Config, { FVector::ZeroVector, FVector(1000.0, 0.0, 0.0) }));
	TestEqual(TEXT("no ladder roofs"), TacticalMapRules::PickLadderOnlyRoofs(Empty, 1).Num(), 0);
	return true;
}

TACTICAL_MAP_TEST(FTacticalMapConfigTest, "Config")
bool FTacticalMapConfigTest::RunTest(const FString&)
{
	FString Json;
	TestTrue(TEXT("tactical_runtime.json readable"), FFileHelper::LoadFileToString(Json, *TacticalMapRules::GetConfigPath()));
	FTacticalRuntimeConfig Config;
	TArray<FString> Errors;
	TestTrue(TEXT("the shipped config parses"), TacticalMapRules::ParseConfig(Json, Config, Errors));
	TestEqual(TEXT("floor tolerance"), Config.FloorToleranceCm, 150.f);
	TestEqual(TEXT("zone falloff"), Config.ZoneFalloffCm, 300.f);
	TestEqual(TEXT("drop weight"), Config.DropWeight, 0.6f);
	TestEqual(TEXT("ambush radius"), Config.AmbushSegmentRadiusCm, 800.f);
	TestEqual(TEXT("short detour"), Config.ShortDetourM, 40.f);
	TestEqual(TEXT("smoke projection extent"), Config.ProjectExtentCm, FVector(150.0, 150.0, 250.0));
	FTacticalRuntimeConfig Partial;
	TestTrue(TEXT("a partial config keeps the defaults"), TacticalMapRules::ParseConfig(TEXT(R"({ "schema": "codex.tactical_runtime/1", "danger": { "drop_weight": 0.9 } })"), Partial, Errors));
	TestEqual(TEXT("overridden"), Partial.DropWeight, 0.9f);
	TestEqual(TEXT("default kept"), Partial.WindowWeight, 0.35f);
	FTacticalRuntimeConfig Bad;
	TestFalse(TEXT("another schema is refused"), TacticalMapRules::ParseConfig(TEXT(R"({ "schema": "x" })"), Bad, Errors));
	return true;
}

TACTICAL_MAP_TEST(FTacticalMapZonesNearTest, "ZonesNear")
bool FTacticalMapZonesNearTest::RunTest(const FString&)
{
	using namespace TacticalMapTestLocal;
	const FTacticalMapFile File = ParseTacticalOk(*this, Fixture);
	const FTacticalRuntimeConfig Config;
	TestEqual(TEXT("inside A"), Ids(File, TacticalMapRules::GetZonesNear(File, Config, FVector(500.0, 0.0, 0.0), 0.f)), TArray<FString>{ TEXT("CP_A") });
	TestEqual(TEXT("200 cm past A, radius 300"), Ids(File, TacticalMapRules::GetZonesNear(File, Config, FVector(1200.0, 0.0, 0.0), 300.f)), TArray<FString>{ TEXT("CP_A") });
	TestEqual(TEXT("radius 2000: A then B (nearest first)"), Ids(File, TacticalMapRules::GetZonesNear(File, Config, FVector(1200.0, 0.0, 0.0), 2000.f)),
		(TArray<FString>{ TEXT("CP_A"), TEXT("CP_B") }));
	TestEqual(TEXT("another floor: none"), TacticalMapRules::GetZonesNear(File, Config, FVector(500.0, 0.0, 600.0), 300.f).Num(), 0);
	TestEqual(TEXT("distance to an outline"), TacticalMapRules::DistanceToPolygon(FVector2D(1300.0, 0.0), File.Chokepoints[0].Poly), 300.f);
	return true;
}

TACTICAL_MAP_TEST(FTacticalMapDangerTest, "Danger")
bool FTacticalMapDangerTest::RunTest(const FString&)
{
	using namespace TacticalMapTestLocal;
	const FTacticalMapFile File = ParseTacticalOk(*this, Fixture);
	FTacticalRuntimeConfig Config;
	// inside A by HS_2's threatened point: the place 0.8 and the hidden enemy 0.4 -> 1 - 0.2 x 0.6
	const FTacticalDanger Inside = TacticalMapRules::DangerAt(File, Config, FVector(200.0, 0.0, 0.0), ETacticalSide::Squad);
	TestEqual(TEXT("squad danger inside A"), Inside.Danger, 0.88f, 1e-4f);
	TestEqual(TEXT("the place"), Inside.ZoneId, FString(TEXT("CP_A")));
	TestTrue(TEXT("kinds: the place's tags + hiding"), Inside.Kinds.Contains(FName(TEXT("only_way"))) && Inside.Kinds.Contains(FName(TEXT("hiding"))));
	TestEqual(TEXT("PMC side: only the place"), TacticalMapRules::DangerAt(File, Config, FVector(200.0, 0.0, 0.0), ETacticalSide::Pmc).Danger, 0.8f, 1e-4f);
	TestEqual(TEXT("150 cm outside A fades to half"), TacticalMapRules::DangerAt(File, Config, FVector(1150.0, 0.0, 0.0), ETacticalSide::Pmc).Danger, 0.4f, 1e-4f);
	TestEqual(TEXT("another floor: no place"), TacticalMapRules::DangerAt(File, Config, FVector(200.0, 0.0, 600.0), ETacticalSide::Pmc).Danger, 0.f);
	// under the ladder-only drop roof: 0.6 x 0.65
	TestEqual(TEXT("drop ambush (ladder-only)"), TacticalMapRules::DangerAt(File, Config, FVector(3500.0, 600.0, 0.0), ETacticalSide::Squad).Danger, 0.39f, 1e-4f);
	Config.bScaleLadderOnlyDrops = false;
	TestEqual(TEXT("drop ambush unscaled"), TacticalMapRules::DangerAt(File, Config, FVector(3500.0, 600.0, 0.0), ETacticalSide::Squad).Danger, 0.6f, 1e-4f);
	Config = FTacticalRuntimeConfig();
	// the window looks -Y from (20000, 3000): full to 28 m, linear to 0 at 60 m, nothing to its side
	TestEqual(TEXT("window 20 m in front"), TacticalMapRules::DangerAt(File, Config, FVector(20000.0, 1000.0, 0.0), ETacticalSide::Squad).Danger, 0.35f, 1e-4f);
	TestEqual(TEXT("window 50 m in front"), TacticalMapRules::DangerAt(File, Config, FVector(20000.0, -2000.0, 0.0), ETacticalSide::Squad).Danger, 0.35f * 0.3125f, 1e-4f);
	TestEqual(TEXT("beside the window"), TacticalMapRules::DangerAt(File, Config, FVector(23000.0, 3000.0, 0.0), ETacticalSide::Squad).Danger, 0.f);
	// inside a marksman roof's range
	const FTacticalDanger Sniper = TacticalMapRules::DangerAt(File, Config, FVector(10000.0, 2000.0, 0.0), ETacticalSide::Squad);
	TestEqual(TEXT("sniper"), Sniper.Danger, 0.1f, 1e-4f);
	TestTrue(TEXT("sniper kind"), Sniper.Kinds.Contains(FName(TEXT("sniper"))));
	TestEqual(TEXT("open ground"), TacticalMapRules::DangerAt(File, Config, FVector(-20000.0, 0.0, 0.0), ETacticalSide::Squad).Danger, 0.f);
	return true;
}

TACTICAL_MAP_TEST(FTacticalMapAmbushTest, "Ambush")
bool FTacticalMapAmbushTest::RunTest(const FString&)
{
	using namespace TacticalMapTestLocal;
	const FTacticalMapFile File = ParseTacticalOk(*this, Fixture);
	const FTacticalRuntimeConfig Config;
	// walking +X through A: HS_1 bites at the exit (0.763), the stealth-only HS_3 at x 0.5 (0.41), HS_2 at the mouth (0.363)
	const TArray<FTacticalPick> All = TacticalMapRules::BestAmbushSpots(File, Config, FVector(0.0, 0.0, 0.0), FVector(1000.0, 0.0, 0.0), 0);
	TestEqual(TEXT("ranked: exit first, stealth-only halved"), HidingIds(File, All), (TArray<FString>{ TEXT("HS_1"), TEXT("HS_3"), TEXT("HS_2") }));
	if (All.Num() == 3)
	{
		TestEqual(TEXT("HS_1 score"), All[0].Score, 0.5f * (1.f - 4.f / 12.f) + 0.3f * 0.9f + 0.2f * 0.8f, 1e-4f);
	}
	TestEqual(TEXT("n = 2"), TacticalMapRules::BestAmbushSpots(File, Config, FVector(0.0, 0.0, 0.0), FVector(1000.0, 0.0, 0.0), 2).Num(), 2);
	// walking -X the exit is at x 0: HS_2 now beats HS_1
	TestEqual(TEXT("the other way round"), HidingIds(File, TacticalMapRules::BestAmbushSpots(File, Config, FVector(1000.0, 0.0, 0.0), FVector(0.0, 0.0, 0.0), 1)),
		TArray<FString>{ TEXT("HS_2") });
	TestEqual(TEXT("a segment far away"), TacticalMapRules::BestAmbushSpots(File, Config, FVector(0.0, 5000.0, 0.0), FVector(1000.0, 5000.0, 0.0), 3).Num(), 0);
	return true;
}

TACTICAL_MAP_TEST(FTacticalMapPositionsTest, "Positions")
bool FTacticalMapPositionsTest::RunTest(const FString&)
{
	using namespace TacticalMapTestLocal;
	const FTacticalMapFile File = ParseTacticalOk(*this, Fixture);
	const FTacticalRuntimeConfig Config;
	// overwatch: B_R (110 m) reaches (10000, 0); B_S (50 m) only (10000, -6000)
	const TArray<FTacticalPick> Over = TacticalMapRules::OverwatchSpots(File, Config, FVector(10000.0, 0.0, 0.0), 0.f, 0);
	TestEqual(TEXT("one roof reaches"), Over.Num(), 1);
	if (Over.Num() == 1)
	{
		TestEqual(TEXT("B_R"), File.OverwatchRoofs[Over[0].Index].Building, FString(TEXT("B_R")));
		TestEqual(TEXT("its spot"), Over[0].SpotIndex, 0);
		TestEqual(TEXT("score"), Over[0].Score, 0.5f * (1.f - 10000.f / 11000.f) + 0.5f, 1e-4f);
	}
	const TArray<FTacticalPick> OverS = TacticalMapRules::OverwatchSpots(File, Config, FVector(10000.0, -6000.0, 0.0), 0.f, 0);
	TestTrue(TEXT("the short-range roof"), OverS.Num() == 1 && File.OverwatchRoofs[OverS[0].Index].Building == TEXT("B_S"));
	// windows: in front, beside (not / with a wide area), beyond range (not / with an area reaching in)
	TestEqual(TEXT("window in front"), TacticalMapRules::WindowPositions(File, Config, FVector(20000.0, 1000.0, 0.0), 0.f).Num(), 1);
	TestEqual(TEXT("window beside"), TacticalMapRules::WindowPositions(File, Config, FVector(23000.0, 3000.0, 0.0), 0.f).Num(), 0);
	TestEqual(TEXT("window beside, wide area"), TacticalMapRules::WindowPositions(File, Config, FVector(23000.0, 3000.0, 0.0), 3000.f).Num(), 1);
	TestEqual(TEXT("window out of range"), TacticalMapRules::WindowPositions(File, Config, FVector(20000.0, -4000.0, 0.0), 0.f).Num(), 0);
	TestEqual(TEXT("window out of range, area reaching in"), TacticalMapRules::WindowPositions(File, Config, FVector(20000.0, -4000.0, 0.0), 1500.f).Num(), 1);
	// drop spots above an area
	TestEqual(TEXT("drop spot above"), TacticalMapRules::DropSpotsAbove(File, Config, FVector(3500.0, 400.0, 0.0), 300.f).Num(), 1);
	TestEqual(TEXT("drop spot: another floor"), TacticalMapRules::DropSpotsAbove(File, Config, FVector(3500.0, 400.0, 600.0), 300.f).Num(), 0);
	TestEqual(TEXT("drop spot: too far"), TacticalMapRules::DropSpotsAbove(File, Config, FVector(0.0, 0.0, 0.0), 300.f).Num(), 0);
	return true;
}

TACTICAL_MAP_TEST(FTacticalMapPathTest, "ChokepointAndDetour")
bool FTacticalMapPathTest::RunTest(const FString&)
{
	using namespace TacticalMapTestLocal;
	const FTacticalMapFile File = ParseTacticalOk(*this, Fixture);
	const FTacticalRuntimeConfig Config;
	int32 Index = INDEX_NONE;
	TestTrue(TEXT("through A"), TacticalMapRules::IsChokepoint(File, Config, { FVector(-500.0, 0.0, 0.0), FVector(1500.0, 0.0, 0.0) }, &Index));
	TestEqual(TEXT("A"), Index, 0);
	TestFalse(TEXT("beside A"), TacticalMapRules::IsChokepoint(File, Config, { FVector(0.0, 1000.0, 0.0), FVector(1000.0, 1000.0, 0.0) }, &Index));
	TestEqual(TEXT("none"), Index, int32(INDEX_NONE));
	TestFalse(TEXT("another floor"), TacticalMapRules::IsChokepoint(File, Config, { FVector(-500.0, 0.0, 600.0), FVector(1500.0, 0.0, 600.0) }));
	TestEqual(TEXT("in path order"), Ids(File, TacticalMapRules::ChokepointsOnPath(File, Config, { FVector(8000.0, 0.0, 0.0), FVector(-500.0, 0.0, 0.0) })),
		(TArray<FString>{ TEXT("CP_C"), TEXT("CP_B"), TEXT("CP_A") }));
	const FTacticalDetour OnlyWay = TacticalMapRules::SafeDetour(File, Config, FVector(-500.0, 0.0, 0.0), FVector(1500.0, 0.0, 0.0));
	TestTrue(TEXT("A: the only way"), OnlyWay.bCrossesChokepoint && OnlyWay.bOnlyWay && !OnlyWay.bTakeDetour);
	const FTacticalDetour Short = TacticalMapRules::SafeDetour(File, Config, FVector(2500.0, 0.0, 0.0), FVector(4500.0, 0.0, 0.0));
	TestTrue(TEXT("B: a 25 m detour - take it"), Short.bCrossesChokepoint && Short.bTakeDetour && Short.DetourM == 25.f);
	const FTacticalDetour Long = TacticalMapRules::SafeDetour(File, Config, FVector(5500.0, 0.0, 0.0), FVector(7500.0, 0.0, 0.0));
	TestTrue(TEXT("C: a 90 m detour - cross"), Long.bCrossesChokepoint && !Long.bTakeDetour && !Long.bOnlyWay);
	const FTacticalDetour Both = TacticalMapRules::SafeDetour(File, Config, FVector(-500.0, 0.0, 0.0), FVector(4500.0, 0.0, 0.0));
	TestEqual(TEXT("the highest-scored crossed place decides"), Both.ChokepointIndex, 0);
	TestFalse(TEXT("open way"), TacticalMapRules::SafeDetour(File, Config, FVector(0.0, 1000.0, 0.0), FVector(1000.0, 1000.0, 0.0)).bCrossesChokepoint);
	return true;
}

TACTICAL_MAP_TEST(FTacticalMapLadderRoofsTest, "LadderRoofPicker")
bool FTacticalMapLadderRoofsTest::RunTest(const FString&)
{
	FTacticalMapFile File;
	File.Thresholds.LadderOnlyFactor = 0.65f;
	for (int32 I = 0; I < 400; ++I)
	{
		FTacticalLadderRoof Roof;
		Roof.Building = FString::Printf(TEXT("B_%03d"), I);
		File.LadderOnlyRoofs.Add(Roof);
	}
	const TArray<int32> A = TacticalMapRules::PickLadderOnlyRoofs(File, 7);
	TestTrue(TEXT("deterministic per seed"), TacticalMapRules::PickLadderOnlyRoofs(File, 7) == A);
	TestTrue(TEXT("another seed, another pick"), TacticalMapRules::PickLadderOnlyRoofs(File, 8) != A);
	TestTrue(FString::Printf(TEXT("PART of them, ~65%% (%d of 400)"), A.Num()), A.Num() >= 220 && A.Num() <= 300);
	TestEqual(TEXT("factor 0: none"), TacticalMapRules::PickLadderOnlyRoofs(File, 7, 0.f).Num(), 0);
	TestEqual(TEXT("factor 1: all"), TacticalMapRules::PickLadderOnlyRoofs(File, 7, 1.f).Num(), 400);
	// the pick is per roof (its id), not by list order
	FTacticalMapFile Reversed = File;
	Algo::Reverse(Reversed.LadderOnlyRoofs);
	TSet<FString> Names, ReversedNames;
	for (const int32 Index : A) { Names.Add(File.LadderOnlyRoofs[Index].Building); }
	for (const int32 Index : TacticalMapRules::PickLadderOnlyRoofs(Reversed, 7)) { ReversedNames.Add(Reversed.LadderOnlyRoofs[Index].Building); }
	TestTrue(TEXT("independent of the list order"), Names.Num() == ReversedNames.Num() && Names.Includes(ReversedNames));
	const double Roll = TacticalMapRules::SeededRoll(123, TEXT("B_x"));
	TestTrue(TEXT("roll in [0, 1)"), Roll >= 0.0 && Roll < 1.0);
	TestEqual(TEXT("roll stable"), TacticalMapRules::SeededRoll(123, TEXT("B_x")), Roll);
	return true;
}

TACTICAL_MAP_TEST(FTacticalMapCommittedFilesTest, "CommittedFiles")
bool FTacticalMapCommittedFilesTest::RunTest(const FString&)
{
	// the exported files of the three generated maps (Tools/TrainingGround scripts/export-tactical-map.ts); x4 counts as in
	// LEVELGEN_LAYERS_REPORT.md 7 (46 places, 7 drop spots, 163 windows, 22 roofs, 150 hiding spots) + 7 ladder-only roofs
	for (const TCHAR* Map : { TEXT("L_Gen_Test_X2"), TEXT("L_Gen_Depot_X4"), TEXT("L_Gen_Depot_1_X6") })
	{
		FString Json;
		if (!TestTrue(FString::Printf(TEXT("%s readable"), Map), FFileHelper::LoadFileToString(Json, *TacticalMapRules::GetDefaultPathForMap(Map))))
		{
			continue;
		}
		FTacticalMapFile File;
		TArray<FString> Errors;
		TestTrue(FString::Printf(TEXT("%s parses"), Map), TacticalMapRules::Parse(Json, File, Errors));
		TestEqual(FString::Printf(TEXT("%s: no errors"), Map), Errors.Num(), 0);
		TestEqual(FString::Printf(TEXT("%s map name"), Map), File.Map, FString(Map));
		TestTrue(FString::Printf(TEXT("%s has places"), Map), File.Chokepoints.Num() > 0 && File.HidingSpots.Num() > 0);
		if (FString(Map) == TEXT("L_Gen_Depot_X4"))
		{
			TestEqual(TEXT("x4 places"), File.Chokepoints.Num(), 46);
			TestEqual(TEXT("x4 drop spots"), File.DropSpots.Num(), 7);
			TestEqual(TEXT("x4 windows"), File.Windows.Num(), 163);
			TestEqual(TEXT("x4 roofs"), File.OverwatchRoofs.Num(), 22);
			TestEqual(TEXT("x4 hiding spots"), File.HidingSpots.Num(), 150);
			TestEqual(TEXT("x4 ladder-only roofs"), File.LadderOnlyRoofs.Num(), 7);
			const FTacticalRuntimeConfig Config;
			const FTacticalChokepoint& Top = File.Chokepoints[0];
			TestTrue(TEXT("x4: danger at the top place >= its score"), TacticalMapRules::DangerAt(File, Config, Top.Location, ETacticalSide::Squad).Danger >= Top.Score);
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
