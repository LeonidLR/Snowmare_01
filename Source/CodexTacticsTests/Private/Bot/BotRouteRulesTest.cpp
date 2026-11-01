#include "Misc/AutomationTest.h"
#include "Bot/BotRouteRules.h"
#include "Data/LevelJsonRules.h"
#include "Data/WaveConfigTypes.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

// The playtest bot's planned route (Bot/BotRouteRules.h; Training Ground "Combat lab", game designer 2026-11-01,
// docs/design/COMBAT_LAB.md section 7): the level JSON "bot_route" block and the run's pure bookkeeping (reached / stuck / skipped, laps).

namespace BotRouteTest
{
	/** A square loop of 4 waypoints 10 m apart (the first at the origin), 1 retry. */
	FBotRouteConfig Square(bool bLoop = true)
	{
		FBotRouteConfig Config;
		Config.Name = TEXT("square");
		Config.bLoop = bLoop;
		Config.MaxRetries = 1;
		const FVector2D Corners[] = { { 0.0, 0.0 }, { 1000.0, 0.0 }, { 1000.0, 1000.0 }, { 0.0, 1000.0 } };
		int32 Index = 0;
		for (const FVector2D& Corner : Corners)
		{
			FBotRouteWaypoint Waypoint;
			Waypoint.At = Corner;
			Waypoint.Label = FString::Printf(TEXT("c%d"), Index++);
			Config.Points.Add(Waypoint);
		}
		return Config;
	}

	FBotRouteInput RouteAt(float Time, const FVector2D& Leader, bool bPaused = false)
	{
		FBotRouteInput In;
		In.Time = Time;
		In.Leader = Leader;
		In.bPaused = bPaused;
		return In;
	}

