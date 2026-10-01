#include "Misc/AutomationTest.h"
#include "../GodotBalanceFixture.h"
#include "Characters/RageRules.h"
#include "Data/GodotBalanceAsset.h"

#if WITH_DEV_AUTOMATION_TESTS

// Godot Scripts/components/rage_component.gd parity: chance decay, entry conditions, general / per-role balance keys.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRageRulesTest, "CodexTactics.Characters.Rage.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRageRulesTest::RunTest(const FString&)
{
	const FRageConfig Defaults = RageRules::ConfigFromBalance(nullptr, TEXT("commander"));
	TestTrue(TEXT("Component defaults"), Defaults.RequiredCrits == 2 && Defaults.BaseChance == 0.8f && Defaults.Duration == 8.5f
		&& Defaults.FireRateMultiplier == 0.45f && Defaults.DamageMultiplier == 1.3f && Defaults.bInfiniteAmmo);

	TestEqual(TEXT("Chance at the start"), RageRules::GetChance(Defaults, 0.f), 0.8f, 0.0001f);
	TestEqual(TEXT("One minute of combat: -10 %"), RageRules::GetChance(Defaults, 60.f), 0.7f, 0.0001f);
	TestEqual(TEXT("Long fight: floor 15 %"), RageRules::GetChance(Defaults, 6000.f), 0.15f, 0.0001f);

	TestTrue(TEXT("Healthy, loaded, lucky"), RageRules::CanEnterRage(Defaults, 0.7f, 10, 0.5f, 0.8f));
	TestFalse(TEXT("Too hurt"), RageRules::CanEnterRage(Defaults, 0.6f, 10, 0.5f, 0.8f));
	TestFalse(TEXT("Too few rounds"), RageRules::CanEnterRage(Defaults, 0.7f, 5, 0.5f, 0.8f));
	TestFalse(TEXT("Roll above the chance"), RageRules::CanEnterRage(Defaults, 0.7f, 10, 0.81f, 0.8f));

	const UGodotBalanceAsset* Config = GodotBalanceFixture::MakeGameBalanceConfig();
	if (!TestNotNull(TEXT("DA_GameBalanceConfig"), Config))
	{
		return false;
	}
	const FRageConfig Commander = RageRules::ConfigFromBalance(Config, TEXT("commander"));
	TestTrue(TEXT("Commander keys (game_balance_config.gd)"), FMath::IsNearlyEqual(Commander.BaseChance, 0.85f) && Commander.RequiredCrits == 2
		&& FMath::IsNearlyEqual(Commander.HighHealthThreshold, 0.6f) && FMath::IsNearlyEqual(Commander.Duration, 9.f)
		&& FMath::IsNearlyEqual(Commander.DamageMultiplier, 1.35f));
	const FRageConfig Engineer = RageRules::ConfigFromBalance(Config, TEXT("engineer"));
	TestTrue(TEXT("Engineer needs 3 crits"), Engineer.RequiredCrits == 3 && FMath::IsNearlyEqual(Engineer.Duration, 7.5f));
	const FRageConfig General = RageRules::ConfigFromBalance(Config, FString());
	TestTrue(TEXT("Recruit keeps the general keys"), FMath::IsNearlyEqual(General.BaseChance, 0.8f) && General.RequiredCrits == 2
		&& FMath::IsNearlyEqual(General.CritMemoryWindow, 25.f) && FMath::IsNearlyEqual(General.MinChance, 0.15f));
	return true;
}

#endif
