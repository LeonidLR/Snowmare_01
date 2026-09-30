#include "Misc/AutomationTest.h"
#include "Combat/EncounterQueries.h"

#if WITH_DEV_AUTOMATION_TESTS

// Turn-based fights start on flat ground only (user decision 2026-09-30).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlatGroundTest, "CodexTactics.Combat.TurnBasedFlatGround",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlatGroundTest::RunTest(const FString&)
{
	// Mostly ground at 0, a platform (240) and a crate top (90) among the samples.
	const TArray<float> Samples = { 0.f, 0.f, 2.f, 240.f, 240.f, 0.f, 90.f, -1.f, 0.f };
	float Ground = 0.f;
	TestTrue(TEXT("Squad on the ground: allowed"), CombatQueries::IsSquadOnFlatGround(Samples, { 0.f, 3.f, -2.f }, 50.f, Ground));
	TestEqual(TEXT("Ground level is the median"), Ground, 0.f);
	TestFalse(TEXT("One operative up on the platform: refused"), CombatQueries::IsSquadOnFlatGround(Samples, { 0.f, 240.f, 0.f }, 50.f, Ground));
	TestFalse(TEXT("Down in a pit: refused"), CombatQueries::IsSquadOnFlatGround(Samples, { -120.f, -120.f, -120.f }, 50.f, Ground));
	TestTrue(TEXT("Whole fight area up on a wide roof: flat there"),
		CombatQueries::IsSquadOnFlatGround({ 240.f, 240.f, 238.f, 241.f, 0.f }, { 240.f, 240.f }, 50.f, Ground));
	return true;
}

#endif
