// CodexTactics.Characters.ProneCrawl.* - the prone crawl under fire (the game designer's CrawlAndShoot_Bug_01, 2026-11-09, ProneCrawlRules):
// told to crawl he crawls first (no fire until he stops), his body faces the danger, the directional crawl clips move him the ordered way.

#include "Misc/AutomationTest.h"
#include "Characters/ProneCrawlRules.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProneCrawlHoldFireTest, "CodexTactics.Characters.ProneCrawl.NoFireWhileOrderedCrawling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProneCrawlHoldFireTest::RunTest(const FString&)
{
	using namespace ProneCrawlRules;
	FCrawlFacts Crawl;
	Crawl.bProne = true;
	Crawl.bOwnMoveOrder = true;
	Crawl.Speed2D = 60.f;
	TestTrue(TEXT("prone on the player's move order: an ordered crawl"), IsOrderedCrawl(Crawl));
	TestTrue(TEXT("... and he holds his fire"), HoldsFire(Crawl, true));
	TestFalse(TEXT("the console A/B off: he fires while crawling (the old rule)"), HoldsFire(Crawl, false));
	FCrawlFacts Paused = Crawl;
	Paused.Speed2D = 0.f;
	TestTrue(TEXT("a pause on the path (a repath, a stance clip's hold) is still the order: still held"), HoldsFire(Paused, true));
	FCrawlFacts Arrived = Crawl;
	Arrived.bOwnMoveOrder = false;
	Arrived.Speed2D = 0.f;
	TestFalse(TEXT("arrived (the order is over): he fires again"), HoldsFire(Arrived, true));
	FCrawlFacts Lying = Arrived;
	TestFalse(TEXT("lying still (the player's attack order prone and stationary): fires as before"), IsOrderedCrawl(Lying));
	FCrawlFacts Standing = Crawl;
	Standing.bProne = false;
	TestFalse(TEXT("walking upright on an order: not a crawl (fires as before)"), HoldsFire(Standing, true));
	FCrawlFacts Cover = Crawl;
	Cover.bInCover = true;
	TestFalse(TEXT("in cover: the cover's own fire rules"), HoldsFire(Cover, true));
	FCrawlFacts Dead = Crawl;
	Dead.bAlive = false;
	TestFalse(TEXT("dead / knocked down: no crawl"), IsOrderedCrawl(Dead));
	FCrawlFacts Formation;
	Formation.bProne = true;
	Formation.bFormationMove = true;
	Formation.Speed2D = 55.f;
	TestTrue(TEXT("a formation follower crawling after the leader (the player's order to the squad): held"), HoldsFire(Formation, true));
	Formation.Speed2D = FormationCrawlMinSpeedCm - 1.f;
	TestFalse(TEXT("a follower parked in his slot (no real crawl): fires as before"), HoldsFire(Formation, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProneCrawlFacingTest, "CodexTactics.Characters.ProneCrawl.FacingChoice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProneCrawlFacingTest::RunTest(const FString&)
{
	using namespace ProneCrawlRules;
	TestTrue(TEXT("a combat target: the body faces it"), ChooseFacing(true, true, true, true) == ECrawlFacing::CombatTarget);
	TestTrue(TEXT("no target, under fire: the muzzle flash"), ChooseFacing(true, false, true, true) == ECrawlFacing::FireFlash);
	TestTrue(TEXT("neither, crawling on an order: the threat he fought"), ChooseFacing(true, false, false, true) == ECrawlFacing::RememberedThreat);
	TestTrue(TEXT("neither, not an ordered crawl: the movement as before"), ChooseFacing(false, false, false, true) == ECrawlFacing::Movement);
	TestTrue(TEXT("nothing to face: the movement as before"), ChooseFacing(true, false, false, false) == ECrawlFacing::Movement);
	// The clips' direction: the move relative to the body (the body faces the threat at +90 deg world; the order goes the four ways).
	TestEqual(TEXT("ordered towards the threat: the forward crawl"), RelativeMoveDeg(90.f, 90.f), 0.f);
	TestEqual(TEXT("ordered away: the backward crawl"), FMath::Abs(RelativeMoveDeg(90.f, -90.f)), 180.f);
	TestEqual(TEXT("ordered to his right: the right crawl (+90)"), RelativeMoveDeg(90.f, 180.f), 90.f);
	TestEqual(TEXT("ordered to his left: the left crawl (-90)"), RelativeMoveDeg(90.f, 0.f), -90.f);
	TestEqual(TEXT("wraps across +-180"), RelativeMoveDeg(170.f, -170.f), 20.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProneCrawlLayerTest, "CodexTactics.Characters.ProneCrawl.LayerAndPhase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProneCrawlLayerTest::RunTest(const FString&)
{
	using namespace ProneCrawlRules;
	TestEqual(TEXT("crawling on an order: the crawl layer"), LayerTarget(true, true, 0.f, 8.f), 1.f);
	TestEqual(TEXT("moving prone without an order (pushed, a formation creep): the crawl layer"), LayerTarget(true, false, 20.f, 8.f), 1.f);
	TestEqual(TEXT("lying still: the ABP's rifle prone pose (aim / fire / reload)"), LayerTarget(true, false, 2.f, 8.f), 0.f);
	TestEqual(TEXT("not eligible (a full-body clip, cover, dead, a stance clip): off"), LayerTarget(false, true, 60.f, 8.f), 0.f);
	// The phase: speed over the cycle distance; a reversal's cross-fade keeps the old clips' contacts planted (their cycle weighs in).
	TestTrue(TEXT("62 cm/s over a 70 cm cycle: 0.886 cycles a second"), FMath::IsNearlyEqual(PhaseRate(62.f, 70.f, 0.f, 0.f, 10.f, 3.33f), 62.f / 70.f, 1.e-4f));
	TestTrue(TEXT("mid cross-fade: the two cycles blended"), FMath::IsNearlyEqual(PhaseRate(60.f, 60.f, 120.f, 0.5f, 10.f, 3.f), 60.f / 90.f, 1.e-4f));
	TestTrue(TEXT("capped at max_play_rate / length"), FMath::IsNearlyEqual(PhaseRate(500.f, 70.f, 0.f, 0.f, 3.f, 3.f), 1.f, 1.e-4f));
	TestEqual(TEXT("standing still: the phase stops"), PhaseRate(0.f, 70.f, 0.f, 0.f, 3.f, 3.f), 0.f);
	TestEqual(TEXT("no cycle data: no phase"), PhaseRate(60.f, 0.f, 0.f, 0.f, 3.f, 3.f), 0.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
