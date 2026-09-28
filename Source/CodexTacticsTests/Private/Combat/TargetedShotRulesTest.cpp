#include "Misc/AutomationTest.h"
#include "Combat/TargetedShotRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Ctrl + click mine shot parity with Godot Scenes/movements/player.gd calculate_mine_shot_hit_chance.

#define TARGETED_SHOT_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Combat.TargetedShot." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

TARGETED_SHOT_TEST(FMineShotChanceByStanceTest, "MineChanceByStanceAndDistance")
bool FMineShotChanceByStanceTest::RunTest(const FString&)
{
	using namespace TargetedShotRules;
	// Commander (accuracy 90), no cold.
	TestEqual(TEXT("Prone 3 m capped at 95"), ComputeMineShotChance(90.f, 0.f, EOperativeStance::Prone, 3.f).Chance, 95.f);
	TestEqual(TEXT("Standing 5 m: 90*0.6 - 5*4"), ComputeMineShotChance(90.f, 0.f, EOperativeStance::Standing, 5.f).Chance, 34.f);
	TestEqual(TEXT("Crouching 10 m, cold 60: (90-15)*1 - 10*1.8"),
		ComputeMineShotChance(90.f, 60.f, EOperativeStance::Crouching, 10.f).Chance, 57.f, 0.001f);
	TestEqual(TEXT("Engineer standing 12 m clamps to 0"), ComputeMineShotChance(75.f, 0.f, EOperativeStance::Standing, 12.f).Chance, 0.f);
	TestEqual(TEXT("Effective accuracy floor 10: 10*1.25 - 2"), ComputeMineShotChance(20.f, 100.f, EOperativeStance::Prone, 2.f).Chance, 10.5f, 0.001f);
	TestEqual(TEXT("Stance label"), ComputeMineShotChance(90.f, 0.f, EOperativeStance::Crouching, 1.f).StanceName.ToString(), FString(TEXT("Присев")));
	return true;
}

TARGETED_SHOT_TEST(FMineMissReasonTest, "MissReasonPriority")
bool FMineMissReasonTest::RunTest(const FString&)
{
	using namespace TargetedShotRules;
	TestEqual(TEXT("Standing far"), GetMineMissReason(EOperativeStance::Standing, 7.5f, 80.f).ToString(),
		FString(TEXT("стоя на таком расстоянии не попасть")));
	TestEqual(TEXT("Standing at 7 m falls through to cold"), GetMineMissReason(EOperativeStance::Standing, 7.f, 41.f).ToString(),
		FString(TEXT("руки дрожат от холода")));
	TestEqual(TEXT("Crouching far, cold 40 is not enough"), GetMineMissReason(EOperativeStance::Crouching, 12.f, 40.f).ToString(),
		FString(TEXT("пуля ушла в мерзлый снег")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
