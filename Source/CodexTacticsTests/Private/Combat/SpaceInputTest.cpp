#include "Misc/AutomationTest.h"
#include "Characters/SquadFormation.h"
#include "Combat/SpaceInput.h"
#include "GameFlow/GameFlowStateMachine.h"

#if WITH_DEV_AUTOMATION_TESTS

// Sprint 03: Space tap / hold (Godot main.gd KEY_SPACE; hold limit from balance.tres, see TANDEM Q2/Q3).

#define COMBAT_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Combat." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

COMBAT_TEST(FSpaceTapTest, "Space.ReleaseBeforeHoldIsTap")
bool FSpaceTapTest::RunTest(const FString&)
{
	FSpaceInputTracker Space;
	Space.Press();
	TestEqual(TEXT("No hold yet"), Space.Tick(1.0f, 1.5f), ESpaceInputAction::None);
	TestEqual(TEXT("Release is a tap"), Space.Release(), ESpaceInputAction::Tap);
	TestEqual(TEXT("Release without press does nothing"), Space.Release(), ESpaceInputAction::None);
	return true;
}

COMBAT_TEST(FSpaceHoldTest, "Space.HoldFiresOnceAndSuppressesTap")
bool FSpaceHoldTest::RunTest(const FString&)
{
	FSpaceInputTracker Space;
	Space.Press();
	TestEqual(TEXT("Before limit"), Space.Tick(1.4f, 1.5f), ESpaceInputAction::None);
	TestEqual(TEXT("Limit reached"), Space.Tick(0.2f, 1.5f), ESpaceInputAction::Hold);
	TestEqual(TEXT("Fires once"), Space.Tick(5.f, 1.5f), ESpaceInputAction::None);
	TestEqual(TEXT("Release after hold is not a tap"), Space.Release(), ESpaceInputAction::None);
	return true;
}

COMBAT_TEST(FSpaceHoldPerPressTest, "Space.EachPressCanHoldAgain")
bool FSpaceHoldPerPressTest::RunTest(const FString&)
{
	FSpaceInputTracker Space;
	Space.Press();
	Space.Tick(2.f, 1.5f);
	Space.Release();
	Space.Press();
	TestEqual(TEXT("Second press holds again"), Space.Tick(1.6f, 1.5f), ESpaceInputAction::Hold);
	return true;
}

COMBAT_TEST(FPauseOrderClampTest, "TacticalPause.OrdersClampedToRadius")
bool FPauseOrderClampTest::RunTest(const FString&)
{
	using namespace SquadFormation;
	const FVector Origin(100.f, 100.f, 90.f);
	TestEqual(TEXT("Inside radius unchanged"), ClampToRadius2D(Origin, FVector(600.f, 100.f, 0.f), 1200.f), FVector(600.f, 100.f, 0.f));
	TestEqual(TEXT("Clamped to 12 m"), ClampToRadius2D(Origin, FVector(3100.f, 100.f, 0.f), 1200.f), FVector(1300.f, 100.f, 0.f));
	return true;
}

COMBAT_TEST(FFlowDefaultsTest, "Flow.GodotDefaultsForSpaceAndPause")
bool FFlowDefaultsTest::RunTest(const FString&)
{
	const FGameFlowConfig Config;
	TestEqual(TEXT("Hold 1.5 s (balance.tres)"), Config.TurnBasedHoldDuration, 1.5f);
	TestEqual(TEXT("Pause dilation 0.02 (main.gd)"), Config.TacticalPauseTimeDilation, 0.02f);
	TestEqual(TEXT("Encounter radius 15 m"), Config.TurnBasedEncounterRadius, 1500.f);
	TestEqual(TEXT("Pause order radius 12 m"), Config.PauseOrderRadius, 1200.f);
	return true;
}

#undef COMBAT_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
