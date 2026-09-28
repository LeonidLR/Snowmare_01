#include "Misc/AutomationTest.h"
#include "Data/GodotBalanceAsset.h"
#include "Tactics/TurnBasedRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Imported balance assets (Scripts/Editor/import_balance.py) match Godot resources/balance.tres and
// resources/game_balance_config.tres — two files with different tactical values.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBalanceImportParityTest, "CodexTactics.Data.BalanceImportParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBalanceImportParityTest::RunTest(const FString&)
{
	const UGodotBalanceAsset* Balance = LoadObject<UGodotBalanceAsset>(nullptr, TEXT("/Game/Data/Balance/DA_Balance.DA_Balance"));
	const UGodotBalanceAsset* Config = LoadObject<UGodotBalanceAsset>(nullptr, TEXT("/Game/Data/Balance/DA_GameBalanceConfig.DA_GameBalanceConfig"));
	if (!TestNotNull(TEXT("DA_Balance"), Balance) || !TestNotNull(TEXT("DA_GameBalanceConfig"), Config))
	{
		return false;
	}
	// balance.tres (turn-based manager)
	const FTurnBasedBalance TurnBased = TurnBasedRules::BalanceFromGodot(Balance);
	TestEqual(TEXT("Squad AP 8"), TurnBased.SquadMaxAP, 8);
	TestEqual(TEXT("Enemy AP 6"), TurnBased.EnemyMaxAP, 6);
	TestEqual(TEXT("Attack 3 AP"), TurnBased.AttackAPCost, 3);
	TestEqual(TEXT("Barrel 80"), TurnBased.BarrelDamage, 80.f);
	TestEqual(TEXT("Step 0.52 s"), Balance->GetNumber(TEXT("tactical_step_duration"), 0.f), 0.52f, 0.0001f);
	// game_balance_config.tres differs on purpose
	TestEqual(TEXT("Config squad AP 3"), Config->GetInt(TEXT("tactical_squad_max_ap"), 0), 3);
	TestEqual(TEXT("Config enemy AP 4"), Config->GetInt(TEXT("tactical_enemy_max_ap"), 0), 4);
	TestEqual(TEXT("Commander health 140"), Config->GetNumber(TEXT("commander_max_health"), 0.f), 140.f);
	// a value only in game_balance_config.gd (default carried over)
	TestTrue(TEXT("Defaults imported"), Config->Numbers.Num() > 100);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
