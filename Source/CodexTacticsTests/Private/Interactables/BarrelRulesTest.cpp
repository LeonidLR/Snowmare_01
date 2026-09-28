#include "Misc/AutomationTest.h"
#include "Interactables/BarrelRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Fuel barrel parity with Godot Scenes/movements/interactable.gd (_interact_barrel, _process, extinguish_barrel).

#define BARREL_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Barrel." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

BARREL_TEST(FBarrelIgniteTest, "IgniteSpendsOneMatchOnce")
bool FBarrelIgniteTest::RunTest(const FString&)
{
	FBarrelBurnState Barrel;
	int32 Matches = 3;
	TestTrue(TEXT("Lights"), Barrel.TryIgnite(Matches, 35.f) == EBarrelIgniteResult::Ignited);
	TestEqual(TEXT("One match spent"), Matches, 2);
	TestTrue(TEXT("Burning"), Barrel.bBurning);
	TestEqual(TEXT("Burn time"), Barrel.TimeLeft, 35.f);
	TestTrue(TEXT("Second strike refused while burning"), Barrel.TryIgnite(Matches, 35.f) == EBarrelIgniteResult::AlreadyBurning);
	TestEqual(TEXT("No match spent on refusal"), Matches, 2);
	return true;
}

BARREL_TEST(FBarrelNoMatchesTest, "NoMatchesRefused")
bool FBarrelNoMatchesTest::RunTest(const FString&)
{
	FBarrelBurnState Barrel;
	int32 Matches = 0;
	TestTrue(TEXT("Refused"), Barrel.TryIgnite(Matches, 35.f) == EBarrelIgniteResult::NoMatches);
	TestFalse(TEXT("Not burning"), Barrel.bBurning);
	TestFalse(TEXT("Still fresh"), Barrel.bBurnt);
	return true;
}

BARREL_TEST(FBarrelBurnOutTest, "BurnsOutOnceAndFades")
bool FBarrelBurnOutTest::RunTest(const FString&)
{
	FBarrelBurnState Barrel;
	int32 Matches = 1;
	Barrel.TryIgnite(Matches, 35.f);
	TestFalse(TEXT("Still burning after 20 s"), Barrel.Tick(20.f));
	TestEqual(TEXT("Full fire before the last 7 s"), Barrel.GetFireStrength(7.f), 1.f);
	Barrel.Tick(11.5f);
	TestEqual(TEXT("Half strength 3.5 s before the end"), Barrel.GetFireStrength(7.f), 0.5f, 0.001f);
	TestTrue(TEXT("Goes out at 35 s"), Barrel.Tick(4.f));
	TestFalse(TEXT("Out"), Barrel.bBurning);
	TestEqual(TEXT("No fire"), Barrel.GetFireStrength(7.f), 0.f);
	int32 More = 5;
	TestTrue(TEXT("Burnt barrel cannot be relit"), Barrel.TryIgnite(More, 35.f) == EBarrelIgniteResult::BurntOut);
	TestEqual(TEXT("No match spent"), More, 5);
	return true;
}

#undef BARREL_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
