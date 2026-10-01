#include "Misc/AutomationTest.h"
#include "../GodotBalanceFixture.h"
#include "Data/GameBalanceConfig.h"
#include "Tactics/TurnBasedRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// The balance assets are UGameBalanceConfig (typed fields generated from Godot game_balance_config.gd, tuned in
// Unreal since 2026-10-01); GetNumber / SetNumber reach the fields by their Godot names. Godot value parity of the
// assets is reported by Scripts/Editor/import_balance.py; the rules are checked against the Godot fixtures.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBalanceImportParityTest, "CodexTactics.Data.BalanceImportParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBalanceImportParityTest::RunTest(const FString&)
{
	const UGameBalanceConfig* Balance = LoadObject<UGameBalanceConfig>(nullptr, TEXT("/Game/Data/Balance/DA_Balance.DA_Balance"));
	const UGameBalanceConfig* Config = LoadObject<UGameBalanceConfig>(nullptr, TEXT("/Game/Data/Balance/DA_GameBalanceConfig.DA_GameBalanceConfig"));
	if (!TestNotNull(TEXT("DA_Balance is a UGameBalanceConfig"), Balance) || !TestNotNull(TEXT("DA_GameBalanceConfig is a UGameBalanceConfig"), Config))
	{
		return false;
	}
	// GetNumber reads the typed fields (what the editor shows).
	TestEqual(TEXT("int field"), Config->GetInt(TEXT("tactical_squad_max_ap"), -1), Config->tactical_squad_max_ap);
	TestEqual(TEXT("float field"), Config->GetNumber(TEXT("commander_max_health"), -1.f), Config->commander_max_health);
	TestEqual(TEXT("bool field"), Config->GetNumber(TEXT("show_vision_cones"), -1.f), Config->show_vision_cones ? 1.f : 0.f);
	TestEqual(TEXT("unknown key -> fallback"), Config->GetNumber(TEXT("no_such_key"), 7.f), 7.f);

	// SetNumber writes fields (bools, ints rounded) and keeps keys without a field in Numbers.
	UGameBalanceConfig* Scratch = NewObject<UGameBalanceConfig>(GetTransientPackage());
	Scratch->SetNumber(TEXT("show_vision_cones"), 0.f);
	Scratch->SetNumber(TEXT("max_campaign_waves"), 2.4f);
	Scratch->SetNumber(TEXT("legacy_only_key"), 3.5f);
	TestFalse(TEXT("bool written"), Scratch->show_vision_cones);
	TestEqual(TEXT("int rounded"), Scratch->max_campaign_waves, 2);
	TestTrue(TEXT("field known"), Scratch->HasField(TEXT("max_campaign_waves")) && !Scratch->HasField(TEXT("legacy_only_key")));
	TestEqual(TEXT("extra key in Numbers"), Scratch->GetNumber(TEXT("legacy_only_key"), 0.f), 3.5f);

	// Godot balance.tres vs game_balance_config.tres differ on purpose (turn-based manager reads balance.tres).
	const FTurnBasedBalance TurnBased = TurnBasedRules::BalanceFromGodot(GodotBalanceFixture::MakeTurnBasedBalance());
	TestEqual(TEXT("Squad AP 8"), TurnBased.SquadMaxAP, 8);
	TestEqual(TEXT("Enemy AP 6"), TurnBased.EnemyMaxAP, 6);
	TestEqual(TEXT("Attack 3 AP"), TurnBased.AttackAPCost, 3);
	TestEqual(TEXT("Barrel 80"), TurnBased.BarrelDamage, 80.f);
	const UGameBalanceConfig* GodotConfig = GodotBalanceFixture::MakeGameBalanceConfig();
	TestEqual(TEXT("Config squad AP 3"), GodotConfig->GetInt(TEXT("tactical_squad_max_ap"), 0), 3);
	TestEqual(TEXT("Commander health 140"), GodotConfig->GetNumber(TEXT("commander_max_health"), 0.f), 140.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
