#include "Misc/AutomationTest.h"
#include "../GodotBalanceFixture.h"
#include "Characters/PanicRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Godot Scripts/components/panic_component.gd _process_stress / on_damage_taken / on_low_ammo / _attempt_trigger_panic.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPanicStressRulesTest, "CodexTactics.Panic.StressGrowthAndJolts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPanicStressRulesTest::RunTest(const FString&)
{
	FPanicConfig Config; // Godot exports: hp 0.35, cold 0.7, ammo 5, monsters 8 m, recovery 5 / s
	// Healthy, warm, full ammo, nobody near: no growth, the stress calms down.
	TestEqual(TEXT("No threats"), PanicRules::GetStressGrowth(Config, 1.f, 0.f, 60, -1.f, 0.f), 0.f);
	TestEqual(TEXT("Calms 5 / s"), PanicRules::StepStress(Config, 50.f, 0.f, 2.f), 40.f, 0.001f);
	// Half the HP threshold: 20 * 0.5 = 10; cold 85 %: 25 * 0.5 = 12.5; 0 ammo: 10; enemy at 4 m: 15 * 0.5 = 7.5.
	TestEqual(TEXT("Wound"), PanicRules::GetStressGrowth(Config, 0.175f, 0.f, 60, -1.f, 0.f), 10.f, 0.001f);
	TestEqual(TEXT("Cold"), PanicRules::GetStressGrowth(Config, 1.f, 0.85f, 60, -1.f, 0.f), 12.5f, 0.001f);
	TestEqual(TEXT("No ammo"), PanicRules::GetStressGrowth(Config, 1.f, 0.f, 0, -1.f, 0.f), 10.f, 0.001f);
	TestEqual(TEXT("Monster"), PanicRules::GetStressGrowth(Config, 1.f, 0.f, 60, 4.f, 0.f), 7.5f, 0.001f);
	// Fortitude 10 cuts 25 %, capped at 65 %.
	TestEqual(TEXT("Fortitude 10"), PanicRules::GetStressGrowth(Config, 1.f, 0.f, 60, 4.f, 10.f), 5.625f, 0.001f);
	TestEqual(TEXT("Fortitude cap"), PanicRules::GetStressGrowth(Config, 1.f, 0.f, 60, 4.f, 100.f), 2.625f, 0.001f);
	TestEqual(TEXT("Grows, clamped at 100"), PanicRules::StepStress(Config, 98.f, 10.f, 1.f), 100.f);
	// A hit: damage x 1.5, fortitude cut 2 % a point (max 60 %), clamped to [6, 35].
	TestEqual(TEXT("Hit 10, fortitude 15"), PanicRules::GetDamageStress(Config, 10.f, 15.f), 10.5f, 0.001f);
	TestEqual(TEXT("Small hit -> 6"), PanicRules::GetDamageStress(Config, 1.f, 0.f), 6.f, 0.001f);
	TestEqual(TEXT("Big hit -> 35"), PanicRules::GetDamageStress(Config, 100.f, 0.f), 35.f, 0.001f);
	TestEqual(TEXT("Low ammo, fortitude 15"), PanicRules::GetLowAmmoStress(Config, 15.f), 8.4f, 0.001f);
	Config.StressGainMultiplier = 0.5f;
	TestEqual(TEXT("Personal multiplier"), PanicRules::GetDamageStress(Config, 100.f, 0.f), 17.5f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPanicTriggerRulesTest, "CodexTactics.Panic.SquadLimitAndLeader",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPanicTriggerRulesTest::RunTest(const FString&)
{
	FPanicConfig Config;
	Config.MaxPanickedMembers = 1; // user decision 2026-10-01 (Godot 2)
	TestTrue(TEXT("Nobody panics yet"), PanicRules::GetTriggerVerdict(Config, 0, false) == PanicRules::ETriggerVerdict::Panic);
	TestTrue(TEXT("Limit 1 reached"), PanicRules::GetTriggerVerdict(Config, 1, false) == PanicRules::ETriggerVerdict::HoldOnLimit);
	Config.MaxPanickedMembers = 2;
	TestTrue(TEXT("Leader holds while others panic"), PanicRules::GetTriggerVerdict(Config, 1, true) == PanicRules::ETriggerVerdict::HoldOnLeader);
	TestTrue(TEXT("A second member may panic under limit 2"), PanicRules::GetTriggerVerdict(Config, 1, false) == PanicRules::ETriggerVerdict::Panic);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPanicBalanceRulesTest, "CodexTactics.Panic.ConfigFromBalance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPanicBalanceRulesTest::RunTest(const FString&)
{
	// Godot apply_balance_config + apply_soldier_config with game_balance_config.gd defaults (+ .tres overrides).
	const UGameBalanceConfig* Godot = GodotBalanceFixture::MakeGameBalanceConfig();
	const FPanicConfig General = PanicRules::ConfigFromBalance(Godot, FString());
	TestEqual(TEXT("General max panicked 2"), General.MaxPanickedMembers, 2);
	TestEqual(TEXT(".tres flee radius 25"), General.MaxFleeRadius, 25.f, 0.001f);
	const FPanicConfig Engineer = PanicRules::ConfigFromBalance(Godot, TEXT("engineer"));
	TestEqual(TEXT("Engineer stress x1.1"), Engineer.StressGainMultiplier, 1.1f, 0.001f);
	TestEqual(TEXT("Engineer recovery 4.5"), Engineer.StressRecoveryRate, 4.5f, 0.001f);
	TestEqual(TEXT("Engineer hp 0.40"), Engineer.HpThreshold, 0.4f, 0.001f);
	const FPanicConfig Susanin = PanicRules::ConfigFromBalance(Godot, TEXT("susanin"));
	TestEqual(TEXT("Susanin stress (.tres 0.1)"), Susanin.StressGainMultiplier, 0.1f, 0.001f);
	TestEqual(TEXT("Susanin recovery mult 1.5"), Susanin.StressRecoveryMultiplier, 1.5f, 0.001f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
