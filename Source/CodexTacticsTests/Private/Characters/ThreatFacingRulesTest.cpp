// CodexTactics.Squad.ThreatFacing.* - after a retreat at a run he turns back to the enemy he was fighting (user request
// 2026-10-22, ThreatFacingRules).

#include "Misc/AutomationTest.h"
#include "Characters/ThreatFacingRules.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FThreatFacingRulesTest, "CodexTactics.Squad.ThreatFacing.TurnsBackAfterTheRetreat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FThreatFacingRulesTest::RunTest(const FString&)
{
	using namespace ThreatFacingRules;
	FFacts Facts;
	Facts.bRealTimeFight = true;
	Facts.bStanding = true;
	Facts.bFoughtThisFight = true;
	Facts.bHasThreat = true;
	TestTrue(TEXT("the run is over, the enemy he fought still comes: he turns to it"), ShouldTurnToThreat(Facts));
	FFacts Running = Facts;
	Running.bStanding = false;
	TestFalse(TEXT("still running: he faces where he runs"), ShouldTurnToThreat(Running));
	FFacts Aiming = Facts;
	Aiming.bFacingCombatTarget = true;
	TestFalse(TEXT("a target in his sights turns him already"), ShouldTurnToThreat(Aiming));
	FFacts Cover = Facts;
	Cover.bInCover = true;
	TestFalse(TEXT("in cover the back stays to the wall"), ShouldTurnToThreat(Cover));
	FFacts Ordered = Facts;
	Ordered.bExplicitFacing = true;
	TestFalse(TEXT("a facing the player gave wins"), ShouldTurnToThreat(Ordered));
	FFacts Grid = Facts;
	Grid.bGridOwnsFacing = true;
	TestFalse(TEXT("turn-based: the grid owns the facing"), ShouldTurnToThreat(Grid));
	FFacts Exploring = Facts;
	Exploring.bRealTimeFight = false;
	TestFalse(TEXT("exploration / no threat: unchanged behaviour"), ShouldTurnToThreat(Exploring));
	FFacts NoFight = Facts;
	NoFight.bFoughtThisFight = false;
	TestFalse(TEXT("never fought anyone in this fight: no turn"), ShouldTurnToThreat(NoFight));
	FFacts NoThreat = Facts;
	NoThreat.bHasThreat = false;
	TestFalse(TEXT("nobody left to face: no turn"), ShouldTurnToThreat(NoThreat));

	FVector Point;
	TestTrue(TEXT("the last target alive: its spot"), PickThreatPoint(true, FVector(100.f, 0.f, 0.f), true, FVector(0.f, 50.f, 0.f), Point)
		&& Point == FVector(100.f, 0.f, 0.f));
	TestTrue(TEXT("the last target dead: the nearest live enemy"), PickThreatPoint(false, FVector(100.f, 0.f, 0.f), true, FVector(0.f, 50.f, 0.f), Point)
		&& Point == FVector(0.f, 50.f, 0.f));
	TestFalse(TEXT("nobody: nothing to face"), PickThreatPoint(false, FVector::ZeroVector, false, FVector::ZeroVector, Point));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
