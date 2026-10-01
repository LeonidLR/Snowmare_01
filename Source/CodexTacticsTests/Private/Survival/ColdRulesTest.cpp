#include "Misc/AutomationTest.h"
#include "Survival/ColdRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Cold survival parity with Godot player.gd `_process_cold_system` / `_shoot_at_target` and balance.tres.

#define COLD_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Cold." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

COLD_TEST(FColdAccumulationTest, "AccumulationWithFortitudeStanceWind")
bool FColdAccumulationTest::RunTest(const FString&)
{
	const FColdConfig Config;
	FColdEnvironment Open;
	// Commander (fortitude 15 -> 12 % cut), standing, open ground: 1.0 * 0.88 %/s.
	TestEqual(TEXT("Commander 10 s"), ColdRules::StepCold(Config, 0.f, 10.f, Open, EOperativeStance::Standing, 15.f), 8.8f, 0.001f);
	// Engineer (25 -> 20 %), prone (0.6): 1.0 * 0.8 * 0.6 = 0.48 %/s.
	TestEqual(TEXT("Engineer prone 10 s"), ColdRules::StepCold(Config, 0.f, 10.f, Open, EOperativeStance::Prone, 25.f), 4.8f, 0.001f);
	// Fortitude cut is capped at 40 %.
	TestEqual(TEXT("Cut capped"), ColdRules::StepCold(Config, 0.f, 10.f, Open, EOperativeStance::Standing, 100.f), 6.f, 0.001f);
	FColdEnvironment High;
	High.bElevated = true;
	High.ZoneMultiplier = 2.5f;
	// Blizzard zone on elevated ground: 2.5 * 2 = x5.
	TestEqual(TEXT("Blizzard + wind"), ColdRules::StepCold(Config, 0.f, 1.f, High, EOperativeStance::Standing, 0.f), 5.f, 0.001f);
	TestEqual(TEXT("Clamped at 100"), ColdRules::StepCold(Config, 99.f, 10.f, Open, EOperativeStance::Standing, 0.f), 100.f);
	return true;
}

COLD_TEST(FColdWarmingTest, "WarmingNearHeatClosedZoneAndPreparation")
bool FColdWarmingTest::RunTest(const FString&)
{
	const FColdConfig Config;
	FColdEnvironment Warm;
	Warm.bWarm = true;
	// 8 %/s * (1 + 20 * 0.01) = 9.6 %/s.
	TestEqual(TEXT("Medic near heat 1 s"), ColdRules::StepCold(Config, 50.f, 1.f, Warm, EOperativeStance::Standing, 20.f), 40.4f, 0.001f);
	FColdEnvironment Prep;
	Prep.bPreparation = true;
	TestEqual(TEXT("Preparation warms"), ColdRules::StepCold(Config, 50.f, 1.f, Prep, EOperativeStance::Standing, 0.f), 42.f, 0.001f);
	FColdEnvironment Bunker;
	Bunker.ZoneMultiplier = 0.f;
	TestEqual(TEXT("Closed bunker warms"), ColdRules::StepCold(Config, 5.f, 1.f, Bunker, EOperativeStance::Standing, 0.f), 0.f);
	return true;
}

COLD_TEST(FColdSprintWarmupTest, "SprintWarmsUp")
bool FColdSprintWarmupTest::RunTest(const FString&)
{
	// Deviation from Godot (user decision 2026-10-01): a sprint in the open lowers the cold instead of raising it.
	const FColdConfig Config;
	FColdEnvironment Sprint;
	Sprint.bSprinting = true;
	// 1.5 %/s * (1 + 15 * 0.01) = 1.725 %/s.
	TestEqual(TEXT("Commander sprints 10 s"), ColdRules::StepCold(Config, 50.f, 10.f, Sprint, EOperativeStance::Standing, 15.f), 32.75f, 0.001f);
	TestEqual(TEXT("Not below 0"), ColdRules::StepCold(Config, 1.f, 10.f, Sprint, EOperativeStance::Standing, 0.f), 0.f);
	Sprint.ZoneMultiplier = 2.5f;
	Sprint.bElevated = true;
	TestTrue(TEXT("Warms up in a blizzard too"), ColdRules::StepCold(Config, 50.f, 1.f, Sprint, EOperativeStance::Standing, 0.f) < 50.f);
	FColdEnvironment Walk;
	TestTrue(TEXT("Walking still cools down"), ColdRules::StepCold(Config, 50.f, 1.f, Walk, EOperativeStance::Standing, 0.f) > 50.f);
	return true;
}

COLD_TEST(FColdTiersTest, "TiersSpeedAndActionPoints")
bool FColdTiersTest::RunTest(const FString&)
{
	using namespace ColdRules;
	TestEqual(TEXT("39.9 normal"), GetTier(39.9f), EColdTier::Normal);
	TestEqual(TEXT("40 chills"), GetTier(40.f), EColdTier::Chills);
	TestEqual(TEXT("70 freezing"), GetTier(70.f), EColdTier::Freezing);
	TestEqual(TEXT("90 hypothermia"), GetTier(90.f), EColdTier::Hypothermia);
	TestEqual(TEXT("100 frostbite"), GetTier(100.f), EColdTier::Frostbite);
	TestEqual(TEXT("Speed chills"), GetSpeedMultiplier(EColdTier::Chills), 0.7f);
	TestEqual(TEXT("Speed freezing"), GetSpeedMultiplier(EColdTier::Freezing), 0.45f);
	TestEqual(TEXT("Speed frostbite"), GetSpeedMultiplier(EColdTier::Frostbite), 0.25f);
	TestEqual(TEXT("AP normal"), GetMaxActionPoints(EColdTier::Normal), 5);
	TestEqual(TEXT("AP hypothermia"), GetMaxActionPoints(EColdTier::Hypothermia), 2);
	return true;
}

COLD_TEST(FColdWeaponTest, "MisfireAimPenaltyAndFreeze")
bool FColdWeaponTest::RunTest(const FString&)
{
	const FColdConfig Config;
	using namespace ColdRules;
	TestEqual(TEXT("No misfire below 60"), GetMisfireChance(Config, 59.f, false), 0.f);
	TestEqual(TEXT("Misfire 80 -> 15 %"), GetMisfireChance(Config, 80.f, false), 0.15f, 0.0001f);
	TestEqual(TEXT("Misfire 100 -> 30 %"), GetMisfireChance(Config, 100.f, false), 0.3f, 0.0001f);
	TestEqual(TEXT("No misfire near heat"), GetMisfireChance(Config, 100.f, true), 0.f);
	TestEqual(TEXT("Aim penalty 75 -> 15 %"), GetAimPenalty(Config, 75.f), 0.15f, 0.0001f);
	TestEqual(TEXT("No aim penalty at 50"), GetAimPenalty(Config, 50.f), 0.f);
	TestTrue(TEXT("Freezes at 90"), UpdateWeaponFrozen(Config, false, 90.f, false));
	TestFalse(TEXT("Not near heat"), UpdateWeaponFrozen(Config, false, 95.f, true));
	TestTrue(TEXT("Stays frozen at 86 (hysteresis)"), UpdateWeaponFrozen(Config, true, 86.f, false));
	TestFalse(TEXT("Thaws below 85"), UpdateWeaponFrozen(Config, true, 84.f, false));
	TestFalse(TEXT("Heat thaws"), UpdateWeaponFrozen(Config, true, 99.f, true));
	return true;
}

#undef COLD_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