	TSharedPtr<FJsonObject> ParseRouteObject(const FString& Json)
	{
		TSharedPtr<FJsonObject> Root;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root);
		return Root;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBotRouteParseTest, "CodexTactics.Bot.Route.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBotRouteParseTest::RunTest(const FString&)
{
	FBotRouteConfig Config;
	FString Error;
	const TSharedPtr<FJsonObject> Good = BotRouteTest::ParseRouteObject(TEXT(R"({"name": "r", "loop": false, "reach_radius_cm": 300, "max_retries": 4, "fight_hold_sec": 0,
		"points": [{"at": [0, 0], "label": "start"}, {"at": [500, 0], "action": "WALL", "wall": [500, 300], "hold_sec": 2}, {"at": [900, 0], "action": "climb"}, {"at": [1200, 0]}]})"));
	TestTrue(TEXT("parses"), Good.IsValid() && BotRouteRules::Parse(*Good, Config, &Error));
	TestEqual(TEXT("name"), Config.Name, FString(TEXT("r")));
	TestFalse(TEXT("loop false"), Config.bLoop);
	TestEqual(TEXT("reach radius"), Config.ReachRadiusCm, 300.f);
	TestEqual(TEXT("retries"), Config.MaxRetries, 4);
	TestEqual(TEXT("fight hold 0 = walk on fighting"), Config.FightHoldSeconds, 0.f);
	TestEqual(TEXT("stuck default"), Config.StuckAfterSeconds, FBotRouteConfig().StuckAfterSeconds);
	TestEqual(TEXT("4 points"), Config.Points.Num(), 4);
	if (Config.Points.Num() == 4)
	{
		TestTrue(TEXT("wall action (any case)"), Config.Points[1].Action == EBotRouteAction::Wall);
		TestTrue(TEXT("wall point"), Config.Points[1].WallPoint.Equals(FVector2D(500.0, 300.0)));
		TestEqual(TEXT("hold"), Config.Points[1].HoldSeconds, 2.f);
		TestTrue(TEXT("climb"), Config.Points[2].Action == EBotRouteAction::Climb);
		TestEqual(TEXT("no label -> #index"), Config.Points[3].Label, FString(TEXT("#3")));
	}
	TestEqual(TEXT("lap length of an open route"), Config.LapLengthCm(), 1200.f);
	Config.bLoop = true;
	TestEqual(TEXT("a loop adds the closing leg"), Config.LapLengthCm(), 2400.f);

	const TCHAR* Bad[] = {
		TEXT(R"({"points": [{"at": [0, 0]}]})"),
		TEXT(R"({"points": [{"at": [0, 0]}, {"at": [1]}]})"),
		TEXT(R"({"points": [{"at": [0, 0]}, {"at": [1, 1], "action": "fly"}]})"),
		TEXT(R"({"points": [{"at": [0, 0]}, {"at": [1, 1], "action": "wall"}]})"),
	};
	for (const TCHAR* Json : Bad)
	{
		FBotRouteConfig Out;
		Error.Reset();
		const TSharedPtr<FJsonObject> Object = BotRouteTest::ParseRouteObject(Json);
		TestFalse(FString::Printf(TEXT("refused: %s"), Json), Object.IsValid() && BotRouteRules::Parse(*Object, Out, &Error));
		TestFalse(TEXT("... with a reason"), Error.IsEmpty());
	}

	// Through the level parser: a bad route fails the level; none leaves the route invalid.
	FLevelCombatConfig Level;
	TestTrue(TEXT("a level without a route parses"), LevelJsonRules::ParseLevel(TEXT(R"({"level_id": "x"})"), TEXT("x"), Level, Error));
	TestFalse(TEXT("... its route is invalid"), Level.BotRoute.IsValid());
	TestFalse(TEXT("a level with a broken route fails"), LevelJsonRules::ParseLevel(TEXT(R"({"level_id": "x", "bot_route": {"points": []}})"), TEXT("x"), Level, Error));
	TestTrue(TEXT("... naming bot_route"), Error.Contains(TEXT("bot_route")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBotRouteWalkTest, "CodexTactics.Bot.Route.WalkAndLaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBotRouteWalkTest::RunTest(const FString&)
{
	using namespace BotRouteTest;
	FBotRouteTracker Tracker;
	Tracker.Reset(Square(), 0.f, FVector2D(-500.0, 0.0));
	TestTrue(TEXT("the first tick orders the move to the start"), Tracker.Tick(RouteAt(0.1f, FVector2D(-500.0, 0.0))) == EBotRouteCommand::Move);
	TestEqual(TEXT("... the start is #0"), Tracker.GetIndex(), 0);
	TestTrue(TEXT("walking: nothing to do"), Tracker.Tick(RouteAt(1.f, FVector2D(-300.0, 0.0))) == EBotRouteCommand::None);
	TestTrue(TEXT("the start reached -> move to #1"), Tracker.Tick(RouteAt(2.f, FVector2D(-100.0, 0.0))) == EBotRouteCommand::Move);
	TestEqual(TEXT("... #1"), Tracker.GetIndex(), 1);
	TestEqual(TEXT("the start is no part of the lap"), Tracker.GetLapReached(), 0);
	const FVector2D Corners[] = { { 1000.0, 0.0 }, { 1000.0, 1000.0 }, { 0.0, 1000.0 }, { 0.0, 0.0 } };
	float Time = 2.f;
	for (const FVector2D& Corner : Corners)
	{
		Time += 5.f;
		TestTrue(TEXT("each corner reached -> the next move"), Tracker.Tick(RouteAt(Time, Corner)) == EBotRouteCommand::Move);
	}
	TestEqual(TEXT("one lap after #1..#3 and back at #0"), Tracker.GetLaps().Num(), 1);
	TestTrue(TEXT("the route is completed"), Tracker.IsRouteCompleted());
	TestEqual(TEXT("progress 1"), Tracker.GetProgress(), 1.f);
	if (Tracker.GetLaps().Num() == 1)
	{
		TestEqual(TEXT("lap: 4 reached"), Tracker.GetLaps()[0].Reached, 4);
		TestEqual(TEXT("lap: 20 s"), Tracker.GetLaps()[0].Seconds, 20.f);
	}
	TestEqual(TEXT("the loop goes on at #1"), Tracker.GetIndex(), 1);
	TestTrue(TEXT("distance walked"), Tracker.GetDistanceCm() > 4000.f);

	// An open route finishes at its last point.
	FBotRouteTracker Open;
	Open.Reset(Square(false), 0.f, FVector2D::ZeroVector);
	Open.Tick(RouteAt(0.1f, FVector2D::ZeroVector));
	TestTrue(TEXT("open: start reached"), Open.Tick(RouteAt(0.2f, FVector2D::ZeroVector)) == EBotRouteCommand::Move);
	Open.Tick(RouteAt(5.f, FVector2D(1000.0, 0.0)));
	Open.Tick(RouteAt(10.f, FVector2D(1000.0, 1000.0)));
	TestTrue(TEXT("open: the last point finishes"), Open.Tick(RouteAt(15.f, FVector2D(0.0, 1000.0))) == EBotRouteCommand::Finished);
	TestTrue(TEXT("open: completed"), Open.IsRouteCompleted());
	TestTrue(TEXT("open: no more commands"), Open.Tick(RouteAt(16.f, FVector2D(0.0, 1000.0))) == EBotRouteCommand::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBotRouteStuckTest, "CodexTactics.Bot.Route.StuckAndSkip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBotRouteStuckTest::RunTest(const FString&)
{
	using namespace BotRouteTest;
	FBotRouteTracker Tracker;
	Tracker.Reset(Square(), 0.f, FVector2D::ZeroVector);
	Tracker.Tick(RouteAt(0.1f, FVector2D::ZeroVector));
	Tracker.Tick(RouteAt(0.2f, FVector2D::ZeroVector)); // start reached, #1 ordered
	Tracker.NotifyOrder(TEXT("move to #1"));
	const float Stuck = FBotRouteConfig().StuckAfterSeconds;
	TestTrue(TEXT("standing still: not stuck yet"), Tracker.Tick(RouteAt(0.2f + Stuck - 1.f, FVector2D(50.0, 0.0))) == EBotRouteCommand::None);
	TestTrue(TEXT("standing still past the limit: retry"), Tracker.Tick(RouteAt(0.2f + Stuck + 0.1f, FVector2D(50.0, 0.0))) == EBotRouteCommand::Retry);
	TestEqual(TEXT("one stuck event"), Tracker.GetStucks().Num(), 1);
	if (Tracker.GetStucks().Num() == 1)
	{
		const FBotRouteStuck& Event = Tracker.GetStucks()[0];
		TestEqual(TEXT("reason no_move"), Event.Reason, FString(TEXT("no_move")));
		TestEqual(TEXT("where"), Event.Position.X, 50.0);
		TestEqual(TEXT("the last order"), Event.LastOrder, FString(TEXT("move to #1")));
		TestEqual(TEXT("which waypoint"), Event.Label, FString(TEXT("c1")));
		TestFalse(TEXT("not skipped yet"), Event.bSkipped);
	}
	// A pause (a fight hold, a knockdown) stops the clock.
	float Time = 0.2f + Stuck + 0.1f;
	for (int32 Each = 0; Each < 30; ++Each)
	{
		Time += 1.f;
		TestTrue(TEXT("paused: never stuck"), Tracker.Tick(RouteAt(Time, FVector2D(50.0, 0.0), true)) == EBotRouteCommand::None);
	}
	// Retries used up (1): the next stuck gives the waypoint up.
	TestTrue(TEXT("stuck again: skipped -> move to #2"), Tracker.Tick(RouteAt(Time + Stuck + 0.1f, FVector2D(50.0, 0.0))) == EBotRouteCommand::Move);
	TestEqual(TEXT("now #2"), Tracker.GetIndex(), 2);
	TestEqual(TEXT("skipped 1"), Tracker.GetTotalSkipped(), 1);
	TestTrue(TEXT("the event says skipped"), Tracker.GetStucks().Num() == 2 && Tracker.GetStucks()[1].bSkipped);

	// Moving but never arriving (circling round an obstacle): the leg time limit.
	Time += Stuck + 0.1f;
	bool bTimedOut = false;
	for (int32 Each = 0; Each < 200 && !bTimedOut; ++Each)
	{
		Time += 1.f;
		const FVector2D Circle(50.0 + 400.0 * FMath::Cos(Each * 0.7), 400.0 * FMath::Sin(Each * 0.7));
		bTimedOut = Tracker.Tick(RouteAt(Time, Circle)) == EBotRouteCommand::Retry;
	}
	TestTrue(TEXT("circling: a leg timeout"), bTimedOut && Tracker.GetStucks().Last().Reason == TEXT("leg_timeout"));

	// A lap with a skip is no completed route; the progress counts the reached waypoints.
	FBotRouteTracker Lap;
	Lap.Reset(Square(), 0.f, FVector2D::ZeroVector);
	Lap.Tick(RouteAt(0.1f, FVector2D::ZeroVector));
	Lap.Tick(RouteAt(0.2f, FVector2D::ZeroVector));
	Lap.Tick(RouteAt(5.f, FVector2D(1000.0, 0.0)));                                          // #1 reached
	TestTrue(TEXT("an order that cannot be given: a stuck event at once"),
		Lap.NotifyOrderFailed(RouteAt(5.1f, FVector2D(1000.0, 0.0)), TEXT("no_ground_hit")) == EBotRouteCommand::Retry);
	TestTrue(TEXT("... again: skipped"), Lap.NotifyOrderFailed(RouteAt(5.2f, FVector2D(1000.0, 0.0)), TEXT("no_ground_hit")) == EBotRouteCommand::Move);
	TestTrue(TEXT("... the reason"), Lap.GetStucks().Last().Reason == TEXT("order_failed:no_ground_hit"));
	Lap.Tick(RouteAt(9.f, FVector2D(0.0, 1000.0)));                                          // #3
	Lap.Tick(RouteAt(12.f, FVector2D(0.0, 0.0)));                                            // #0: lap over
	TestEqual(TEXT("a lap"), Lap.GetLaps().Num(), 1);
	TestFalse(TEXT("with a skip: not completed"), Lap.IsRouteCompleted());
	TestEqual(TEXT("progress 3 of 4"), Lap.GetProgress(), 0.75f);
	TestEqual(TEXT("furthest of the first lap: #0 (back at the start)"), Lap.GetFurthestFirstLap(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBotRouteActionTest, "CodexTactics.Bot.Route.Actions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBotRouteActionTest::RunTest(const FString&)
{
	using namespace BotRouteTest;
	FBotRouteConfig Config = Square();
	Config.Points[1].Action = EBotRouteAction::Wall;
	Config.Points[1].WallPoint = FVector2D(1300.0, 0.0);
	Config.Points[2].Action = EBotRouteAction::Climb;
	FBotRouteTracker Tracker;
	Tracker.Reset(Config, 0.f, FVector2D::ZeroVector);
	Tracker.Tick(RouteAt(0.1f, FVector2D::ZeroVector));
	Tracker.Tick(RouteAt(0.2f, FVector2D::ZeroVector));
	TestTrue(TEXT("the wall waypoint reached: start the action"), Tracker.Tick(RouteAt(5.f, FVector2D(1000.0, 0.0))) == EBotRouteCommand::StartAction);
	TestTrue(TEXT("in the action"), Tracker.IsInAction());
	TestTrue(TEXT("the action runs: no command, no stuck however long"), Tracker.Tick(RouteAt(200.f, FVector2D(1000.0, 0.0))) == EBotRouteCommand::None);
	TestTrue(TEXT("the action done: move to #2"), Tracker.FinishAction(RouteAt(201.f, FVector2D(1000.0, 0.0)), true, FString()) == EBotRouteCommand::Move);
	TestEqual(TEXT("reached counts it"), Tracker.GetLapReached(), 1);
	TestTrue(TEXT("the climb waypoint: start the action"), Tracker.Tick(RouteAt(205.f, FVector2D(1000.0, 1000.0))) == EBotRouteCommand::StartAction);
	TestTrue(TEXT("a failed action: on to #3"), Tracker.FinishAction(RouteAt(250.f, FVector2D(1000.0, 1000.0)), false, TEXT("climb_up_timeout")) == EBotRouteCommand::Move);
	TestEqual(TEXT("... the waypoint skipped"), Tracker.GetLapSkipped(), 1);
	TestTrue(TEXT("... a stuck event with the reason"), Tracker.GetStucks().Num() == 1 && Tracker.GetStucks()[0].Reason == TEXT("action_failed:climb_up_timeout")
		&& Tracker.GetStucks()[0].bSkipped);
	TestTrue(TEXT("FinishAction outside an action does nothing"), Tracker.FinishAction(RouteAt(251.f, FVector2D::ZeroVector), true, FString()) == EBotRouteCommand::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBotRouteCombatLabFileTest, "CodexTactics.Bot.Route.CombatLabRoute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBotRouteCombatLabFileTest::RunTest(const FString&)
{
	// The combat lab's route (L_FormationLabCombat.json "bot_route"): a loop through walls, corners, doorways and the four towers.
	FLevelCombatConfig Level;
	FString Error;
	if (!TestTrue(FString::Printf(TEXT("L_FormationLabCombat.json loads (%s)"), *Error), LevelJsonRules::LoadLevel(TEXT("L_FormationLabCombat.json"), Level, Error)))
	{
		return false;
	}
	const FBotRouteConfig& Route = Level.BotRoute;
	TestTrue(TEXT("it has a route"), Route.IsValid());
	TestTrue(TEXT("a loop"), Route.bLoop);
	int32 Walls = 0;
	int32 Climbs = 0;
	for (const FBotRouteWaypoint& Each : Route.Points)
	{
		Walls += Each.Action == EBotRouteAction::Wall ? 1 : 0;
		Climbs += Each.Action == EBotRouteAction::Climb ? 1 : 0;
		TestTrue(FString::Printf(TEXT("%s inside the lab (+-6000)"), *Each.Label), FMath::Abs(Each.At.X) < 6000.0 && FMath::Abs(Each.At.Y) < 6000.0);
	}
	TestTrue(FString::Printf(TEXT("wall hugs on the way (%d)"), Walls), Walls >= 2);
	TestEqual(TEXT("every tower climbed"), Climbs, 4);
	TestTrue(FString::Printf(TEXT("a lap of 300-600 m (%.0f m)"), Route.LapLengthCm() / 100.f), Route.LapLengthCm() >= 30000.f && Route.LapLengthCm() <= 60000.f);
	return true;
}

#endif
