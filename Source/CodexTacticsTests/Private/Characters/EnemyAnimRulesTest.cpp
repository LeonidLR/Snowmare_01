#include "Misc/AutomationTest.h"
#include "Characters/EnemyAnimRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Game designer 2026-10-20 (Military Zombie): per-instance clip / look variants, idle breaks (fidgets, the devour activity) and the
// stand-still lock of the alert roar / the spawn entrance. UE-only rules (no Godot reference).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyLookBodiesTest, "CodexTactics.Enemy.LookVariants.BodyAndSet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyLookBodiesTest::RunTest(const FString&)
{
	// User 2026-10-21: single-slot bodies carry their own colour sets; the set is one of the PICKED body's sets.
	int32 Body = 0;
	int32 Set = 0;
	EnemyAnimRules::PickLookBodyAndSet(123u, {}, Body, Set);
	TestTrue(TEXT("no bodies -> -1 / -1"), Body == -1 && Set == -1);
	EnemyAnimRules::PickLookBodyAndSet(123u, { 0 }, Body, Set);
	TestTrue(TEXT("one body without sets -> body 0, set -1"), Body == 0 && Set == -1);
	EnemyAnimRules::PickLookBodyAndSet(123u, { 5 }, Body, Set);
	TestTrue(TEXT("one body (Hound: 5 sets) -> body 0"), Body == 0 && Set >= 0 && Set < 5);

	// Military Zombie 3 x 3: the same picks as the old Meshes / MaterialSets path (a seed keeps its body and set).
	TArray<int32> BodiesHit;
	BodiesHit.SetNumZeroed(3);
	TSet<int32> Pairs;
	for (uint32 Seed = 0; Seed < 300; ++Seed)
	{
		EnemyAnimRules::PickLookBodyAndSet(Seed, { 3, 3, 3 }, Body, Set);
		if (!TestTrue(TEXT("3 x 3: indices in range"), Body >= 0 && Body < 3 && Set >= 0 && Set < 3))
		{
			return false;
		}
		TestEqual(TEXT("body = PickVariantIndex(seed, body salt, 3)"), Body, EnemyAnimRules::PickVariantIndex(Seed, EnemyAnimRules::LookBodySalt, 3));
		TestEqual(TEXT("set = PickVariantIndex(seed, set salt, 3)"), Set, EnemyAnimRules::PickVariantIndex(Seed, EnemyAnimRules::LookSetSalt, 3));
		++BodiesHit[Body];
		Pairs.Add(Body * 10 + Set);
	}
	TestEqual(TEXT("300 seeds reach all 9 body x set looks"), Pairs.Num(), 9);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		TestTrue(FString::Printf(TEXT("body %d picked %d / 300 times"), Index, BodiesHit[Index]), BodiesHit[Index] > 50 && BodiesHit[Index] < 150);
	}

	// Uneven bodies: the set index always fits the picked body's own set count.
	for (uint32 Seed = 0; Seed < 200; ++Seed)
	{
		EnemyAnimRules::PickLookBodyAndSet(Seed, { 4, 1, 0 }, Body, Set);
		const int32 Counts[] = { 4, 1, 0 };
		TestTrue(TEXT("set fits the picked body"), Counts[Body] == 0 ? Set == -1 : (Set >= 0 && Set < Counts[Body]));
	}
	int32 BodyAgain = 0;
	int32 SetAgain = 0;
	EnemyAnimRules::PickLookBodyAndSet(77u, { 3, 3, 3 }, Body, Set);
	EnemyAnimRules::PickLookBodyAndSet(77u, { 3, 3, 3 }, BodyAgain, SetAgain);
	TestTrue(TEXT("deterministic"), Body == BodyAgain && Set == SetAgain);
	return true;
}

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
