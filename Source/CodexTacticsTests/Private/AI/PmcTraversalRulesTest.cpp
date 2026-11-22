// CodexTactics.AI.PmcTraversal.* - the pure rules of the PMC traversal check (smart enemies T-12, task 095; UE-only, no Godot
// reference): PmcTraversalRules (leg timeout by height, stair flight evidence, arrival on the ground, the 95 % bar, failure groups).

#include "Characters/PmcTraversalRules.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#define PMC_TRAVERSAL_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.AI.PmcTraversal." TestPath, EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

PMC_TRAVERSAL_TEST(FPmcTraversalTimeoutTest, "TimeoutScalesWithHeight")
bool FPmcTraversalTimeoutTest::RunTest(const FString&)
{
	using namespace PmcTraversalRules;
	const FPmcTraversalTimeout Timeout; // 15 s + walk / 150 + climb / 30 (+ 8 s transition)
	TestEqual(TEXT("Flat short walk = the base"), LegTimeoutSeconds(Timeout, 0.f, 0.f, false), 15.f);
	TestEqual(TEXT("Walk 300 cm adds 2 s"), LegTimeoutSeconds(Timeout, 300.f, 0.f, false), 17.f);
	// Review 095 item 3: the 24-rung 874 cm ladders were cut at the flat 15 s mid ClimbUp (the clips climb ~41 cm/s = 21 s alone).
	const float Tall = LegTimeoutSeconds(Timeout, 200.f, 874.f, true);
	TestTrue(TEXT("874 cm ladder gets more than its climb time at 41 cm/s plus walk"), Tall > 874.f / 41.f + 10.f);
	TestTrue(TEXT("Taller ladder -> longer budget"), LegTimeoutSeconds(Timeout, 200.f, 874.f, true) > LegTimeoutSeconds(Timeout, 200.f, 300.f, true));
	TestTrue(TEXT("Negative inputs clamp to the base"), FMath::IsNearlyEqual(LegTimeoutSeconds(Timeout, -50.f, -10.f, false), 15.f));
	return true;
}

PMC_TRAVERSAL_TEST(FPmcTraversalStairFlightTest, "StairFlightEvidence")
bool FPmcTraversalStairFlightTest::RunTest(const FString&)
{
	using namespace PmcTraversalRules;
	// A flight from (0,0) at z 0 up to (600,0) at z 300.
	const TArray<FVector2D> Flight = { FVector2D(0, 0), FVector2D(600, 0) };
	TestTrue(TEXT("Halfway up on the flight"), IsOnStairFlight(FVector(300, 20, 150), Flight, 0.f, 300.f, 150.f));
	TestFalse(TEXT("At the foot (the spawn before the flight) is no evidence"), IsOnStairFlight(FVector(-100, 0, 0), Flight, 0.f, 300.f, 150.f));
	TestFalse(TEXT("Under the flight at ground level is no evidence (review: 2D-only check)"), IsOnStairFlight(FVector(300, 0, 0), Flight, 0.f, 300.f, 150.f));
	TestFalse(TEXT("Mid height but far beside the flight"), IsOnStairFlight(FVector(300, 400, 150), Flight, 0.f, 300.f, 150.f));
	TestFalse(TEXT("Already on top"), IsOnStairFlight(FVector(600, 0, 300), Flight, 0.f, 300.f, 150.f));
	TestFalse(TEXT("Too low a flight has no band"), IsOnStairFlight(FVector(300, 0, 20), Flight, 0.f, 40.f, 150.f));
	// An external flight with a landing: foot -> mid -> top.
	const TArray<FVector2D> Turned = { FVector2D(0, 0), FVector2D(300, 0), FVector2D(300, 300) };
	TestTrue(TEXT("On the second run of a turned flight"), IsOnStairFlight(FVector(310, 200, 200), Turned, 0.f, 300.f, 100.f));
	TestTrue(TEXT("Polyline distance to the nearest segment"), FMath::IsNearlyEqual(DistToPolyline2D(FVector2D(150, 50), Turned), 50.f, 0.01f));
	TestTrue(TEXT("Single point polyline"), FMath::IsNearlyEqual(DistToPolyline2D(FVector2D(3, 4), { FVector2D(0, 0) }), 5.f, 0.01f));
	return true;
}

PMC_TRAVERSAL_TEST(FPmcTraversalArrivalTest, "ArrivalNeedsGround")
bool FPmcTraversalArrivalTest::RunTest(const FString&)
{
	using namespace PmcTraversalRules;
	FPmcTraversalArrival Arrival;
	Arrival.Dist2DCm = 80.f;
	Arrival.FeetDistZCm = 10.f;
	Arrival.bMovingOnGround = true;
	TestTrue(TEXT("Near, grounded, still"), HasArrived(Arrival, 150.f, 70.f));
	FPmcTraversalArrival Air = Arrival;
	Air.bMovingOnGround = false;
	TestFalse(TEXT("Review 095 item 6: in the air over the goal is no arrival"), HasArrived(Air, 150.f, 70.f));
	FPmcTraversalArrival Falling = Arrival;
	Falling.VelocityZ = -400.f;
	TestFalse(TEXT("Vertical speed (just landing / still falling)"), HasArrived(Falling, 150.f, 70.f));
	FPmcTraversalArrival Busy = Arrival;
	Busy.bBusyTraversing = true;
	TestFalse(TEXT("Mid mantle / ladder / drop settle"), HasArrived(Busy, 150.f, 70.f));
	FPmcTraversalArrival High = Arrival;
	High.FeetDistZCm = 120.f;
	TestFalse(TEXT("Wrong level"), HasArrived(High, 150.f, 70.f));
	return true;
}

PMC_TRAVERSAL_TEST(FPmcTraversalBarTest, "BarAndGroups")
bool FPmcTraversalBarTest::RunTest(const FString&)
{
	using namespace PmcTraversalRules;
	TestTrue(TEXT("19 of 20 = 95 % passes"), MeetsBar(19, 20));
	TestFalse(TEXT("18 of 20 fails"), MeetsBar(18, 20));
	TestFalse(TEXT("Nothing sampled never passes (a typo'd filter)"), MeetsBar(0, 0));
	TestEqual(TEXT("Rate"), RatePercent(1, 4), 25.0);
	TestEqual(TEXT("Rate of nothing"), RatePercent(0, 0), 0.0);

	const TArray<FString> Keys = { TEXT("mantles: stuck"), TEXT("drops: bypassed"), TEXT("mantles: stuck"), TEXT("ladders: timed out"),
		TEXT("drops: bypassed"), TEXT("mantles: stuck") };
	const TArray<TPair<FString, int32>> Groups = GroupCounts(Keys);
	if (TestEqual(TEXT("Three groups"), Groups.Num(), 3))
	{
		TestEqual(TEXT("Most first"), Groups[0].Key, FString(TEXT("mantles: stuck")));
		TestEqual(TEXT("Its count"), Groups[0].Value, 3);
		TestEqual(TEXT("Then the pair"), Groups[1].Key, FString(TEXT("drops: bypassed")));
		TestEqual(TEXT("Single last"), Groups[2].Value, 1);
	}
	TestEqual(TEXT("No keys -> no groups"), GroupCounts({}).Num(), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
