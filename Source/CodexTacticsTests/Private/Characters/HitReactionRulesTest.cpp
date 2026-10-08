#include "Misc/AutomationTest.h"
#include "Characters/HitReactionRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Operative hit reactions (user request 2026-10-08): throttled, not in cover, not over a busy body; back / front.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHitReactionRulesTest, "CodexTactics.Anim.HitReaction.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHitReactionRulesTest::RunTest(const FString&)
{
	using namespace HitReactionRules;
	const FHitReactionSettings Settings;
	FHitReactionContext Hit;
	Hit.Damage = 12.f;
	TestTrue(TEXT("First hit reacts"), ShouldReact(Settings, Hit));
	Hit.SecondsSinceLast = 0.3f;
	TestFalse(TEXT("A burst within 0.8 s plays one"), ShouldReact(Settings, Hit));
	Hit.SecondsSinceLast = 0.85f;
	TestTrue(TEXT("Next one after the interval"), ShouldReact(Settings, Hit));
	Hit.bInCover = true;
	TestFalse(TEXT("Cover keeps its pose"), ShouldReact(Settings, Hit));
	FHitReactionSettings CoverOn;
	CoverOn.bAllowInCover = true;
	TestTrue(TEXT("...unless allowed"), ShouldReact(CoverOn, Hit));
	Hit.bInCover = false;
	Hit.bBusy = true;
	TestFalse(TEXT("Knocked down / throwing / reloading"), ShouldReact(Settings, Hit));
	Hit.bBusy = false;
	Hit.Damage = 0.5f;
	TestFalse(TEXT("Chip damage"), ShouldReact(Settings, Hit));
	const FVector Forward(1.f, 0.f, 0.f);
	TestFalse(TEXT("Shot from the front"), IsFromBehind(Forward, FVector::ZeroVector, FVector(500.f, 100.f, 0.f)));
	TestTrue(TEXT("Shot from behind"), IsFromBehind(Forward, FVector::ZeroVector, FVector(-500.f, 100.f, 0.f)));
	TestFalse(TEXT("No source"), IsFromBehind(Forward, FVector::ZeroVector, FVector::ZeroVector));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
