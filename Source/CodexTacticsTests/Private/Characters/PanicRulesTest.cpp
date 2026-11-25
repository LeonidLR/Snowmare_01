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

	// Trigger chance (default 0.2), per-operative cooldown (default 45 s), and fixed-seed determinism.
	TestEqual(TEXT("Default trigger chance 0.2"), Config.TriggerChance, 0.2f, 0.001f);
	TestEqual(TEXT("Default cooldown 45 s"), Config.PostPanicCooldownSeconds, 45.f, 0.001f);
	TestTrue(TEXT("Cooldown blocks even on 0 roll"), PanicRules::EvaluateTriggerAttempt(Config, 0, false, 10.f, 0.f) == PanicRules::ETriggerVerdict::HoldOnCooldown);
	TestTrue(TEXT("Roll 0.19 passes 0.2 chance"), PanicRules::EvaluateTriggerAttempt(Config, 0, false, 0.f, 0.19f) == PanicRules::ETriggerVerdict::Panic);
	TestTrue(TEXT("Roll 0.20 fails 0.2 chance"), PanicRules::EvaluateTriggerAttempt(Config, 0, false, 0.f, 0.20f) == PanicRules::ETriggerVerdict::HoldOnChance);
	TestTrue(TEXT("Limit takes priority over chance"), PanicRules::EvaluateTriggerAttempt(Config, 2, false, 0.f, 0.f) == PanicRules::ETriggerVerdict::HoldOnLimit);
	Config.TriggerChance = 0.f;
	TestTrue(TEXT("Zero chance always holds"), PanicRules::EvaluateTriggerAttempt(Config, 0, false, 0.f, 0.f) == PanicRules::ETriggerVerdict::HoldOnChance);
	Config.TriggerChance = 1.f;
	TestTrue(TEXT("Full chance always panics"), PanicRules::EvaluateTriggerAttempt(Config, 0, false, 0.f, 0.999f) == PanicRules::ETriggerVerdict::Panic);

	// Determinism with fixed seed.
	Config.TriggerChance = 0.2f;
	FRandomStream StreamA(42), StreamB(42);
	int32 HitsA = 0, HitsB = 0;
	for (int32 i = 0; i < 20; ++i)
	{
		HitsA += PanicRules::EvaluateTriggerAttempt(Config, 0, false, 0.f, StreamA.FRand()) == PanicRules::ETriggerVerdict::Panic ? 1 : 0;
		HitsB += PanicRules::EvaluateTriggerAttempt(Config, 0, false, 0.f, StreamB.FRand()) == PanicRules::ETriggerVerdict::Panic ? 1 : 0;
	}
	TestEqual(TEXT("Fixed seed deterministic count"), HitsA, HitsB);

	// Sector resupply low-ammo suppression.
	TestTrue(TEXT("Sector resupply suppresses low ammo"), PanicRules::IsSectorResupplyPending(true, true, 0, 5));
	TestFalse(TEXT("Inactive sector does not suppress"), PanicRules::IsSectorResupplyPending(false, true, 0, 5));
	TestFalse(TEXT("Non-resupply mode does not suppress"), PanicRules::IsSectorResupplyPending(true, false, 0, 5));
	TestFalse(TEXT("Above threshold does not suppress"), PanicRules::IsSectorResupplyPending(true, true, 6, 5));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPanicFleeSpeedRulesTest, "CodexTactics.Panic.FleeSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPanicFleeSpeedRulesTest::RunTest(const FString&)
{
	using namespace CriticalWoundRules;

	const FOperativeMovementConfig Movement; // WalkSpeed 220, RunSpeed 725, CrouchSpeed 125, ProneSpeedMultiplier 0.28, WoundedSpeedMultiplier 0.65
	const FCriticalWoundConfig Wound;        // HeavySpeedMultiplier 0.45

	const float NormalCrawl = Movement.WalkSpeed * Movement.ProneSpeedMultiplier; // 61.6 cm/s
	const float NormalCrouch = Movement.CrouchSpeed;                              // 125.0 cm/s
	const float WoundedRun = Movement.RunSpeed * Movement.WoundedSpeedMultiplier; // 471.25 cm/s

	// 1. Prone wounded <= crawl (crawl speed * 0.65 = 40.04 <= 61.6)
	const float ProneLight = PanicRules::PanicFleeSpeed(EOperativeStance::Prone, EWoundTier::Light, Movement, Wound);
	const float ProneHeavy = PanicRules::PanicFleeSpeed(EOperativeStance::Prone, EWoundTier::Heavy, Movement, Wound);
	TestTrue(TEXT("Prone light wounded <= crawl"), ProneLight <= NormalCrawl);
	TestEqual(TEXT("Prone light wounded exact"), ProneLight, NormalCrawl * Movement.WoundedSpeedMultiplier, 0.01f);
	TestTrue(TEXT("Prone heavy wounded <= crawl"), ProneHeavy <= NormalCrawl);

	// 2. Crouch <= crouch
	const float CrouchHealthy = PanicRules::PanicFleeSpeed(EOperativeStance::Crouching, EWoundTier::None, Movement, Wound);
	const float CrouchLight = PanicRules::PanicFleeSpeed(EOperativeStance::Crouching, EWoundTier::Light, Movement, Wound);
	TestTrue(TEXT("Crouch healthy <= crouch"), CrouchHealthy <= NormalCrouch);
	TestEqual(TEXT("Crouch healthy exact"), CrouchHealthy, NormalCrouch, 0.01f);
	TestTrue(TEXT("Crouch wounded <= crouch"), CrouchLight <= NormalCrouch);
	TestEqual(TEXT("Crouch wounded exact"), CrouchLight, NormalCrouch * Movement.WoundedSpeedMultiplier, 0.01f);

	// 3. Standing healthy = run
	const float StandingHealthy = PanicRules::PanicFleeSpeed(EOperativeStance::Standing, EWoundTier::None, Movement, Wound);
	TestEqual(TEXT("Standing healthy = run"), StandingHealthy, Movement.RunSpeed, 0.01f);

	// 4. Badly wounded standing <= wounded run
	const float StandingHeavy = PanicRules::PanicFleeSpeed(EOperativeStance::Standing, EWoundTier::Heavy, Movement, Wound);
	TestTrue(TEXT("Badly wounded standing <= wounded run"), StandingHeavy <= WoundedRun);
	TestEqual(TEXT("Badly wounded standing = heavy walk (99 cm/s)"), StandingHeavy, Movement.WalkSpeed * Wound.HeavySpeedMultiplier, 0.01f);

	// Cold multiplier scale
	const float ColdProne = PanicRules::PanicFleeSpeed(EOperativeStance::Prone, EWoundTier::Light, Movement, Wound, 0.8f);
	TestEqual(TEXT("Cold scaling"), ColdProne, ProneLight * 0.8f, 0.01f);

	// Critical wound is crawl only
	const float CriticalSpeed = PanicRules::PanicFleeSpeed(EOperativeStance::Standing, EWoundTier::Critical, Movement, Wound);
	TestEqual(TEXT("Critical wound = crawl"), CriticalSpeed, NormalCrawl * Movement.WoundedSpeedMultiplier, 0.01f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
