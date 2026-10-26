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
	// The OLD rule (Godot can_relocate_objects_now; Codex.Relocation.RealTimeCombat 0): never in the live fight.
	using namespace RelocationRules;
	TestTrue(TEXT("Exploration"), CanRelocateNow(ECodexGamePhase::Exploration, ECodexCombatMode::None, false, false));
	TestTrue(TEXT("Preparation"), CanRelocateNow(ECodexGamePhase::Preparation, ECodexCombatMode::None, false, false));
	TestTrue(TEXT("Tactical pause"), CanRelocateNow(ECodexGamePhase::WaveCombat, ECodexCombatMode::TacticalPause, false, false));
	TestFalse(TEXT("Live combat"), CanRelocateNow(ECodexGamePhase::WaveCombat, ECodexCombatMode::RealTime, false, false));
	TestFalse(TEXT("Turn-based combat"), CanRelocateNow(ECodexGamePhase::WaveCombat, ECodexCombatMode::TurnBased, false, false));
	TestTrue(TEXT("Leader alone in a camera zone (Godot is_zone_solo)"), CanRelocateNow(ECodexGamePhase::WaveCombat, ECodexCombatMode::RealTime, true, false));
	return true;
}

RELOCATION_TEST(FRelocationRealTimeCombatTest, "RealTimeCombat.AllowedExceptTurnBased")
bool FRelocationRealTimeCombatTest::RunTest(const FString&)
{
	// User decision 2026-10-26: "the tactical pause is an aid, it must not cancel everything else" - barricades / objects move in the
	// real-time fight too; only the turn-based grid (its own push) refuses.
	using namespace RelocationRules;
	const ECodexGamePhase Wave = ECodexGamePhase::WaveCombat;
	TestTrue(TEXT("Exploration"), CanRelocateNow(ECodexGamePhase::Exploration, ECodexCombatMode::None));
	TestTrue(TEXT("Tactical pause"), CanRelocateNow(Wave, ECodexCombatMode::TacticalPause));
	TestTrue(TEXT("Real-time fight: allowed now"), CanRelocateNow(Wave, ECodexCombatMode::RealTime));
	TestFalse(TEXT("Turn-based combat: still refused"), CanRelocateNow(Wave, ECodexCombatMode::TurnBased));
	TestTrue(TEXT("Turn-based, zone solo"), CanRelocateNow(Wave, ECodexCombatMode::TurnBased, true));
	TestFalse(TEXT("A carry started in real time is not dropped in real time"), ShouldDropActiveTask(Wave, ECodexCombatMode::RealTime, false, false));
	TestFalse(TEXT("... nor one planned in the pause"), ShouldDropActiveTask(Wave, ECodexCombatMode::RealTime, false, true));
	TestTrue(TEXT("Turn-based drops any carry"), ShouldDropActiveTask(Wave, ECodexCombatMode::TurnBased, false, false));
	TestTrue(TEXT("... a planned one too"), ShouldDropActiveTask(Wave, ECodexCombatMode::TurnBased, false, true));
	TestEqual(TEXT("Real time: the worker's placement radius"), GetPlacementRadius(Wave, ECodexCombatMode::RealTime, 1200.f, 1500.f), 1500.f);
	TestEqual(TEXT("Pause: the pause radius stays"), GetPlacementRadius(Wave, ECodexCombatMode::TacticalPause, 1200.f, 1500.f), 1200.f);
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

RELOCATION_TEST(FRelocationPausedCarryTest, "PausedCarrySurvivesResume")
bool FRelocationPausedCarryTest::RunTest(const FString&)
{
	// Bug 2026-10-08: a carry ordered in the tactical pause was dropped the frame after the resume.
	using namespace RelocationRules;
	const ECodexGamePhase Wave = ECodexGamePhase::WaveCombat;
	// (The old pause-only rule, Codex.Relocation.RealTimeCombat 0.)
	TestFalse(TEXT("Paused order keeps running in real time"), ShouldDropActiveTask(Wave, ECodexCombatMode::RealTime, false, true, false));
	TestTrue(TEXT("Carry started outside the pause is dropped when live combat begins"),
		ShouldDropActiveTask(Wave, ECodexCombatMode::RealTime, false, false, false));
	TestFalse(TEXT("Zone solo keeps any carry"), ShouldDropActiveTask(Wave, ECodexCombatMode::RealTime, true, false, false));
	TestFalse(TEXT("Paused again: nothing dropped"), ShouldDropActiveTask(Wave, ECodexCombatMode::TacticalPause, false, false, false));
	TestTrue(TEXT("Turn-based drops even a paused order"), ShouldDropActiveTask(Wave, ECodexCombatMode::TurnBased, false, true, false));
	TestFalse(TEXT("Exploration"), ShouldDropActiveTask(ECodexGamePhase::Exploration, ECodexCombatMode::None, false, false, false));
	return true;
}

#undef RELOCATION_TEST

#endif // WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelocationNearestWorkerTest, "CodexTactics.Interactables.RelocationRules.NearestWorker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRelocationNearestWorkerTest::RunTest(const FString& Parameters)
{
	// Left flank barricade: the operative on the left runs (user decision 2026-10-05).
	const TArray<FVector> Positions = { FVector(0.f, 0.f, 0.f), FVector(0.f, -1500.f, 0.f), FVector(0.f, 1500.f, 0.f) };
	const FVector LeftEdge(200.f, -2000.f, 0.f);
	TestEqual(TEXT("closest to the left edge"), RelocationRules::ChooseNearestWorker(Positions, { true, true, true }, LeftEdge), 1);
	TestEqual(TEXT("busy one skipped"), RelocationRules::ChooseNearestWorker(Positions, { true, false, true }, LeftEdge), 0);
	TestEqual(TEXT("height ignored"), RelocationRules::ChooseNearestWorker({ FVector(0.f, 0.f, 900.f), FVector(500.f, 0.f, 0.f) }, { true, true },
		FVector(0.f, 0.f, 0.f)), 0);
	TestEqual(TEXT("nobody free"), RelocationRules::ChooseNearestWorker(Positions, { false, false, false }, LeftEdge), INDEX_NONE);
	return true;
}
