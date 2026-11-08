// CodexTactics.Falls.CorpseFall - fix/corpse-falls-to-ground (game designer PIE 2026-11-08, L_Gen_Depot_1: a PMC fighter killed on a ladder
// hung ~2 m up in his death pose, another body lay against the wall face). UE-only, no Godot reference. Pure rule: a body killed on a
// ladder, mid-vault or over a drop falls first and settles on landing; one on his floor stays put.

#include "Falls/FallRules.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCorpseFallRulesTest, "CodexTactics.Falls.CorpseFall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCorpseFallRulesTest::RunTest(const FString&)
{
	// Who falls.
	TestTrue(TEXT("killed on a ladder 2 m up: falls"), FallRules::ShouldCorpseFall(true, false, true, 200.f));
	TestTrue(TEXT("killed on a ladder right above its foot: still dropped off it"), FallRules::ShouldCorpseFall(true, false, true, 5.f));
	TestTrue(TEXT("killed mid-vault: falls onto the obstacle / beside it"), FallRules::ShouldCorpseFall(false, true, true, 60.f));
	TestTrue(TEXT("killed in the air off a roof edge (3 m): falls"), FallRules::ShouldCorpseFall(false, false, true, 300.f));
	TestFalse(TEXT("standing on his floor: lies where he is"), FallRules::ShouldCorpseFall(false, false, true, 3.f));
	TestFalse(TEXT("on a step / kerb (30 cm): no fall"), FallRules::ShouldCorpseFall(false, false, true, FallRules::CorpseFloorToleranceCm));
	TestTrue(TEXT("31 cm over the floor: falls"), FallRules::ShouldCorpseFall(false, false, true, FallRules::CorpseFloorToleranceCm + 1.f));
	TestFalse(TEXT("no floor below at all (out of the level): no endless fall"), FallRules::ShouldCorpseFall(false, false, false, 0.f));
	// When he has landed.
	TestTrue(TEXT("the movement found the ground"), FallRules::HasCorpseLanded(true, true, 10.f, 0.3f));
	TestTrue(TEXT("on the floor by the trace"), FallRules::HasCorpseLanded(false, true, 1.f, 0.3f));
	TestFalse(TEXT("still 1 m up, falling"), FallRules::HasCorpseLanded(false, true, 100.f, 0.3f));
	TestTrue(TEXT("wedged: settles after the timeout"), FallRules::HasCorpseLanded(false, true, 100.f, FallRules::CorpseFallTimeoutSeconds));
	TestFalse(TEXT("no floor, falling, in time"), FallRules::HasCorpseLanded(false, false, 0.f, 1.f));
	return true;
}

#endif
