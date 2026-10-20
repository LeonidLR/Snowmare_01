#include "Misc/AutomationTest.h"
#include "Characters/EnemyAnimRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Game designer 2026-10-20 (Military Zombie): per-instance clip / look variants, idle breaks (fidgets, the devour activity) and the
// stand-still lock of the alert roar / the spawn entrance. UE-only rules (no Godot reference).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyAnimVariantSeedTest, "CodexTactics.Characters.EnemyAnim.VariantSeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyAnimVariantSeedTest::RunTest(const FString&)
{
	const FVector At(1200.f, -340.f, 92.f);
	const uint32 Seed = EnemyAnimRules::MakeVariantSeed(TEXT("BP_Enemy_MilitaryZombie_C_0"), At);
	TestEqual(TEXT("same name + spawn point -> same seed (deterministic per spawn)"), EnemyAnimRules::MakeVariantSeed(TEXT("BP_Enemy_MilitaryZombie_C_0"), At), Seed);
	TestEqual(TEXT("sub-10 cm jitter keeps the seed"), EnemyAnimRules::MakeVariantSeed(TEXT("BP_Enemy_MilitaryZombie_C_0"), At + FVector(2.f, -1.f, 0.f)), Seed);
	TestNotEqual(TEXT("another actor -> another seed"), EnemyAnimRules::MakeVariantSeed(TEXT("BP_Enemy_MilitaryZombie_C_1"), At), Seed);
	TestNotEqual(TEXT("another spawn point -> another seed"), EnemyAnimRules::MakeVariantSeed(TEXT("BP_Enemy_MilitaryZombie_C_0"), At + FVector(300.f, 0.f, 0.f)), Seed);
	TestEqual(TEXT("override pins the seed"), EnemyAnimRules::MakeVariantSeed(TEXT("X"), At, 7), 7u);

	TestEqual(TEXT("no variants -> -1"), EnemyAnimRules::PickVariantIndex(Seed, 1, 0), -1);
	TestEqual(TEXT("one variant -> 0"), EnemyAnimRules::PickVariantIndex(Seed, 1, 1), 0);
	TestEqual(TEXT("deterministic"), EnemyAnimRules::PickVariantIndex(Seed, 3, 4), EnemyAnimRules::PickVariantIndex(Seed, 3, 4));
	// Spread: 400 consecutive seeds hit every one of 4 / 3 variants, none above ~2x its share.
	for (const int32 Count : { 3, 4 })
	{
		TArray<int32> Hits;
		Hits.SetNumZeroed(Count);
		for (uint32 S = 0; S < 400; ++S)
		{
			const int32 Index = EnemyAnimRules::PickVariantIndex(S, 2, Count);
			if (TestTrue(TEXT("index in range"), Index >= 0 && Index < Count))
			{
				++Hits[Index];
			}
		}
		for (int32 Index = 0; Index < Count; ++Index)
		{
			TestTrue(FString::Printf(TEXT("%d variants: variant %d picked %d / 400 times"), Count, Index, Hits[Index]),
				Hits[Index] > 400 / Count / 2 && Hits[Index] < 2 * 400 / Count);
		}
	}
	int32 SameAcrossSalts = 0;
	for (uint32 S = 0; S < 200; ++S)
	{
		SameAcrossSalts += EnemyAnimRules::PickVariantIndex(S, 1, 4) == EnemyAnimRules::PickVariantIndex(S, 2, 4) ? 1 : 0;
	}
	TestTrue(FString::Printf(TEXT("salts decorrelate the lists (%d / 200 equal, ~50 expected)"), SameAcrossSalts), SameAcrossSalts < 100);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyAnimIdleBreakTest, "CodexTactics.Characters.EnemyAnim.IdleBreak",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyAnimIdleBreakTest::RunTest(const FString&)
{
	TestTrue(TEXT("standing calm"), EnemyAnimRules::IsCalmIdle(false, false, false, false, false));
	TestFalse(TEXT("moving"), EnemyAnimRules::IsCalmIdle(true, false, false, false, false));
	TestFalse(TEXT("dead"), EnemyAnimRules::IsCalmIdle(false, true, false, false, false));
	TestFalse(TEXT("knocked down"), EnemyAnimRules::IsCalmIdle(false, false, true, false, false));
	TestFalse(TEXT("an attack / hit / roar playing"), EnemyAnimRules::IsCalmIdle(false, false, false, true, false));
	TestFalse(TEXT("turn-based fight"), EnemyAnimRules::IsCalmIdle(false, false, false, false, true));

	using EnemyAnimRules::ChooseIdleBreak;
	TestTrue(TEXT("no clips -> none"), ChooseIdleBreak(0.1f, 0.5f, 0, 0, false) == EEnemyIdleBreak::None);
	TestTrue(TEXT("calm, roll under the chance -> devour"), ChooseIdleBreak(0.2f, 0.35f, 4, 1, false) == EEnemyIdleBreak::Activity);
	TestTrue(TEXT("calm, roll over the chance -> fidget"), ChooseIdleBreak(0.5f, 0.35f, 4, 1, false) == EEnemyIdleBreak::Fidget);
	TestTrue(TEXT("hunting the squad: never devours"), ChooseIdleBreak(0.0f, 1.f, 4, 1, true) == EEnemyIdleBreak::Fidget);
	TestTrue(TEXT("hunting, no fidgets -> none"), ChooseIdleBreak(0.0f, 1.f, 0, 1, true) == EEnemyIdleBreak::None);
	TestTrue(TEXT("calm, only the activity -> activity"), ChooseIdleBreak(0.9f, 0.1f, 0, 1, false) == EEnemyIdleBreak::Activity);

	using EnemyAnimRules::PickFidget;
	TestEqual(TEXT("no fidgets"), PickFidget(0.5f, 0, -1), -1);
	TestEqual(TEXT("one fidget even if it is the loop"), PickFidget(0.9f, 1, 0), 0);
	for (const float Roll : { 0.f, 0.3f, 0.6f, 0.99f, 1.f })
	{
		const int32 Index = PickFidget(Roll, 5, 2);
		TestTrue(FString::Printf(TEXT("roll %.2f -> %d: never the idle loop it plays (2), in range"), Roll, Index), Index != 2 && Index >= 0 && Index < 5);
	}
	TestEqual(TEXT("roll 0 with the loop at 0 -> 1"), PickFidget(0.f, 5, 0), 1);
	TestEqual(TEXT("roll 1 with the loop last -> the one before"), PickFidget(1.f, 5, 4), 3);
	TestEqual(TEXT("loop not among the fidgets: the whole range"), PickFidget(1.f, 5, -1), 4);

	TestEqual(TEXT("delay min"), EnemyAnimRules::NextIdleBreakDelay(0.f, 6.f, 12.f), 6.f);
	TestEqual(TEXT("delay max"), EnemyAnimRules::NextIdleBreakDelay(1.f, 6.f, 12.f), 12.f);
	TestEqual(TEXT("swapped range"), EnemyAnimRules::NextIdleBreakDelay(0.5f, 12.f, 6.f), 9.f);
	TestEqual(TEXT("floor 0.5 s"), EnemyAnimRules::NextIdleBreakDelay(0.f, 0.f, 0.f), 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyAnimPerformanceLockTest, "CodexTactics.Characters.EnemyAnim.PerformanceLock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyAnimPerformanceLockTest::RunTest(const FString&)
{
	TestEqual(TEXT("no clip -> no lock"), EnemyAnimRules::PerformanceLockSeconds(0.f, 1.f, 3.f), 0.f);
	TestEqual(TEXT("clip at rate 1"), EnemyAnimRules::PerformanceLockSeconds(2.4f, 1.f, 0.f), 2.4f);
	TestEqual(TEXT("faster rate -> shorter"), EnemyAnimRules::PerformanceLockSeconds(3.f, 1.5f, 0.f), 2.f);
	TestEqual(TEXT("capped"), EnemyAnimRules::PerformanceLockSeconds(6.f, 1.f, 3.5f), 3.5f);
	TestEqual(TEXT("rate floor 0.1"), EnemyAnimRules::PerformanceLockSeconds(1.f, 0.f, 0.f), 10.f);
	return true;
}

#endif
