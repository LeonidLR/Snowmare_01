// CodexTactics.Combat.Sight.* — Sprint 08 line of sight against 60 cm barricades (TANDEM «SPRINT 08 DIRECTIVE»).

#include "Combat/SightRules.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSightHeightsTest, "CodexTactics.Combat.Sight.CoverOcclusion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSightHeightsTest::RunTest(const FString& Parameters)
{
	using namespace SightRules;
	TestEqual(TEXT("eyes standing"), EyeHeight(EOperativeStance::Standing), 160.f);
	TestEqual(TEXT("eyes crouched"), EyeHeight(EOperativeStance::Crouching), 95.f);
	TestEqual(TEXT("eyes prone"), EyeHeight(EOperativeStance::Prone), 25.f);
	TestEqual(TEXT("profile standing"), ProfileHeight(EOperativeStance::Standing), 150.f);
	TestEqual(TEXT("profile crouched"), ProfileHeight(EOperativeStance::Crouching), 90.f);
	TestEqual(TEXT("profile prone"), ProfileHeight(EOperativeStance::Prone), 25.f);

	// 8-A.1: crouched 0.5 m behind his barricade, enemies 10 m away.
	TestTrue(TEXT("crouched sees a standing enemy over his cover"), ClearsCover(95.f, 150.f, 50.f, 1000.f));
	TestTrue(TEXT("crouched sees a crouching enemy over his cover"), ClearsCover(95.f, 90.f, 50.f, 1000.f));
	// 8-A.2: the enemy goes prone 1 m behind a barricade -> hidden from the crouched observer 10 m away.
	TestFalse(TEXT("prone enemy behind its barricade hidden (crouched, 10 m)"), ClearsCover(95.f, 25.f, 900.f, 1000.f));
	// 8-A.3: standing up gives the steep angle — close in (3 m) the prone one shows, crouched it does not.
	TestTrue(TEXT("standing 3 m away sees the prone one"), ClearsCover(160.f, 25.f, 200.f, 300.f));
	TestFalse(TEXT("crouched 3 m away does not"), ClearsCover(95.f, 25.f, 200.f, 300.f));
	// 8-B: symmetric — an enemy standing 10 m away does not see a prone operative 1 m behind a barricade, sees a crouched one.
	TestFalse(TEXT("prone operative hidden from a standing enemy"), ClearsCover(160.f, 25.f, 900.f, 1000.f));
	TestTrue(TEXT("crouched operative seen over the cover"), ClearsCover(160.f, 90.f, 900.f, 1000.f));
	TestEqual(TEXT("line height halfway"), LineHeightAt(160.f, 20.f, 500.f, 1000.f), 90.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSightSensesTest, "CodexTactics.Combat.Sight.HearingBlindFireMemory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSightSensesTest::RunTest(const FString& Parameters)
{
	using namespace SightRules;
	TestEqual(TEXT("demask 2 s"), DemaskSeconds, 2.f);
	TestEqual(TEXT("squad hears within 12 m"), SquadHearingCm, 1200.f);
	TestEqual(TEXT("enemy hears a walking operative at 12 m"), EnemyHearingRadius(EOperativeStance::Crouching), 1200.f);
	TestEqual(TEXT("a crawling one only at 5 m"), EnemyHearingRadius(EOperativeStance::Prone), 500.f);
	TestEqual(TEXT("blind fire -80 %"), BlindFireHitChance(0.8f, 50.f), 0.16f, 0.001f);
	TestEqual(TEXT("blind fire at an empty spot never hits"), BlindFireHitChance(0.8f, 400.f), 0.f);
	TestFalse(TEXT("pursues a fresh last known spot"), ShouldForget(5.f, 0.f));
	TestFalse(TEXT("searches the spot a while"), ShouldForget(8.f, 3.f));
	TestTrue(TEXT("gives up after searching 5 s"), ShouldForget(10.f, 5.f));
	TestTrue(TEXT("forgets an old memory"), ShouldForget(25.f, 0.f));
	return true;
}
