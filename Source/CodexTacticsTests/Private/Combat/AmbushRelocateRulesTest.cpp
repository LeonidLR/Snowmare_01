#include "Misc/AutomationTest.h"
#include "AI/AmbushWatchRules.h"
#include "Combat/AmbushPlanRules.h"
#include "Combat/AmbushRelocateRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// The game designer's decisions of 2026-11-01 night (docs/design/COMBAT_LAB.md 2.1): (1) a bypassed ambush RUNS a concealed route on foot -
// spotted on the way it strikes, no concealed route -> no teleport (it stays, then follows as a patrol); (2) an ambush watched all along strikes
// after its 60-90 s hold limit (single groups and coordinated plans). UE-only, no Godot reference.

namespace AmbushRelocateTestLocal
{
	FAmbushRouteCandidate RelocateRoute(float Score, float PathCm, int32 Seen = 0, bool bReachable = true, bool bDestinationConcealed = true)
	{
		FAmbushRouteCandidate Out;
		Out.Score = Score;
		Out.PathCm = PathCm;
		Out.SeenSamples = Seen;
		Out.bReachable = bReachable;
		Out.bDestinationConcealed = bDestinationConcealed;
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAmbushRelocateSampleTest, "CodexTactics.Combat.AmbushRelocate.SamplePath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAmbushRelocateSampleTest::RunTest(const FString&)
{
	// An L-shaped route round a wall corner: 250 cm east, then 150 cm north (400 cm in all).
	const TArray<FVector2D> Path = { FVector2D(0.0, 0.0), FVector2D(250.0, 0.0), FVector2D(250.0, 150.0) };
	TestEqual(TEXT("Its length"), AmbushRelocateRules::PathLength(Path), 400.f, 0.01f);
	const TArray<FVector2D> Samples = AmbushRelocateRules::SamplePath(Path, 100.f);
	TestEqual(TEXT("Every 100 cm along it, both ends included: 0, 100, 200, 300, 400"), Samples.Num(), 5);
	if (Samples.Num() == 5)
	{
		TestTrue(TEXT("Starts at its start"), Samples[0].Equals(FVector2D(0.0, 0.0), 0.01));
		TestTrue(TEXT("200 cm along: on the first leg"), Samples[2].Equals(FVector2D(200.0, 0.0), 0.01));
		TestTrue(TEXT("300 cm along: round the corner, 50 cm up the second leg"), Samples[3].Equals(FVector2D(250.0, 50.0), 0.01));
		TestTrue(TEXT("Ends at its end"), Samples[4].Equals(FVector2D(250.0, 150.0), 0.01));
	}
	TestEqual(TEXT("A single point: itself"), AmbushRelocateRules::SamplePath({ FVector2D(5.0, 5.0) }, 100.f).Num(), 1);
	TestEqual(TEXT("No path: no samples"), AmbushRelocateRules::SamplePath({}, 100.f).Num(), 0);
	TestEqual(TEXT("A 450 cm leg: 5 samples + its end"), AmbushRelocateRules::SamplePath({ FVector2D(0.0, 0.0), FVector2D(450.0, 0.0) }, 100.f).Num(), 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAmbushRelocateRouteTest, "CodexTactics.Combat.AmbushRelocate.PickRoute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAmbushRelocateRouteTest::RunTest(const FString&)
{
	using namespace AmbushRelocateTestLocal;
	const FAmbushRelocateParams Params;
	TestTrue(TEXT("Reachable, never seen, the spot concealed: a concealed route"), AmbushRelocateRules::IsConcealedRoute(RelocateRoute(1.f, 2500.f), Params));
	TestFalse(TEXT("One sample in an operative's sight (a man on a tower sees over the wall): refused"),
		AmbushRelocateRules::IsConcealedRoute(RelocateRoute(1.f, 2500.f, 1), Params));
	TestFalse(TEXT("No full navmesh path: refused"), AmbushRelocateRules::IsConcealedRoute(RelocateRoute(1.f, 2500.f, 0, false), Params));
	TestFalse(TEXT("The new spot itself seen: refused"), AmbushRelocateRules::IsConcealedRoute(RelocateRoute(1.f, 2500.f, 0, true, false), Params));
	TestFalse(TEXT("Longer than max_route_cm (100 m): refused"), AmbushRelocateRules::IsConcealedRoute(RelocateRoute(1.f, 10100.f), Params));

	// The best spot's route crosses the squad's view: the next best concealed one is run (the spot score decides, not the path).
	const TArray<FAmbushRouteCandidate> Routes = { RelocateRoute(2.f, 1800.f, 3), RelocateRoute(1.5f, 3200.f), RelocateRoute(1.2f, 1500.f), RelocateRoute(1.5f, 2400.f) };
	TestEqual(TEXT("The best CONCEALED route wins; a tie goes to the shorter path"), AmbushRelocateRules::PickRoute(Routes, Params), 3);
	TestEqual(TEXT("Nothing concealed: none (no teleport - it stays)"), AmbushRelocateRules::PickRoute({ RelocateRoute(2.f, 1800.f, 1), RelocateRoute(1.f, 900.f, 0, false) }, Params),
		INDEX_NONE);
	TestEqual(TEXT("No candidates: none"), AmbushRelocateRules::PickRoute({}, Params), INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAmbushRelocateOutcomeTest, "CodexTactics.Combat.AmbushRelocate.Outcome",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAmbushRelocateOutcomeTest::RunTest(const FString&)
{
	FAmbushRelocateParams Params;
	// No concealed route: it stays put the first time, then becomes a patrol following the squad (no_route_tries 2).
	TestTrue(TEXT("1st search without a route: stays"), AmbushRelocateRules::NoRouteOutcome(1, Params) == EAmbushNoRouteOutcome::Stay);
	TestTrue(TEXT("2nd: a patrol following the squad"), AmbushRelocateRules::NoRouteOutcome(2, Params) == EAmbushNoRouteOutcome::Patrol);
	Params.bNoRoutePatrol = false;
	TestTrue(TEXT("no_route_patrol false: it only ever stays (the recycling takes it)"), AmbushRelocateRules::NoRouteOutcome(9, Params) == EAmbushNoRouteOutcome::Stay);

	// The run: spotted beats everything, then the arrival, then the time limit.
	const float Limit = AmbushRelocateRules::RunLimitSeconds(3000.f, 300.f);
	TestEqual(TEXT("Run limit: twice the path at its pace + 10 s"), Limit, 30.f, 0.01f);
	TestTrue(TEXT("On its way"), AmbushRelocateRules::StepRun(false, false, 5.f, Limit) == EAmbushRunStep::Running);
	TestTrue(TEXT("Spotted on the way: it strikes at once"), AmbushRelocateRules::StepRun(true, false, 5.f, Limit) == EAmbushRunStep::Spotted);
	TestTrue(TEXT("... even as it arrives"), AmbushRelocateRules::StepRun(true, true, 5.f, Limit) == EAmbushRunStep::Spotted);
	TestTrue(TEXT("Every runner at his spot: arrived (it hides there)"), AmbushRelocateRules::StepRun(false, true, 12.f, Limit) == EAmbushRunStep::Arrived);
	TestTrue(TEXT("Far too long: it stops and hides where it stands (never a teleport)"), AmbushRelocateRules::StepRun(false, false, 31.f, Limit)
		== EAmbushRunStep::TimedOut);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAmbushHoldLimitTest, "CodexTactics.AI.AmbushWatch.HoldLimit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAmbushHoldLimitTest::RunTest(const FString&)
{
	FAmbushWatchParams Params;
	TestEqual(TEXT("Default range 60..90 s (decision 2)"), Params.HoldStrikeMinSeconds * 1000.f + Params.HoldStrikeMaxSeconds, 60090.f);
	TestEqual(TEXT("Random 0: 60 s"), AmbushWatchRules::HoldLimitSeconds(0.f, Params), 60.f);
	TestEqual(TEXT("Random 0.5: 75 s"), AmbushWatchRules::HoldLimitSeconds(0.5f, Params), 75.f);
	TestEqual(TEXT("Random 1: 90 s"), AmbushWatchRules::HoldLimitSeconds(1.f, Params), 90.f);
	TestFalse(TEXT("Held 59 s of a 60 s limit: still holds"), AmbushWatchRules::HeldTooLong(59.f, 60.f));
	TestTrue(TEXT("60 s: strikes anyway"), AmbushWatchRules::HeldTooLong(60.f, 60.f));
	Params.HoldStrikeMaxSeconds = 0.f;
	TestEqual(TEXT("max 0: no limit"), AmbushWatchRules::HoldLimitSeconds(0.7f, Params), 0.f);
	TestFalse(TEXT("... it never strikes for holding"), AmbushWatchRules::HeldTooLong(10000.f, 0.f));

	// A coordinated plan watched all round strikes too (the flank block) once its limit is reached.
	FAmbushPlanParams Plan;
	FAmbushPlanTick Tick;
	Tick.Phase = EAmbushPlanPhase::Holding;
	Tick.NearestSquadCm = 1200.f;
	Tick.FlanksUnwatched = 0;
	Tick.HoldLimitSeconds = 72.f;
	Tick.HoldSeconds = 71.9f;
	TestTrue(TEXT("Plan watched all round, 71.9 of 72 s: holds"), AmbushPlanRules::NextPhase(Tick, Plan) == EAmbushPlanPhase::Holding);
	Tick.HoldSeconds = 72.f;
	TestTrue(TEXT("... 72 s: the flank block anyway"), AmbushPlanRules::NextPhase(Tick, Plan) == EAmbushPlanPhase::FlankBlock);
	Tick.NearestSquadCm = 2500.f;
	TestTrue(TEXT("Out of range the limit means nothing: waiting"), AmbushPlanRules::NextPhase(Tick, Plan) == EAmbushPlanPhase::Waiting);
	Tick.NearestSquadCm = 1200.f;
	Tick.HoldLimitSeconds = 0.f;
	TestTrue(TEXT("No limit: it holds for ever (the first version)"), AmbushPlanRules::NextPhase(Tick, Plan) == EAmbushPlanPhase::Holding);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
