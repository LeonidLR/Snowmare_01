#include "Misc/AutomationTest.h"
#include "AI/AmbushWatchRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// The combat lab's ambushers (game designer 2026-11-01, docs/design/COMBAT_LAB.md "Ambushers"): is the ambusher's side watched by the squad?

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAmbushWatchSectorTest, "CodexTactics.AI.AmbushWatch.Sector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAmbushWatchSectorTest::RunTest(const FString&)
{
	FAmbushWatchParams Params;
	Params.WatchHalfAngleDeg = 45.f;
	Params.WatchRangeCm = 5000.f;
	// One man at the origin looking +X (yaw 0).
	const TArray<FAmbushWatcher> One = { { FVector2D(0.0, 0.0), 0.f } };
	TestTrue(TEXT("Straight ahead is watched"), AmbushWatchRules::IsWatched(One, FVector2D(1000.0, 0.0), Params));
	TestTrue(TEXT("40 deg off his look is watched"), AmbushWatchRules::IsWatched(One, FVector2D(1000.0, 839.0), Params));
	TestFalse(TEXT("50 deg off his look is not"), AmbushWatchRules::IsWatched(One, FVector2D(1000.0, 1192.0), Params));
	TestFalse(TEXT("Behind his back is not watched (he turned his back)"), AmbushWatchRules::IsWatched(One, FVector2D(-1000.0, 0.0), Params));
	TestFalse(TEXT("Beyond the watch range nobody watches"), AmbushWatchRules::IsWatched(One, FVector2D(6000.0, 0.0), Params));
	TestEqual(TEXT("Watcher index"), AmbushWatchRules::FindWatcher(One, FVector2D(1000.0, 0.0), Params), 0);
	TestEqual(TEXT("Margin: straight ahead -45"), AmbushWatchRules::WatchMarginDeg(One, FVector2D(1000.0, 0.0), Params), -45.f, 0.01f);
	TestEqual(TEXT("Margin: behind +135"), AmbushWatchRules::WatchMarginDeg(One, FVector2D(-1000.0, 0.0), Params), 135.f, 0.01f);
	TestEqual(TEXT("Margin: nobody in range 180"), AmbushWatchRules::WatchMarginDeg(One, FVector2D(9000.0, 0.0), Params), 180.f, 0.01f);
	// The bearing is per watcher: a rear guard looking back covers the squad's back.
	const TArray<FAmbushWatcher> Diamond = { { FVector2D(0.0, 0.0), 0.f }, { FVector2D(-560.0, 0.0), 180.f } };
	TestTrue(TEXT("The rear guard watches the back"), AmbushWatchRules::IsWatched(Diamond, FVector2D(-2000.0, 100.0), Params));
	TestEqual(TEXT("... it is him"), AmbushWatchRules::FindWatcher(Diamond, FVector2D(-2000.0, 100.0), Params), 1);
	TestFalse(TEXT("The flank between them is open"), AmbushWatchRules::IsWatched(Diamond, FVector2D(-280.0, 1500.0), Params));
	// Angles wrap.
	TestEqual(TEXT("Angle off wraps (170 vs -170 = 20)"), AmbushWatchRules::AngleOffDeg(170.f, -170.f), 20.f, 0.01f);
	TestEqual(TEXT("Bearing +Y = 90"), AmbushWatchRules::BearingDeg(FVector2D(0.0, 0.0), FVector2D(0.0, 10.0)), 90.f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAmbushWatchDecideTest, "CodexTactics.AI.AmbushWatch.Decide",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAmbushWatchDecideTest::RunTest(const FString&)
{
	FAmbushWatchParams Params;
	Params.WatchHalfAngleDeg = 45.f;
	Params.StrikeRangeCm = 1800.f;
	const TArray<FVector2D> Squad = { FVector2D(0.0, 0.0), FVector2D(-300.0, 200.0) };
	const TArray<FAmbushWatcher> LookingEast = { { Squad[0], 0.f }, { Squad[1], 10.f } };
	TestTrue(TEXT("Squad beyond the strike range: wait"),
		AmbushWatchRules::Decide(LookingEast, Squad, FVector2D(-3000.0, 0.0), Params) == EAmbushDecision::Wait);
	TestTrue(TEXT("In range and watched: hold"), AmbushWatchRules::Decide(LookingEast, Squad, FVector2D(1500.0, 0.0), Params) == EAmbushDecision::Hold);
	TestTrue(TEXT("In range behind their backs: spring"),
		AmbushWatchRules::Decide(LookingEast, Squad, FVector2D(-1500.0, 0.0), Params) == EAmbushDecision::Spring);
	TestTrue(TEXT("The strike range counts the NEAREST operative"),
		AmbushWatchRules::Decide(LookingEast, Squad, FVector2D(-2050.0, 200.0), Params) == EAmbushDecision::Spring);
	TestTrue(TEXT("No squad: wait"), AmbushWatchRules::Decide({}, {}, FVector2D(0.0, 0.0), Params) == EAmbushDecision::Wait);
	// The same ambusher: watched -> the squad turns round -> unwatched.
	const TArray<FAmbushWatcher> LookingWest = { { Squad[0], 180.f }, { Squad[1], 170.f } };
	TestTrue(TEXT("Watched while they look west"), AmbushWatchRules::Decide(LookingWest, Squad, FVector2D(-1500.0, 0.0), Params) == EAmbushDecision::Hold);
	TestEqual(TEXT("Nearest distance"), AmbushWatchRules::NearestDistance(Squad, FVector2D(-300.0, 0.0)), 200.f, 0.01f);
	TestEqual(TEXT("Nearest of none = MAX"), AmbushWatchRules::NearestDistance({}, FVector2D(0.0, 0.0)), MAX_flt);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAmbushWatchGapTest, "CodexTactics.AI.AmbushWatch.WidestGap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAmbushWatchGapTest::RunTest(const FString&)
{
	FAmbushWatchParams Params;
	Params.WatchHalfAngleDeg = 45.f;
	const TOptional<float> Nobody = AmbushWatchRules::WidestUnwatchedBearing({}, Params);
	TestTrue(TEXT("No watchers: everything open"), Nobody.IsSet());
	const TOptional<float> East = AmbushWatchRules::WidestUnwatchedBearing({ { FVector2D::ZeroVector, 0.f } }, Params);
	TestTrue(TEXT("One look east: the gap is west"), East.IsSet() && AmbushWatchRules::AngleOffDeg(East.GetValue(), 180.f) < 0.1f);
	// East and north: covered -45..135, the widest gap 135..315, its middle 225 (= -135).
	const TOptional<float> Two = AmbushWatchRules::WidestUnwatchedBearing({ { FVector2D::ZeroVector, 0.f }, { FVector2D::ZeroVector, 90.f } }, Params);
	TestTrue(TEXT("East + north: the gap's middle is south-west"), Two.IsSet() && AmbushWatchRules::AngleOffDeg(Two.GetValue(), -135.f) < 0.1f);
	// A wrapping look (170 +- 45 crosses 180).
	const TOptional<float> Wrap = AmbushWatchRules::WidestUnwatchedBearing({ { FVector2D::ZeroVector, 170.f } }, Params);
	TestTrue(TEXT("Wrapping arc: the gap opposite (-10)"), Wrap.IsSet() && AmbushWatchRules::AngleOffDeg(Wrap.GetValue(), -10.f) < 0.1f);
	// Four looks 90 deg apart with 45 deg half angles cover the ring: no gap.
	const TArray<FAmbushWatcher> AllRound = { { FVector2D::ZeroVector, 0.f }, { FVector2D::ZeroVector, 90.f }, { FVector2D::ZeroVector, 180.f },
		{ FVector2D::ZeroVector, -90.f } };
	TestFalse(TEXT("All-round watch: no unwatched side"), AmbushWatchRules::WidestUnwatchedBearing(AllRound, Params).IsSet());
	Params.WatchHalfAngleDeg = 30.f;
	TestTrue(TEXT("Narrower sectors open gaps again"), AmbushWatchRules::WidestUnwatchedBearing(AllRound, Params).IsSet());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
