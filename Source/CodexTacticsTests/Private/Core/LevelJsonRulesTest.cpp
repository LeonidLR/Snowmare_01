#include "Misc/AutomationTest.h"
#include "Core/MissionRules.h"
#include "Data/LevelJsonRules.h"
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
