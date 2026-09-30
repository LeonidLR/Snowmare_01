#include "Misc/AutomationTest.h"
#include "Characters/ColdAnimationRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Godot cold_animation_controller.gd select_tier / resolve_clip / update parity (thresholds 25 / 40 / 70 / 90, hysteresis 5).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FColdAnimationRulesTest, "CodexTactics.Characters.ColdAnimation.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FColdAnimationRulesTest::RunTest(const FString&)
{
	const TArray<float> Thresholds = { 25.f, 40.f, 70.f, 90.f };
	TestEqual(TEXT("Warm: level 0"), ColdAnimationRules::SelectTier(0, 10.f, Thresholds, 5.f), 0);
	TestEqual(TEXT("25 cold: level 1"), ColdAnimationRules::SelectTier(0, 25.f, Thresholds, 5.f), 1);
	TestEqual(TEXT("Jumps straight to level 3 at 75"), ColdAnimationRules::SelectTier(0, 75.f, Thresholds, 5.f), 3);
	TestEqual(TEXT("Frozen solid: level 4"), ColdAnimationRules::SelectTier(0, 100.f, Thresholds, 5.f), 4);
	TestEqual(TEXT("Hysteresis: 68 keeps level 3"), ColdAnimationRules::SelectTier(3, 68.f, Thresholds, 5.f), 3);
	TestEqual(TEXT("Below 65 drops to level 2"), ColdAnimationRules::SelectTier(3, 64.f, Thresholds, 5.f), 2);
	TestEqual(TEXT("Cold clamps to 0..100"), ColdAnimationRules::SelectTier(4, -20.f, Thresholds, 5.f), 0);
	TestEqual(TEXT("Wrong threshold count: off"), ColdAnimationRules::SelectTier(2, 80.f, { 10.f, 20.f }, 5.f), 0);

	TestEqual(TEXT("Own clip"), ColdAnimationRules::ResolveClipIndex({ true, true, true, true }, 3), 2);
	TestEqual(TEXT("Missing level 3 clip: level 2's"), ColdAnimationRules::ResolveClipIndex({ true, true, false, false }, 3), 1);
	TestEqual(TEXT("No clip at all"), ColdAnimationRules::ResolveClipIndex({ false, false, false, false }, 4), INDEX_NONE);

	TestEqual(TEXT("Not eligible: off at once"), ColdAnimationRules::StepWeight(0.8f, 3, false, 0.016f), 0.f);
	TestTrue(TEXT("Fades in over 0.3 s"), FMath::IsNearlyEqual(ColdAnimationRules::StepWeight(0.f, 2, true, 0.15f), 0.5f, 0.001f));
	TestTrue(TEXT("Fades out when warm"), FMath::IsNearlyEqual(ColdAnimationRules::StepWeight(1.f, 0, true, 0.15f), 0.5f, 0.001f));
	return true;
}

#endif
