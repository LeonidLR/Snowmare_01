// CodexTactics.AI.PatrolRoute.* — Sprint 11 outpost stealth patrols (spline routes, hound escort, alert break).
// No Godot reference — Sprint 11 spec by Gemini, docs/port/TANDEM.md «SPRINT 11 DIRECTIVE».

#include "AI/PatrolRouteRules.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPatrolRouteLoopTest, "CodexTactics.AI.PatrolRoute.LoopProgression",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPatrolRouteLoopTest::RunTest(const FString& Parameters)
{
	using namespace PatrolRouteRules;
	bool bForward = true;
	// Loop 0 -> 1 -> 2 -> 0 -> 1.
	int32 Index = 0;
	const int32 Expected[] = { 1, 2, 0, 1 };
	for (const int32 Want : Expected)
	{
		Index = GetNextWaypointIndex(3, Index, /*bIsLoop*/ true, /*bPingPong*/ false, bForward);
		TestEqual(FString::Printf(TEXT("loop step to %d"), Want), Index, Want);
	}
	TestTrue(TEXT("loop stays forward"), bForward);
	// Loop wins over ping-pong.
	bForward = true;
	TestEqual(TEXT("loop + ping-pong wraps"), GetNextWaypointIndex(3, 2, true, true, bForward), 0);

	// Neither flag: stops at the last point.
	bForward = true;
	TestEqual(TEXT("one-way 0 -> 1"), GetNextWaypointIndex(3, 0, false, false, bForward), 1);
	TestEqual(TEXT("one-way 1 -> 2"), GetNextWaypointIndex(3, 1, false, false, bForward), 2);
	TestEqual(TEXT("one-way ends at 2"), GetNextWaypointIndex(3, 2, false, false, bForward), static_cast<int32>(INDEX_NONE));

	// Degenerate routes.
	TestEqual(TEXT("empty route"), GetNextWaypointIndex(0, 0, true, false, bForward), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("single point loop stays"), GetNextWaypointIndex(1, 0, true, false, bForward), 0);
	TestEqual(TEXT("single point one-way ends"), GetNextWaypointIndex(1, 0, false, false, bForward), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("out-of-range index restarts"), GetNextWaypointIndex(3, 7, true, false, bForward), 0);

	// Waits: per-point entry > 0, else the default.
	const TArray<float> Waits = { 5.f, 0.f, -1.f };
	TestEqual(TEXT("per-point wait"), GetWaitTime(Waits, 0, 3.f), 5.f);
	TestEqual(TEXT("0 -> default"), GetWaitTime(Waits, 1, 3.f), 3.f);
	TestEqual(TEXT("negative -> default"), GetWaitTime(Waits, 2, 3.f), 3.f);
	TestEqual(TEXT("missing entry -> default"), GetWaitTime(Waits, 3, 3.f), 3.f);
	TestEqual(TEXT("never negative"), GetWaitTime({}, 0, -2.f), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPatrolRoutePingPongTest, "CodexTactics.AI.PatrolRoute.PingPongProgression",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPatrolRoutePingPongTest::RunTest(const FString& Parameters)
{
	using namespace PatrolRouteRules;
	// 0 -> 1 -> 2 -> 1 -> 0 -> 1 -> 2 on a three-point route.
	bool bForward = true;
	int32 Index = 0;
	const int32 Expected[] = { 1, 2, 1, 0, 1, 2 };
	const bool ExpectedForward[] = { true, true, false, false, true, true };
	for (int32 Step = 0; Step < UE_ARRAY_COUNT(Expected); ++Step)
	{
		Index = GetNextWaypointIndex(3, Index, /*bIsLoop*/ false, /*bPingPong*/ true, bForward);
		TestEqual(FString::Printf(TEXT("ping-pong step %d"), Step), Index, Expected[Step]);
		TestEqual(FString::Printf(TEXT("ping-pong direction %d"), Step), bForward, ExpectedForward[Step]);
	}
	// Two points: back and forth.
	bForward = true;
	TestEqual(TEXT("2 points 0 -> 1"), GetNextWaypointIndex(2, 0, false, true, bForward), 1);
	TestEqual(TEXT("2 points 1 -> 0"), GetNextWaypointIndex(2, 1, false, true, bForward), 0);
	TestEqual(TEXT("2 points 0 -> 1 again"), GetNextWaypointIndex(2, 0, false, true, bForward), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPatrolRouteEscortTest, "CodexTactics.AI.PatrolRoute.EscortFollowsLeader",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPatrolRouteEscortTest::RunTest(const FString& Parameters)
{
	using namespace PatrolRouteRules;
	const FVector Leader(1000.f, 500.f, 90.f);

	// Within the 200-350 cm band: mills about, no move.
	FEscortDecision Decision = EvaluateEscort(Leader + FVector(-300.f, 0.f, -30.f), Leader, false);
	TestFalse(TEXT("3 m: stays"), Decision.bShouldMove);
	Decision = EvaluateEscort(Leader + FVector(0.f, 220.f, 0.f), Leader, false);
	TestFalse(TEXT("2.2 m: stays"), Decision.bShouldMove);
	Decision = EvaluateEscort(Leader + FVector(150.f, 0.f, 0.f), Leader, false);
	TestFalse(TEXT("1.5 m (too close): does not push into him"), Decision.bShouldMove);

	// Beyond 350 cm: catches up to the middle of the band (275 cm) on its own side, keeping its height.
	const FVector Behind = Leader + FVector(-800.f, 0.f, -30.f);
	Decision = EvaluateEscort(Behind, Leader, false);
	TestTrue(TEXT("8 m: catches up"), Decision.bShouldMove);
	TestEqual(TEXT("destination 2.75 m from him"), static_cast<float>(FVector::Dist2D(Decision.Destination, Leader)), (EscortMinCm + EscortMaxCm) * 0.5f, 0.5f);
	TestTrue(TEXT("on its own side (behind)"), Decision.Destination.X < Leader.X);
	TestEqual(TEXT("keeps its height"), static_cast<float>(Decision.Destination.Z), static_cast<float>(Behind.Z), 0.01f);

	// Hysteresis: once moving it goes on to the middle of the band, not stopping at the 350 cm edge.
	Decision = EvaluateEscort(Leader + FVector(-320.f, 0.f, 0.f), Leader, true);
	TestTrue(TEXT("moving at 3.2 m: keeps closing in"), Decision.bShouldMove);
	Decision = EvaluateEscort(Leader + FVector(-260.f, 0.f, 0.f), Leader, true);
	TestFalse(TEXT("moving at 2.6 m: arrived"), Decision.bShouldMove);
	Decision = EvaluateEscort(Leader + FVector(-320.f, 0.f, 0.f), Leader, false);
	TestFalse(TEXT("idle at 3.2 m: stays"), Decision.bShouldMove);

	// Exactly on the leader (degenerate): still a finite destination in the band.
	Decision = EvaluateEscort(Leader, Leader, true);
	TestFalse(TEXT("on top of him: no move"), Decision.bShouldMove);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPatrolRouteAlertTest, "CodexTactics.AI.PatrolRoute.AlertBreaksPatrol",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPatrolRouteAlertTest::RunTest(const FString& Parameters)
{
	using namespace PatrolRouteRules;
	FPatrolAlertInput Calm;
	TestFalse(TEXT("nothing: keeps patrolling"), ShouldBreakPatrol(Calm));

	FPatrolAlertInput Sight;
	Sight.bSeesOperative = true;
	TestTrue(TEXT("sees an operative: engage"), ShouldBreakPatrol(Sight));

	FPatrolAlertInput Damage;
	Damage.bTookDamage = true;
	TestTrue(TEXT("took damage: engage"), ShouldBreakPatrol(Damage));

	FPatrolAlertInput Partner;
	Partner.bPartnerAlerted = true;
	TestTrue(TEXT("leader / escort alerted: engage"), ShouldBreakPatrol(Partner));

	FPatrolAlertInput TrapNear;
	TrapNear.TrapDistanceCm = 1500.f;
	TestTrue(TEXT("tripwire at 15 m: engage"), ShouldBreakPatrol(TrapNear));
	TrapNear.TrapDistanceCm = 2000.f;
	TestTrue(TEXT("tripwire at exactly 20 m: engage"), ShouldBreakPatrol(TrapNear));

	FPatrolAlertInput TrapFar;
	TrapFar.TrapDistanceCm = 2500.f;
	TestFalse(TEXT("tripwire at 25 m: stays on patrol"), ShouldBreakPatrol(TrapFar));
	TestEqual(TEXT("default trap alert radius 20 m"), TrapAlertRadiusCm, 2000.f);
	TestFalse(TEXT("no trap (-1)"), IsTrapHeard(-1.f));

	// A wider tuned radius hears the 25 m blast.
	TrapFar.TrapAlertRadiusCm = 3000.f;
	TestTrue(TEXT("tuned 30 m radius hears 25 m"), ShouldBreakPatrol(TrapFar));
	return true;
}
