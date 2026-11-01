#include "Misc/AutomationTest.h"
#include "Core/MissionRules.h"
#include "Data/LevelJsonRules.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

// Level JSON read at runtime (Wave Editor data, Content/Data/LevelJson) — same contract as the former import_levels.py.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLevelJsonParseTest, "CodexTactics.LevelJson.ParseContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelJsonParseTest::RunTest(const FString&)
{
	const FString Json = TEXT(R"({
		"level_id": "test_level", "level_name": "Test", "prep_phase_duration": 45, "wave_rest_duration": 15,
		"waves": [
			{ "wave_index": 1, "name": "One", "max_simultaneous_enemies": 6,
			  "spawns": [ { "enemy_type": "HOUND", "count": 4, "spawn_lane": "NORTH", "spawn_delay_sec": 0.5, "initial_delay_sec": 2 },
			              { "enemy_type": "marksman", "count": 1, "custom_stats": { "health": 150, "speed": 3.5 } } ],
			  "wave_modifiers": { "enemy_hp_mult": 1.2, "cold_drain_mult": 1.5 } },
			{ "wave_index": 2, "is_active": false, "spawns": [ { "enemy_type": "BRUTE", "count": 9 } ] },
			{ "wave_index": 3, "spawns": [ { "enemy_type": "BRUTE", "count": 2 } ] } ]
	})");
	FLevelCombatConfig Config;
	FString Error;
	TestTrue(TEXT("Parses"), LevelJsonRules::ParseLevel(Json, TEXT("fallback"), Config, Error));
	TestEqual(TEXT("Id"), Config.LevelId, FString(TEXT("test_level")));
	TestEqual(TEXT("Prep"), Config.PrepPhaseDuration, 45.f);
	TestEqual(TEXT("Rest"), Config.WaveRestDuration, 15.f);
	TestEqual(TEXT("Inactive wave skipped"), Config.Waves.Num(), 2);
	if (Config.Waves.Num() == 2)
	{
		const FWaveDefinition& One = Config.Waves[0];
		TestEqual(TEXT("Max simultaneous"), One.MaxSimultaneousEnemies, 6);
		TestEqual(TEXT("Total"), One.GetTotalEnemyCount(), 5);
		TestTrue(TEXT("Hound"), One.Spawns[0].EnemyType == EEnemyArchetype::FrostHound);
		TestEqual(TEXT("Lane"), One.Spawns[0].SpawnLane, FString(TEXT("NORTH")));
		TestEqual(TEXT("Initial delay"), One.Spawns[0].InitialDelaySec, 2.f);
		TestTrue(TEXT("Marksman (any case)"), One.Spawns[1].EnemyType == EEnemyArchetype::Marksman);
		TestEqual(TEXT("Custom health"), One.Spawns[1].CustomHealth, 150.f);
		TestEqual(TEXT("Custom speed, m/s"), One.Spawns[1].CustomSpeed, 3.5f);
		TestEqual(TEXT("Default lane"), One.Spawns[1].SpawnLane, FString(TEXT("ANY")));
		TestEqual(TEXT("HP mult"), One.Modifiers.EnemyHpMult, 1.2f);
		TestEqual(TEXT("Damage mult default"), One.Modifiers.EnemyDamageMult, 1.f);
		TestEqual(TEXT("Wave 3 kept its index"), Config.Waves[1].WaveIndex, 3);
	}
	TestEqual(TEXT("cold_rate_mult default 1 (every other level unchanged)"), Config.ColdRateMult, 1.f);
	FLevelCombatConfig Cold;
	TestTrue(TEXT("cold_rate_mult parses"), LevelJsonRules::ParseLevel(TEXT(R"({"level_id":"c","cold_rate_mult":0.8})"), TEXT("c"), Cold, Error));
	TestEqual(TEXT("cold_rate_mult 0.8"), Cold.ColdRateMult, 0.8f, 0.0001f);
	LevelJsonRules::ParseLevel(TEXT(R"({"level_id":"c","cold_rate_mult":-2})"), TEXT("c"), Cold, Error);
	TestEqual(TEXT("cold_rate_mult clamps to >= 0"), Cold.ColdRateMult, 0.f);
	TestEqual(TEXT("Loadout default mode"), Config.SquadLoadout.SimulationMode, FString(TEXT("EXPLORE_AND_COLLECT")));
	TestEqual(TEXT("Loadout default ammo"), Config.SquadLoadout.M16Ammo, 120);

	FLevelCombatConfig Bad;
	TestFalse(TEXT("Unknown type rejected"), LevelJsonRules::ParseLevel(TEXT(R"({"waves":[{"spawns":[{"enemy_type":"DRAGON"}]}]})"),
		TEXT("x"), Bad, Error));
	TestTrue(TEXT("Error names the type"), Error.Contains(TEXT("DRAGON")));
	TestFalse(TEXT("Invalid JSON rejected"), LevelJsonRules::ParseLevel(TEXT("{ nope"), TEXT("x"), Bad, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLevelJsonFilesParseTest, "CodexTactics.LevelJson.AllFilesParse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelJsonFilesParseTest::RunTest(const FString&)
{
	// Every level file the game / Wave Editor uses parses and has waves with enemies. (Until 2026-10-04 this compared
	// the files with the DA_Level_* assets imported from the Godot archive; the JSON is the reference now and is edited
	// in the Wave Editor, the assets are only the fallback.)
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(LevelJsonRules::GetLevelsDirectory() / TEXT("*.json")), true, false);
	TestTrue(TEXT("Level files present"), Files.Num() >= 13);
	for (const FString& File : Files)
	{
		if (FPaths::GetBaseFilename(File) == TEXT("stage_template"))
		{
			continue;
		}
		FLevelCombatConfig Config;
		FString Error;
		if (!TestTrue(FString::Printf(TEXT("%s parses (%s)"), *File, *Error), LevelJsonRules::LoadLevel(File, Config, Error)))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("%s has waves"), *File), Config.Waves.Num() > 0);
		for (const FWaveDefinition& Wave : Config.Waves)
		{
			TestTrue(FString::Printf(TEXT("%s wave %d has enemies"), *File, Wave.WaveIndex), Wave.GetTotalEnemyCount() > 0);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLevelJsonPerMapTest, "CodexTactics.LevelJson.PerMapResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelJsonPerMapTest::RunTest(const FString&)
{
	const FString Default = TEXT("level_01_outpost.json");
	TestEqual(TEXT("PIE prefix stripped"), LevelJsonRules::NormalizeMapName(TEXT("UEDPIE_0_L_Demo01_A_FrozenLake")), FString(TEXT("L_Demo01_A_FrozenLake")));
	TestEqual(TEXT("Package path stripped"), LevelJsonRules::NormalizeMapName(TEXT("/Game/Maps/L_Demo01_A_FrozenLake")), FString(TEXT("L_Demo01_A_FrozenLake")));
	TestEqual(TEXT("Plain name kept"), LevelJsonRules::NormalizeMapName(TEXT("L_MovementTest")), FString(TEXT("L_MovementTest")));
	TestEqual(TEXT("Map file wins over the default"), LevelJsonRules::ChooseLevelJsonFile(TEXT("UEDPIE_1_L_X"), true, Default, FString()), FString(TEXT("L_X.json")));
	TestEqual(TEXT("No map file -> default"), LevelJsonRules::ChooseLevelJsonFile(TEXT("L_X"), false, Default, FString()), Default);
	TestEqual(TEXT("Command line wins over the map file"), LevelJsonRules::ChooseLevelJsonFile(TEXT("L_X"), true, Default, TEXT("C:/a.json")), FString(TEXT("C:/a.json")));
	TestEqual(TEXT("Empty default stays empty"), LevelJsonRules::ChooseLevelJsonFile(TEXT("L_X"), false, FString(), FString()), FString());

	// Real files: the demo map resolves to its own JSON (demo flags, no intro window); maps without a file keep the default.
	const FString Demo = LevelJsonRules::ResolveLevelJsonFile(TEXT("UEDPIE_0_L_Demo01_A_FrozenLake"), Default, FString());
	TestEqual(TEXT("Demo map file"), Demo, FString(TEXT("L_Demo01_A_FrozenLake.json")));
	TestEqual(TEXT("Movement test keeps the default"), LevelJsonRules::ResolveLevelJsonFile(TEXT("L_MovementTest"), Default, FString()), Default);
	TestEqual(TEXT("Patrol test keeps the default"), LevelJsonRules::ResolveLevelJsonFile(TEXT("L_PatrolTest"), Default, FString()), Default);
	TestEqual(TEXT("-LevelJson= still wins"), LevelJsonRules::ResolveLevelJsonFile(TEXT("L_Demo01_A_FrozenLake"), Default, Default), Default);

	FLevelCombatConfig Config;
	FString Error;
	if (TestTrue(FString::Printf(TEXT("Demo level loads (%s)"), *Error), LevelJsonRules::LoadLevel(Demo, Config, Error)))
	{
		TestTrue(TEXT("Demo flag"), Config.Demo.bDemo);
		TestFalse(TEXT("Panic off"), Config.Demo.bPanicEnabled);
		TestTrue(TEXT("Tutorial on"), Config.Demo.bTutorial);
		TestTrue(TEXT("Demo HUD"), Config.Demo.bDemoHud);
		TestFalse(TEXT("Intro dialogue off"), Config.Demo.bIntroDialogue);
		TestFalse(TEXT("The intro briefing does not open"), MissionRules::ShouldOpenIntroDialogue(false, Config.Demo.bDemo, Config.Demo.bIntroDialogue));
		TestFalse(TEXT("Mid-fight horde off"), Config.bHordeEnabled);
		TestEqual(TEXT("Three waves"), Config.Waves.Num(), 3);
		for (const FWaveDefinition& Wave : Config.Waves)
		{
			TestTrue(TEXT("Sudden start"), Wave.bSuddenStart);
			TestTrue(TEXT("Cold drain not above 1"), Wave.Modifiers.ColdDrainMult <= 1.f);
		}
	}
	return true;
}

#endif

#if WITH_DEV_AUTOMATION_TESTS

// Combat lab (game designer 2026-11-01, docs/design/COMBAT_LAB.md): the "pressure" and "cheats" blocks.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLevelJsonPressureTest, "CodexTactics.LevelJson.Pressure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelJsonPressureTest::RunTest(const FString&)
{
	FLevelCombatConfig Config;
	FString Error;
	TestTrue(TEXT("A level without the block parses"), LevelJsonRules::ParseLevel(TEXT(R"({"level_id":"p"})"), TEXT("p"), Config, Error));
	TestFalse(TEXT("No block: pressure off"), Config.Pressure.bEnabled);
	TestFalse(TEXT("No block: no cheats"), Config.bCheatImmortal || Config.bCheatInfiniteAmmo);

	const FString Json = TEXT(R"({"level_id":"p", "cheats": {"immortal": true, "infinite_ammo": true},
		"pressure": {"interval_sec": 90, "first_event_sec": 5, "group_min": 1, "group_max": 3, "max_alive": 4, "patrol_share": 0.25, "seed": 77,
			"types": {"HOUND": 2, "frostbitten": 1, "SPITTER": 0, "CUTTER": 1, "MARKSMAN": 0.5},
			"wall_clearance_cm": 300, "min_squad_distance_cm": 2000, "max_squad_distance_cm": 4500,
			"patrol": {"points": 5, "radius_cm": 900, "wait_sec": 1.5},
			"ambush": {"min_squad_distance_cm": 1200, "max_squad_distance_cm": 2800, "watch_half_angle_deg": 50, "watch_range_cm": 4000, "strike_range_cm": 1500,
				"spots": {"wall_standoff_cm": 200, "end_inset_cm": 80, "doorway_max_cm": 500, "authored_bonus": 0.7},
				"points": [[100, 200], [-300.5, 400]],
				"coordinated": {"share": 0.5, "bodies_min": 3, "bodies_max": 5, "max_alive": 7, "assault_min_sec": 2, "assault_max_sec": 1, "min_ranged": 2},
				"relocate": {"enabled": false, "after_sec": 20, "behind_angle_deg": 120, "far_cm": 4000, "min_ahead_cos": 0.3}},
			"recycle": {"distance_cm": 7000, "after_sec": 45}}})");
	TestTrue(TEXT("Parses"), LevelJsonRules::ParseLevel(Json, TEXT("p"), Config, Error));
	const FCombatPressureConfig& P = Config.Pressure;
	TestTrue(TEXT("Enabled by default when present"), P.bEnabled);
	TestTrue(TEXT("Cheats on"), Config.bCheatImmortal && Config.bCheatInfiniteAmmo);
	TestEqual(TEXT("interval"), P.IntervalSeconds, 90.f);
	TestEqual(TEXT("first event"), P.FirstEventSeconds, 5.f);
	TestEqual(TEXT("group"), P.GroupMax, 3);
	TestEqual(TEXT("max alive"), P.MaxAlive, 4);
	TestEqual(TEXT("patrol share"), P.PatrolShare, 0.25f);
	TestEqual(TEXT("seed"), P.Seed, 77);
	TestEqual(TEXT("4 positive weights (SPITTER 0 dropped)"), P.TypeWeights.Num(), 4);
	TestEqual(TEXT("clearance"), P.WallClearanceCm, 300.f);
	TestEqual(TEXT("squad band"), P.MaxSquadDistanceCm, 4500.f);
	TestEqual(TEXT("patrol points"), P.PatrolPoints, 5);
	TestEqual(TEXT("patrol wait"), P.PatrolWaitSeconds, 1.5f);
	TestEqual(TEXT("ambush band"), P.AmbushMinDistanceCm, 1200.f);
	TestEqual(TEXT("watch half angle"), P.Watch.WatchHalfAngleDeg, 50.f);
	TestEqual(TEXT("strike range"), P.Watch.StrikeRangeCm, 1500.f);
	TestEqual(TEXT("recycle"), P.RecycleAfterSeconds, 45.f);
	// Answers 2026-11-01: hiding spots, authored points, coordinated plans, relocation.
	TestEqual(TEXT("spots: standoff"), P.Spots.WallStandoffCm, 200.f);
	TestEqual(TEXT("spots: inset"), P.Spots.EndInsetCm, 80.f);
	TestEqual(TEXT("spots: doorway max"), P.Spots.DoorwayMaxCm, 500.f);
	TestEqual(TEXT("spots: authored bonus"), P.Spots.AuthoredBonus, 0.7f);
	TestEqual(TEXT("spots: untouched keys keep the default"), P.Spots.DoorwayMinCm, FAmbushSpotParams().DoorwayMinCm);
	TestEqual(TEXT("2 authored points"), P.AuthoredPoints.Num(), 2);
	TestTrue(TEXT("... [x, y]"), P.AuthoredPoints.Num() == 2 && P.AuthoredPoints[1].Equals(FVector2D(-300.5, 400.0)));
	TestEqual(TEXT("coordinated share"), P.Coordinated.Share, 0.5f);
	TestEqual(TEXT("coordinated: never fewer than 4 bodies"), P.Coordinated.BodiesMin, 4);
	TestEqual(TEXT("coordinated: bodies max >= min"), P.Coordinated.BodiesMax, 5);
	TestEqual(TEXT("coordinated: own cap"), P.Coordinated.MaxAlive, 7);
	TestEqual(TEXT("coordinated: assault max >= min"), P.Coordinated.AssaultMaxSeconds, 2.f);
	TestEqual(TEXT("coordinated: min ranged"), P.Coordinated.MinRanged, 2);
	TestFalse(TEXT("relocate: off"), P.Relocate.bEnabled);
	TestEqual(TEXT("relocate: after"), P.Relocate.AfterSeconds, 20.f);
	TestEqual(TEXT("relocate: behind"), P.Relocate.BehindAngleDeg, 120.f);
	TestEqual(TEXT("relocate: ahead cos"), P.Relocate.MinAheadCos, 0.3f);
	TestFalse(TEXT("A malformed ambush point is refused"), LevelJsonRules::ParseLevel(TEXT(R"({"level_id":"p","pressure":{"ambush":{"points":[[1]]}}})"), TEXT("p"), Config, Error));
	TestTrue(TEXT("... the error names it"), Error.Contains(TEXT("ambush.points")));

	FCombatPressureConfig Clamped;
	FJsonObject Wild;
	Wild.SetNumberField(TEXT("interval_sec"), 1.0);
	Wild.SetNumberField(TEXT("group_min"), 0.0);
	Wild.SetNumberField(TEXT("group_max"), -3.0);
	Wild.SetNumberField(TEXT("patrol_share"), 7.0);
	Wild.SetBoolField(TEXT("enabled"), false);
	TestTrue(TEXT("Wild values parse"), LevelJsonRules::ParsePressureSettings(Wild, Clamped));
	TestFalse(TEXT("enabled: false"), Clamped.bEnabled);
	TestEqual(TEXT("interval >= 5 s"), Clamped.IntervalSeconds, 5.f);
	TestEqual(TEXT("group_min >= 1"), Clamped.GroupMin, 1);
	TestEqual(TEXT("group_max >= group_min"), Clamped.GroupMax, 1);
	TestEqual(TEXT("share <= 1"), Clamped.PatrolShare, 1.f);

	TestFalse(TEXT("A Brute in the types is refused"), LevelJsonRules::ParseLevel(TEXT(R"({"level_id":"p","pressure":{"types":{"BRUTE":1}}})"), TEXT("p"), Config, Error));
	TestTrue(TEXT("... the error names it"), Error.Contains(TEXT("BRUTE")));
	TestFalse(TEXT("An unknown type is refused"), LevelJsonRules::ParseLevel(TEXT(R"({"level_id":"p","pressure":{"types":{"DRAGON":1}}})"), TEXT("p"), Config, Error));

	// The combat lab's own file: both cheats on, the designer's numbers.
	FLevelCombatConfig Lab;
	if (TestTrue(TEXT("L_FormationLabCombat.json parses"), LevelJsonRules::LoadLevel(TEXT("L_FormationLabCombat.json"), Lab, Error)))
	{
		TestTrue(TEXT("Lab: pressure on"), Lab.Pressure.bEnabled);
		TestEqual(TEXT("Lab: every 2 minutes"), Lab.Pressure.IntervalSeconds, 120.f);
		TestEqual(TEXT("Lab: groups 1-3"), Lab.Pressure.GroupMin * 10 + Lab.Pressure.GroupMax, 13);
		TestEqual(TEXT("Lab: at most 4 alive"), Lab.Pressure.MaxAlive, 4);
		TestEqual(TEXT("Lab: the first group at 30 s (answer 6)"), Lab.Pressure.FirstEventSeconds, 30.f);
		TestTrue(TEXT("Lab: authored ambush points (answer 1)"), Lab.Pressure.AuthoredPoints.Num() >= 4);
		TestTrue(TEXT("Lab: coordinated plans with their own cap above max_alive (answer 2)"), Lab.Pressure.Coordinated.Share > 0.f
			&& Lab.Pressure.Coordinated.MaxAlive >= Lab.Pressure.Coordinated.BodiesMax && Lab.Pressure.Coordinated.MaxAlive > Lab.Pressure.MaxAlive);
		TestTrue(TEXT("Lab: relocation on (answer 3)"), Lab.Pressure.Relocate.bEnabled);
		TestEqual(TEXT("Lab: the watch stays +-45 deg (answer 4)"), Lab.Pressure.Watch.WatchHalfAngleDeg, 45.f);
		TestTrue(TEXT("Lab: immortal + infinite ammo by default"), Lab.bCheatImmortal && Lab.bCheatInfiniteAmmo);
		TestTrue(TEXT("Lab: fights start on contact as skirmishes"), Lab.CombatStart == ECombatStartMode::Ambush && Lab.bAmbushSkirmish);
		TestEqual(TEXT("Lab: no cold"), Lab.ColdRateMult, 0.f);
		TestFalse(TEXT("Lab: no intro briefing"), Lab.bIntroDialogue);
		for (const FPressureTypeWeight& Weight : Lab.Pressure.TypeWeights)
		{
			TestTrue(TEXT("Lab: only the allowed types"), CombatPressureRules::IsAllowedType(Weight.Type));
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
