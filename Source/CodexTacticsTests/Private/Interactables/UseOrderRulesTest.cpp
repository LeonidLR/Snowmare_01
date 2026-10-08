#include "Misc/AutomationTest.h"
#include "Interactables/UseOrderRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Use orders in a fight (user decision 2026-10-08: ignite a barrel with matches in real time and in the tactical pause).

#define USE_ORDER_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Interactables.UseOrder." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

USE_ORDER_TEST(FUseOrderDispatchTest, "DispatchPerMode")
bool FUseOrderDispatchTest::RunTest(const FString&)
{
	using namespace UseOrderRules;
	const ECodexGamePhase Wave = ECodexGamePhase::WaveCombat;
	TestTrue(TEXT("Pause: queued"), GetDispatch(Wave, ECodexCombatMode::TacticalPause) == EUseOrderDispatch::Queue);
	TestTrue(TEXT("Real time: walk up and use"), GetDispatch(Wave, ECodexCombatMode::RealTime) == EUseOrderDispatch::Execute);
	TestTrue(TEXT("Turn-based: grid rules (immediate menu path)"), GetDispatch(Wave, ECodexCombatMode::TurnBased) == EUseOrderDispatch::Immediate);
	TestTrue(TEXT("Exploration: as before"), GetDispatch(ECodexGamePhase::Exploration, ECodexCombatMode::None) == EUseOrderDispatch::Immediate);
	TestTrue(TEXT("Preparation: as before"), GetDispatch(ECodexGamePhase::Preparation, ECodexCombatMode::None) == EUseOrderDispatch::Immediate);
	return true;
}

USE_ORDER_TEST(FUseOrderStepTest, "Steps")
bool FUseOrderStepTest::RunTest(const FString&)
{
	using namespace UseOrderRules;
	TestTrue(TEXT("In reach: use"), GetStep(140.f, 150.f, 0.f, 1.f) == EUseOrderStep::Use);
	TestTrue(TEXT("In reach wins over a late goal change"), GetStep(150.f, 150.f, 500.f, 1.f) == EUseOrderStep::Use);
	TestTrue(TEXT("Walking"), GetStep(600.f, 150.f, 20.f, 3.f) == EUseOrderStep::Walk);
	TestTrue(TEXT("Another move order: cancelled"), GetStep(600.f, 150.f, OtherOrderTolerance + 1.f, 3.f) == EUseOrderStep::Cancelled);
	TestTrue(TEXT("Never arrived: timed out"), GetStep(600.f, 150.f, 0.f, TimeoutSeconds + 0.1f) == EUseOrderStep::TimedOut);
	return true;
}

#undef USE_ORDER_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
