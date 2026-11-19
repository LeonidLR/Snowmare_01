#include "Misc/AutomationTest.h"
#include "AI/PmcArenaScriptRules.h"
#include "Dom/JsonObject.h"

#if WITH_DEV_AUTOMATION_TESTS

// smart-enemies T-03 (docs/design/gdd/smart-enemies-roadmap.md): the arena's scripted opponent (Opponent=Script Script=run) - the pure half:
// the mode names, the "pmc_arena" keys and the route -> waypoints rule the follower walks. UE-only, no Godot reference.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcArenaScriptOpponentTest, "CodexTactics.AI.PmcArena.ScriptOpponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcArenaScriptOpponentTest::RunTest(const FString&)
{
	using namespace PmcArenaScriptRules;

	// --- The modes: run / bound parse; bound is T-07's seam and plays as run until it is filled.
	EMode Mode = EMode::Bound;
	TestTrue(TEXT("run parses"), ParseMode(TEXT("Run"), Mode));
	TestTrue(TEXT("run is Run"), Mode == EMode::Run);
	TestTrue(TEXT("bound parses (case-insensitive)"), ParseMode(TEXT("BOUND"), Mode));
	TestTrue(TEXT("bound is Bound"), Mode == EMode::Bound);
	TestFalse(TEXT("an unknown mode does not parse"), ParseMode(TEXT("sneak"), Mode));
	TestTrue(TEXT("an unknown mode leaves the value"), Mode == EMode::Bound);
	TestEqual(TEXT("run's name"), FString(ModeName(EMode::Run)), FString(TEXT("run")));
	TestEqual(TEXT("bound's name"), FString(ModeName(EMode::Bound)), FString(TEXT("bound")));
	bool bFellBack = true;
	TestTrue(TEXT("run resolves to run"), ResolveMode(EMode::Run, bFellBack) == EMode::Run);
	TestFalse(TEXT("run does not fall back"), bFellBack);
	TestTrue(TEXT("bound falls back to run until T-07"), ResolveMode(EMode::Bound, bFellBack) == EMode::Run);
	TestTrue(TEXT("and says so"), bFellBack);

	// --- The data keys: metres -> cm, clamped, a missing key keeps the code default.
	{
		FConfig Defaults;
		TestEqual(TEXT("default waypoint spacing 6 m"), Defaults.WaypointCm, 600.f);
		TestEqual(TEXT("default arrival 2 m"), Defaults.ArriveCm, 200.f);
		TestEqual(TEXT("default level 1.5 m"), Defaults.LevelCm, 150.f);
		TestFalse(TEXT("default walks (a sprinting operative cannot fire)"), Defaults.bSprint);
		FConfig Config;
		FJsonObject Empty;
		ReadJson(Empty, Config);
		TestEqual(TEXT("no keys: spacing kept"), Config.WaypointCm, 600.f);
		FJsonObject Arena;
		Arena.SetNumberField(TEXT("script_waypoint_m"), 8.0);
		Arena.SetNumberField(TEXT("script_arrive_m"), 1.5);
		Arena.SetNumberField(TEXT("script_level_m"), 2.0);
		Arena.SetBoolField(TEXT("script_sprint"), true);
		ReadJson(Arena, Config);
		TestEqual(TEXT("waypoint 8 m"), Config.WaypointCm, 800.f);
		TestEqual(TEXT("arrive 1.5 m"), Config.ArriveCm, 150.f);
		TestEqual(TEXT("level 2 m"), Config.LevelCm, 200.f);
		TestTrue(TEXT("sprint on"), Config.bSprint);
		FJsonObject Wild;
		Wild.SetNumberField(TEXT("script_waypoint_m"), 0.0);
		Wild.SetNumberField(TEXT("script_arrive_m"), 99.0);
		FConfig Clamped;
		ReadJson(Wild, Clamped);
		TestEqual(TEXT("spacing clamped up to 1 m"), Clamped.WaypointCm, 100.f);
		TestEqual(TEXT("arrival clamped down to 10 m"), Clamped.ArriveCm, 1000.f);
	}

	// --- The waypoints: a straight 20 m route, 6 m apart: 6, 12 and the end (the 18 m point is 2 m from the end: merged into it).
	{
		const TArray<FVector> Route = { FVector(0.f, 0.f, 0.f), FVector(2000.f, 0.f, 0.f) };
		const TArray<FVector> Waypoints = WaypointsFromRoute(Route, 600.f);
		TestEqual(TEXT("straight 20 m / 6 m: three waypoints"), Waypoints.Num(), 3);
		if (Waypoints.Num() == 3)
		{
			TestTrue(TEXT("first at 6 m"), Waypoints[0].Equals(FVector(600.f, 0.f, 0.f), 0.01));
			TestTrue(TEXT("second at 12 m"), Waypoints[1].Equals(FVector(1200.f, 0.f, 0.f), 0.01));
			TestTrue(TEXT("the last is the route's end exactly"), Waypoints[2] == Route.Last());
		}
		TestEqual(TEXT("the route's start is no waypoint"), Waypoints.Contains(Route[0]) ? 1 : 0, 0);
	}
	// An 18 m route: 6, 12, then the end (not a doubled 18 m point).
	{
		const TArray<FVector> Route = { FVector(0.f, 0.f, 0.f), FVector(1800.f, 0.f, 0.f) };
		const TArray<FVector> Waypoints = WaypointsFromRoute(Route, 600.f);
		TestEqual(TEXT("18 m / 6 m: 6, 12, end"), Waypoints.Num(), 3);
		if (Waypoints.Num() == 3)
		{
			TestTrue(TEXT("12 m"), Waypoints[1].Equals(FVector(1200.f, 0.f, 0.f), 0.01));
			TestTrue(TEXT("end"), Waypoints[2] == Route.Last());
		}
	}
	// A corner: the spacing is measured along the path, not in a straight line (10 m east, then 10 m north, 6 m apart).
	{
		const TArray<FVector> Route = { FVector(0.f, 0.f, 0.f), FVector(1000.f, 0.f, 0.f), FVector(1000.f, 1000.f, 0.f) };
		const TArray<FVector> Waypoints = WaypointsFromRoute(Route, 600.f);
		TestEqual(TEXT("L route 20 m / 6 m: three waypoints"), Waypoints.Num(), 3);
		if (Waypoints.Num() == 3)
		{
			TestTrue(TEXT("6 m on the first leg"), Waypoints[0].Equals(FVector(600.f, 0.f, 0.f), 0.01));
			TestTrue(TEXT("12 m is 2 m up the second leg"), Waypoints[1].Equals(FVector(1000.f, 200.f, 0.f), 0.01));
			TestTrue(TEXT("end"), Waypoints[2] == Route.Last());
		}
		TestEqual(TEXT("route length 20 m"), RouteLength(Route), 2000.f);
	}
	// A ramp: the height is interpolated and counts in the length (3D polyline).
	{
		const TArray<FVector> Route = { FVector(0.f, 0.f, 0.f), FVector(0.f, 0.f, 1200.f) };
		const TArray<FVector> Waypoints = WaypointsFromRoute(Route, 600.f);
		TestEqual(TEXT("a 12 m climb / 6 m: the 6 m point and the end"), Waypoints.Num(), 2);
		if (Waypoints.Num() == 2)
		{
			TestTrue(TEXT("6 m up"), Waypoints[0].Equals(FVector(0.f, 0.f, 600.f), 0.01));
		}
	}
	// Degenerate routes.
	{
		TestEqual(TEXT("no route: no waypoints"), WaypointsFromRoute(TArray<FVector>(), 600.f).Num(), 0);
		const TArray<FVector> One = { FVector(5.f, 6.f, 7.f) };
		TestEqual(TEXT("one point: that point"), WaypointsFromRoute(One, 600.f).Num(), 1);
		const TArray<FVector> Doubled = { FVector(0.f), FVector(0.f), FVector(1000.f, 0.f, 0.f), FVector(1000.f, 0.f, 0.f) };
		const TArray<FVector> FromDoubled = WaypointsFromRoute(Doubled, 600.f);
		TestEqual(TEXT("repeated corners make no zero-length legs: 6 m + the end"), FromDoubled.Num(), 2);
		const TArray<FVector> Short = { FVector(0.f), FVector(300.f, 0.f, 0.f) };
		const TArray<FVector> FromShort = WaypointsFromRoute(Short, 600.f);
		TestEqual(TEXT("a route shorter than the spacing: just the end"), FromShort.Num(), 1);
		const TArray<FVector> Long = { FVector(0.f), FVector(5000.f, 0.f, 0.f) };
		const TArray<FVector> NoSpacing = WaypointsFromRoute(Long, 0.f);
		TestEqual(TEXT("no spacing: just the end"), NoSpacing.Num(), 1);
		if (NoSpacing.Num() == 1)
		{
			TestTrue(TEXT("and it is the end"), NoSpacing[0] == Long.Last());
		}
	}
	// Spacing on a long winding route: every interior leg between waypoints is the spacing (path length), and the same route gives the
	// same list every time (the same seed gives the same walk: nothing here is random).
	{
		const TArray<FVector> Route = { FVector(0.f), FVector(3000.f, 0.f, 0.f), FVector(3000.f, 4000.f, 0.f), FVector(500.f, 4000.f, 90.f), FVector(500.f, 6500.f, 90.f) };
		const TArray<FVector> Copy = Route;
		const TArray<FVector> First = WaypointsFromRoute(Route, 600.f);
		const TArray<FVector> Second = WaypointsFromRoute(Route, 600.f);
		TestTrue(TEXT("the route is not modified"), Route == Copy);
		TestTrue(TEXT("the same route gives the same waypoints"), First == Second);
		TestTrue(TEXT("a copy of the route too"), WaypointsFromRoute(Copy, 600.f) == First);
		TestTrue(TEXT("the end is the last waypoint"), !First.IsEmpty() && First.Last() == Route.Last());
		// 3000 + 4000 + 2501.6 (2500 cm over a 90 cm rise) + 2500 = ~12002 cm: 19 interior waypoints (the 20th spacing is within half of the end) + the end.
		const float Total = RouteLength(Route);
		const int32 Expected = FMath::FloorToInt((Total - 300.f) / 600.f) + 1;
		TestEqual(TEXT("one waypoint per spacing plus the end"), First.Num(), Expected);
		// Waypoints lie on the polyline: each is within a hair of the segment it was cut from (distance to the path ~ 0).
		bool bOnPath = true;
		for (int32 Index = 0; Index + 1 < First.Num(); ++Index)
		{
			double Best = TNumericLimits<double>::Max();
			for (int32 Leg = 1; Leg < Route.Num(); ++Leg)
			{
				Best = FMath::Min(Best, FMath::PointDistToSegment(First[Index], Route[Leg - 1], Route[Leg]));
			}
			bOnPath &= Best < 0.5;
		}
		TestTrue(TEXT("every waypoint lies on the route"), bOnPath);
	}

	// --- The follower's progress: standing on the start, 0; reaching a waypoint advances; the goal = Num; never backwards.
	{
		const TArray<FVector> Waypoints = { FVector(600.f, 0.f, 0.f), FVector(1200.f, 0.f, 0.f), FVector(2000.f, 0.f, 0.f) };
		const FConfig Config; // arrive 2 m, level 1.5 m
		TestEqual(TEXT("at the start: still the first waypoint"), AdvanceWaypoint(Waypoints, 0, FVector(0.f), Config), 0);
		TestEqual(TEXT("1.5 m short of the first: reached, next is the second"), AdvanceWaypoint(Waypoints, 0, FVector(450.f, 0.f, 0.f), Config), 1);
		TestEqual(TEXT("2.5 m short: not yet"), AdvanceWaypoint(Waypoints, 0, FVector(350.f, 0.f, 0.f), Config), 0);
		TestEqual(TEXT("on the second while heading to it"), AdvanceWaypoint(Waypoints, 1, FVector(1200.f, 0.f, 0.f), Config), 2);
		TestEqual(TEXT("on the goal: the goal is reached (Num)"), AdvanceWaypoint(Waypoints, 2, FVector(2000.f, 0.f, 0.f), Config), 3);
		TestEqual(TEXT("past the goal stays Num"), AdvanceWaypoint(Waypoints, 3, FVector(2000.f, 0.f, 0.f), Config), 3);
		TestEqual(TEXT("an index never goes back (he stands at the start again)"), AdvanceWaypoint(Waypoints, 2, FVector(0.f), Config), 2);
		TestEqual(TEXT("a negative index is clamped"), AdvanceWaypoint(Waypoints, -4, FVector(0.f), Config), 0);
		TestEqual(TEXT("a man on a floor above (3 m up) has not reached it"), AdvanceWaypoint(Waypoints, 0, FVector(600.f, 0.f, 300.f), Config), 0);
		TestEqual(TEXT("within the level tolerance he has"), AdvanceWaypoint(Waypoints, 0, FVector(600.f, 0.f, 100.f), Config), 1);
		// Two waypoints one on top of the other: both passed in one step.
		const TArray<FVector> Stacked = { FVector(100.f, 0.f, 0.f), FVector(150.f, 0.f, 0.f), FVector(900.f, 0.f, 0.f) };
		TestEqual(TEXT("stacked waypoints are passed together"), AdvanceWaypoint(Stacked, 0, FVector(120.f, 0.f, 0.f), Config), 2);
		TestEqual(TEXT("an empty list: index 0 = the goal"), AdvanceWaypoint(TArray<FVector>(), 0, FVector(0.f), Config), 0);
	}

	// --- A whole walk: a man stepping along the waypoints of a route arrives at its end (the follower's contract), the same way twice.
	{
		const TArray<FVector> Route = { FVector(0.f), FVector(2500.f, 0.f, 0.f), FVector(2500.f, 1800.f, 0.f) };
		const FConfig Config;
		auto Walk = [&Route, &Config](int32& OutSteps)
		{
			const TArray<FVector> Waypoints = WaypointsFromRoute(Route, Config.WaypointCm);
			int32 Next = 0;
			FVector Feet = Route[0];
			OutSteps = 0;
			// A "man" who walks straight to his waypoint 1 m a step (the order the follower gives him).
			while (Next < Waypoints.Num() && OutSteps < 100000)
			{
				const FVector To = Waypoints[Next] - Feet;
				Feet += To.GetSafeNormal() * FMath::Min(100.0, To.Size());
				Next = AdvanceWaypoint(Waypoints, Next, Feet, Config);
				++OutSteps;
			}
			return Next == Waypoints.Num() && Feet.Equals(Route.Last(), Config.ArriveCm);
		};
		int32 StepsA = 0;
		int32 StepsB = 0;
		TestTrue(TEXT("the man walks the route to its end"), Walk(StepsA));
		TestTrue(TEXT("and again"), Walk(StepsB));
		TestEqual(TEXT("the same walk: the same number of steps"), StepsA, StepsB);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
