#include "Misc/AutomationTest.h"
#include "Tactics/TurnAttackTimeline.h"

#if WITH_DEV_AUTOMATION_TESTS

// User request 2026-10-09: a grid shot shows turn -> (kneel) -> fire clip -> tracer / hit, never the hit first.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTurnAttackTimelineTest, "CodexTactics.Tactics.TurnAttackTimeline.Order",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTurnAttackTimelineTest::RunTest(const FString&)
{
	FTurnShotReadiness R;
	R.Elapsed = 0.f;
	R.BodyYawErrorDeg = 90.f;
	TestEqual(TEXT("Still turned away: wait"), TurnAttackTimeline::NextStep(R), ETurnShotStep::Wait);
	R.BodyYawErrorDeg = 5.1f;
	TestEqual(TEXT("Just outside the tolerance: wait"), TurnAttackTimeline::NextStep(R), ETurnShotStep::Wait);
	R.BodyYawErrorDeg = -4.f;
	TestEqual(TEXT("On target: fire"), TurnAttackTimeline::NextStep(R), ETurnShotStep::Fire);
	R.bStanceSettling = true;
	TestEqual(TEXT("On target but still kneeling: wait"), TurnAttackTimeline::NextStep(R), ETurnShotStep::Wait);
	R.Elapsed = TurnAttackTimeline::MaxWaitSeconds;
	TestEqual(TEXT("Never longer than the safety limit: fire"), TurnAttackTimeline::NextStep(R), ETurnShotStep::Fire);
	R.bAttackerLost = true;
	TestEqual(TEXT("Attacker gone: drop"), TurnAttackTimeline::NextStep(R), ETurnShotStep::Drop);
	return true;
}

#endif
