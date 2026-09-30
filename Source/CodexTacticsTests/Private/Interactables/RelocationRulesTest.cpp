#include "Misc/AutomationTest.h"
#include "Interactables/RelocationRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Object relocation parity with Godot main.gd (can_relocate_objects_now, _get_relocate_radius_for_worker)
// and player.gd can_lift_objects.

#define RELOCATION_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Relocation." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

RELOCATION_TEST(FRelocationWhenTest, "AllowedOutsideLiveCombat")
bool FRelocationWhenTest::RunTest(const FString&)
{
	using namespace RelocationRules;
	TestTrue(TEXT("Exploration"), CanRelocateNow(ECodexGamePhase::Exploration, ECodexCombatMode::None));
	TestTrue(TEXT("Preparation"), CanRelocateNow(ECodexGamePhase::Preparation, ECodexCombatMode::None));
	TestTrue(TEXT("Tactical pause"), CanRelocateNow(ECodexGamePhase::WaveCombat, ECodexCombatMode::TacticalPause));
	TestFalse(TEXT("Live combat"), CanRelocateNow(ECodexGamePhase::WaveCombat, ECodexCombatMode::RealTime));
	TestFalse(TEXT("Turn-based combat"), CanRelocateNow(ECodexGamePhase::WaveCombat, ECodexCombatMode::TurnBased));
	TestTrue(TEXT("Leader alone in a camera zone (Godot is_zone_solo)"), CanRelocateNow(ECodexGamePhase::WaveCombat, ECodexCombatMode::RealTime, true));
	return true;
}

RELOCATION_TEST(FRelocationRadiusTest, "RadiusPerMode")
bool FRelocationRadiusTest::RunTest(const FString&)
{
	using namespace RelocationRules;
	TestEqual(TEXT("Pause: 12 m from the pause origin"),
		GetPlacementRadius(ECodexGamePhase::WaveCombat, ECodexCombatMode::TacticalPause, 1200.f, 1500.f), 1200.f);
	TestEqual(TEXT("Preparation: unlimited"), GetPlacementRadius(ECodexGamePhase::Preparation, ECodexCombatMode::None, 1200.f, 1500.f),
		UnlimitedRadius);
	TestEqual(TEXT("Exploration: placement radius 15 m"),
		GetPlacementRadius(ECodexGamePhase::Exploration, ECodexCombatMode::None, 1200.f, 1500.f), 1500.f);
	TestTrue(TEXT("Inside (2D)"), IsWithinRadius(FVector::ZeroVector, FVector(1199.f, 0.f, 500.f), 1200.f));
	TestFalse(TEXT("Outside"), IsWithinRadius(FVector::ZeroVector, FVector(900.f, 900.f, 0.f), 1200.f));
	return true;
}

RELOCATION_TEST(FRelocationLiftTest, "ColdAndWoundsBlockLifting")
bool FRelocationLiftTest::RunTest(const FString&)
{
	using namespace RelocationRules;
	TestTrue(TEXT("Fit"), GetLiftBlocker(79.f, 0.5f, 80.f, 0.5f) == ELiftBlocker::None);
	TestTrue(TEXT("80 % cold"), GetLiftBlocker(80.f, 1.f, 80.f, 0.5f) == ELiftBlocker::TooCold);
	TestTrue(TEXT("Below half health"), GetLiftBlocker(10.f, 0.49f, 80.f, 0.5f) == ELiftBlocker::Wounded);
	TestTrue(TEXT("Cold reported first"), GetLiftBlocker(95.f, 0.2f, 80.f, 0.5f) == ELiftBlocker::TooCold);
	return true;
}

#undef RELOCATION_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
