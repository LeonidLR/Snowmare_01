#include "Misc/AutomationTest.h"
#include "Core/LevelFlowRules.h"
#include "Data/GodotBalanceAsset.h"
#include "Data/WaveConfigTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

// Godot main.gd _load_active_level_config / _ready fallback parity, and the imported level_01_outpost.json.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLevelFlowRulesApplyTest, "CodexTactics.Core.LevelFlow.ApplyLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelFlowRulesApplyTest::RunTest(const FString&)
{
	FGameFlowConfig Base;
	Base.TacticalPauseDuration = 12.f;

	FLevelCombatConfig Level;
	Level.PrepPhaseDuration = 45.f;
	Level.WaveRestDuration = 15.f;
	Level.Waves.SetNum(3);
	const FGameFlowConfig FromLevel = LevelFlowRules::ApplyLevel(Base, &Level, nullptr);
	TestEqual(TEXT("Level preparation"), FromLevel.PreparationDuration, 45.f);
	TestEqual(TEXT("Level rest"), FromLevel.WaveRestDuration, 15.f);
	TestEqual(TEXT("Waves = level waves"), FromLevel.TotalWaves, 3);
	TestEqual(TEXT("Other values kept"), FromLevel.TacticalPauseDuration, 12.f);

	UGodotBalanceAsset* Balance = NewObject<UGodotBalanceAsset>();
	Balance->Numbers.Add(TEXT("preparation_phase_duration"), 30.f);
	Balance->Numbers.Add(TEXT("max_campaign_waves"), 2.f);
	const FGameFlowConfig FromBalance = LevelFlowRules::ApplyLevel(Base, nullptr, Balance);
	TestEqual(TEXT("No level: balance preparation"), FromBalance.PreparationDuration, 30.f);
	TestEqual(TEXT("No level: max_campaign_waves"), FromBalance.TotalWaves, 2);
	TestEqual(TEXT("No level: rest keeps main.gd default"), FromBalance.WaveRestDuration, Base.WaveRestDuration);

	FLevelCombatConfig NoWaves;
	NoWaves.PrepPhaseDuration = 50.f;
	TestEqual(TEXT("Level without waves: balance wave count"), LevelFlowRules::ApplyLevel(Base, &NoWaves, Balance).TotalWaves, 2);
	TestEqual(TEXT("Nothing: base kept"), LevelFlowRules::ApplyLevel(Base, nullptr, nullptr).TotalWaves, Base.TotalWaves);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLevelImportParityTest, "CodexTactics.Core.LevelFlow.ImportParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelImportParityTest::RunTest(const FString&)
{
	const ULevelConfigAsset* Asset = LoadObject<ULevelConfigAsset>(nullptr,
		TEXT("/Game/Data/Levels/DA_Level_level_01_outpost.DA_Level_level_01_outpost"));
	if (!TestNotNull(TEXT("DA_Level_level_01_outpost"), Asset))
	{
		return false;
	}
	const FLevelCombatConfig& Config = Asset->Config;
	TestEqual(TEXT("Preparation 60"), Config.PrepPhaseDuration, 60.f);
	TestEqual(TEXT("Rest 20"), Config.WaveRestDuration, 20.f);
	if (!TestEqual(TEXT("3 waves"), Config.Waves.Num(), 3))
	{
		return false;
	}
	const int32 Expected[] = { 12, 20, 21 };
	const int32 ExpectedMax[] = { 8, 10, 12 };
	for (int32 Index = 0; Index < 3; ++Index)
	{
		int32 Total = 0;
		for (const FEnemySpawnEntry& Entry : Config.Waves[Index].Spawns)
		{
			Total += Entry.Count;
		}
		TestEqual(FString::Printf(TEXT("Wave %d enemies"), Index + 1), Total, Expected[Index]);
		TestEqual(FString::Printf(TEXT("Wave %d max simultaneous"), Index + 1), Config.Waves[Index].MaxSimultaneousEnemies, ExpectedMax[Index]);
	}
	TestEqual(TEXT("Wave 1 opens with cutters"), Config.Waves[0].Spawns[0].EnemyType, EEnemyArchetype::Cutter);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
