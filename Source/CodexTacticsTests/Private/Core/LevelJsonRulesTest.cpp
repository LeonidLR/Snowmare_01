#include "Misc/AutomationTest.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLevelJsonFilesMatchAssetsTest, "CodexTactics.LevelJson.FilesMatchImportedAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelJsonFilesMatchAssetsTest::RunTest(const FString&)
{
	// Every migrated level file parses and equals what import_levels.py put into DA_Level_<name> (the fallback).
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(LevelJsonRules::GetLevelsDirectory() / TEXT("*.json")), true, false);
	TestTrue(TEXT("Level files present"), Files.Num() >= 13);
	for (const FString& File : Files)
	{
		const FString Base = FPaths::GetBaseFilename(File);
		if (Base == TEXT("stage_template"))
		{
			continue;
		}
		FLevelCombatConfig Config;
		FString Error;
		if (!TestTrue(FString::Printf(TEXT("%s parses (%s)"), *File, *Error), LevelJsonRules::LoadLevel(File, Config, Error)))
		{
			continue;
		}
		const FString AssetPath = FString::Printf(TEXT("/Game/Data/Levels/DA_Level_%s.DA_Level_%s"), *Base, *Base);
		const ULevelConfigAsset* Asset = LoadObject<ULevelConfigAsset>(nullptr, *AssetPath);
		if (!Asset)
		{
			continue;
		}
		const FLevelCombatConfig& Old = Asset->Config;
		TestEqual(FString::Printf(TEXT("%s waves"), *Base), Config.Waves.Num(), Old.Waves.Num());
		TestEqual(FString::Printf(TEXT("%s prep"), *Base), Config.PrepPhaseDuration, Old.PrepPhaseDuration);
		TestEqual(FString::Printf(TEXT("%s rest"), *Base), Config.WaveRestDuration, Old.WaveRestDuration);
		for (int32 Index = 0; Index < FMath::Min(Config.Waves.Num(), Old.Waves.Num()); ++Index)
		{
			TestEqual(FString::Printf(TEXT("%s wave %d enemies"), *Base, Index + 1), Config.Waves[Index].GetTotalEnemyCount(),
				Old.Waves[Index].GetTotalEnemyCount());
			TestEqual(FString::Printf(TEXT("%s wave %d HP mult"), *Base, Index + 1), Config.Waves[Index].Modifiers.EnemyHpMult,
				Old.Waves[Index].Modifiers.EnemyHpMult);
		}
		TestEqual(FString::Printf(TEXT("%s loadout mode"), *Base), Config.SquadLoadout.SimulationMode, Old.SquadLoadout.SimulationMode);
	}
	return true;
}

#endif
