#include "Misc/AutomationTest.h"
#include "../GodotBalanceFixture.h"
#include "Characters/ProgressionRules.h"
#include "Data/GodotBalanceAsset.h"

#if WITH_DEV_AUTOMATION_TESTS

// Godot Scenes/movements/player.gd parity: level thresholds, EXP overflow, level cap, stat point bounds; kill rewards
// from the Godot balance fixture (enemy_base.gd / enemy_cutter.gd).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionRulesTest, "CodexTactics.Characters.Progression.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProgressionRulesTest::RunTest(const FString&)
{
	TestEqual(TEXT("Level 1 needs 250"), ProgressionRules::NextLevelExp(1), 250);
	TestEqual(TEXT("Level 9 needs 2250"), ProgressionRules::NextLevelExp(9), 2250);
	TestEqual(TEXT("Level cap shows 2500"), ProgressionRules::NextLevelExp(10), 2500);

	int32 Level = 1;
	int32 Exp = 0;
	TestEqual(TEXT("240 EXP: no level"), ProgressionRules::AddExp(Level, Exp, 240), 0);
	TestTrue(TEXT("240 EXP kept"), Level == 1 && Exp == 240);
	TestEqual(TEXT("+20: level 2"), ProgressionRules::AddExp(Level, Exp, 20), 1);
	TestTrue(TEXT("Overflow carried: 10 EXP at level 2"), Level == 2 && Exp == 10);
	TestEqual(TEXT("+1240: levels 3 and 4 (500 + 750)"), ProgressionRules::AddExp(Level, Exp, 1240), 2);
	TestTrue(TEXT("Level 4 with 0 EXP"), Level == 4 && Exp == 0);
	Level = 9;
	Exp = 0;
	TestEqual(TEXT("Huge reward stops at the cap"), ProgressionRules::AddExp(Level, Exp, 100000), 1);
	TestTrue(TEXT("Level 10, overflow kept"), Level == 10 && Exp == 100000 - 2250);
	TestEqual(TEXT("Nothing more at the cap"), ProgressionRules::AddExp(Level, Exp, 50), 0);
	TestEqual(TEXT("Cap EXP untouched"), Exp, 100000 - 2250);

	TestTrue(TEXT("HP 195 + 5 = cap"), ProgressionRules::CanIncrease(EProgressStat::Health, 195.f, 1));
	TestFalse(TEXT("HP 196 + 5 over the cap"), ProgressionRules::CanIncrease(EProgressStat::Health, 196.f, 1));
	TestFalse(TEXT("No free point"), ProgressionRules::CanIncrease(EProgressStat::Luck, 25.f, 0));
	TestTrue(TEXT("Luck 59 -> 60"), ProgressionRules::CanIncrease(EProgressStat::Luck, 59.f, 3));
	TestFalse(TEXT("Luck 60 capped"), ProgressionRules::CanIncrease(EProgressStat::Luck, 60.f, 3));
	TestFalse(TEXT("Accuracy 100 capped"), ProgressionRules::CanIncrease(EProgressStat::Accuracy, 100.f, 3));
	TestFalse(TEXT("Fortitude 50 capped"), ProgressionRules::CanIncrease(EProgressStat::Fortitude, 50.f, 3));
	TestFalse(TEXT("No step below the base"), ProgressionRules::CanDecrease(EProgressStat::Luck, 25.f, 25.f));
	TestTrue(TEXT("Step back to the base"), ProgressionRules::CanDecrease(EProgressStat::Health, 135.f, 130.f));
	TestFalse(TEXT("HP 134 cannot drop to 129"), ProgressionRules::CanDecrease(EProgressStat::Health, 134.f, 130.f));

	TestEqual(TEXT("Fortitude 15: -22 %"), ProgressionRules::FortitudeCutPercent(15.f), 22);
	TestEqual(TEXT("Fortitude 50: -50 %"), ProgressionRules::FortitudeCutPercent(50.f), 50);

	TestEqual(TEXT("Default hound reward"), ProgressionRules::KillReward(EEnemyArchetype::FrostHound, nullptr), 9);
	TestEqual(TEXT("Cutter 16"), ProgressionRules::KillReward(EEnemyArchetype::Cutter, nullptr), 16);
	const UGodotBalanceAsset* Config = GodotBalanceFixture::MakeGameBalanceConfig();
	if (!TestNotNull(TEXT("DA_GameBalanceConfig"), Config))
	{
		return false;
	}
	// game_balance_config.tres: hound 15, spitter 20, brute 35; frostbitten keeps the .gd default 7.
	TestEqual(TEXT("Hound 15"), ProgressionRules::KillReward(EEnemyArchetype::FrostHound, Config), 15);
	TestEqual(TEXT("Spitter 20"), ProgressionRules::KillReward(EEnemyArchetype::Spitter, Config), 20);
	TestEqual(TEXT("Brute 35"), ProgressionRules::KillReward(EEnemyArchetype::Brute, Config), 35);
	TestEqual(TEXT("Frostbitten 7"), ProgressionRules::KillReward(EEnemyArchetype::Frostbitten, Config), 7);
	TestEqual(TEXT("Wave clear 25 (game_balance_config.tres)"), ProgressionRules::WaveClearReward(Config), 25);
	TestEqual(TEXT("Wave clear default 40"), ProgressionRules::WaveClearReward(nullptr), 40);
	TestEqual(TEXT("Cutter stays 16"), ProgressionRules::KillReward(EEnemyArchetype::Cutter, Config), 16);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
