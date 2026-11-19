// CodexTactics.Squad.GiveWay.* - an idle squadmate steps out of a moving squadmate's path (the game designer's bug report 2026-11-02 item 7,
// "formation stuck at a ramp foot"): SquadGiveWayRules (who is idle, where the path crosses his capsule, where he steps to) and the
// squad_regroup.json "give_way" tuning. In-game check: LevelGenRampSmoke (Codex.Squad.GiveWay 0 = off). UE-only (no Godot reference).

#include "Characters/SquadGiveWayRules.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

#define SQUAD_GIVE_WAY_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Squad.GiveWay." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace SquadGiveWayTestLocal
{
	// Two 35 cm capsules + the 20 cm clearance.
	constexpr float CrossCm = 90.f;
}

SQUAD_GIVE_WAY_TEST(FSquadGiveWayIdleTest, "IdleOnly")
bool FSquadGiveWayIdleTest::RunTest(const FString&)
{
	using namespace SquadGiveWayRules;
	TestTrue(TEXT("standing, no order: idle"), IsIdle(FIdleState()));
	FIdleState State;
	State.bOwnMoveOrder = true;
	TestFalse(TEXT("an own move order"), IsIdle(State));
	State = FIdleState();
	State.bInCover = true;
	TestFalse(TEXT("in cover"), IsIdle(State));
	State = FIdleState();
	State.bCombatAction = true;
	TestFalse(TEXT("a combat action"), IsIdle(State));
	State = FIdleState();
	State.bMoving = true;
	TestFalse(TEXT("walking"), IsIdle(State));
	State = FIdleState();
	State.bHeld = true;
	TestFalse(TEXT("held (traffic / ramp file)"), IsIdle(State));
	State = FIdleState();
	State.bWalker = false;
	TestFalse(TEXT("on a ladder / knocked down / mounted"), IsIdle(State));
	State = FIdleState();
	State.bAtOwnPost = true;
	TestFalse(TEXT("squad mode, on the spot his group order gave him"), IsIdle(State));
	return true;
}

SQUAD_GIVE_WAY_TEST(FSquadGiveWayCrossingTest, "PathCrossesCapsule")
bool FSquadGiveWayCrossingTest::RunTest(const FString&)
{
	using namespace SquadGiveWayRules;
	const FSquadGiveWayTuning Tuning;
	// The mover walks +X to a stair foot at 300, then up +Y (the flight, rising).
	const TArray<FVector> Path{ FVector(0.0, 0.0, 0.0), FVector(300.0, 0.0, 0.0), FVector(300.0, 600.0, 300.0) };
	const FCrossing Mouth = FindCrossing(Path, FVector(250.0, -40.0, 0.0), SquadGiveWayTestLocal::CrossCm, Tuning);
	TestTrue(TEXT("40 cm right of his way 250 cm ahead: crosses"), Mouth.bCrosses);
	TestEqual(TEXT("the first leg"), Mouth.Segment, 0);
	TestEqual(TEXT("250 cm along"), Mouth.AlongCm, 250.f, 0.01f);
	TestEqual(TEXT("right of the path"), Mouth.Side, -1);
	TestFalse(TEXT("100 cm off the way: clear"), FindCrossing(Path, FVector(200.0, 100.0, 0.0), SquadGiveWayTestLocal::CrossCm, Tuning).bCrosses);
	TestFalse(TEXT("behind the mover: clear"), FindCrossing(Path, FVector(-60.0, 0.0, 0.0), SquadGiveWayTestLocal::CrossCm, Tuning).bCrosses);
	// On the flight's line but on the floor under its top end: the path there is 250 cm above his feet (a look-ahead that reaches it).
	FSquadGiveWayTuning Far;
	Far.LookAheadCm = 1000.f;
	TestFalse(TEXT("under the flight (another level): clear"), FindCrossing(Path, FVector(300.0, 500.0, 0.0), SquadGiveWayTestLocal::CrossCm, Far).bCrosses);
	TestTrue(TEXT("in the flight's mouth (its foot, his level): crosses"), FindCrossing(Path, FVector(300.0, 60.0, 0.0), SquadGiveWayTestLocal::CrossCm, Far).bCrosses);
	// Beyond the look-ahead (400 cm): later.
	const TArray<FVector> Long{ FVector(0.0, 0.0, 0.0), FVector(1500.0, 0.0, 0.0) };
	TestFalse(TEXT("600 cm ahead: not yet"), FindCrossing(Long, FVector(600.0, 0.0, 0.0), SquadGiveWayTestLocal::CrossCm, Tuning).bCrosses);
	TestTrue(TEXT("350 cm ahead: now"), FindCrossing(Long, FVector(350.0, 0.0, 0.0), SquadGiveWayTestLocal::CrossCm, Tuning).bCrosses);
	return true;
}

SQUAD_GIVE_WAY_TEST(FSquadGiveWayAsideTest, "StepsAsideOffThePath")
bool FSquadGiveWayAsideTest::RunTest(const FString&)
{
	using namespace SquadGiveWayRules;
	const FSquadGiveWayTuning Tuning;
	const TArray<FVector> Path{ FVector(0.0, 0.0, 0.0), FVector(300.0, 0.0, 0.0), FVector(300.0, 600.0, 300.0) };
	// A straight way (a doorway, a corridor): perpendicular, his own side, step_aside_cm off.
	const TArray<FVector> Straight{ FVector(0.0, 0.0, 0.0), FVector(1000.0, 0.0, 0.0) };
	const TArray<FVector2D> StraightSpots = AsideSpots(Straight, FindCrossing(Straight, FVector(220.0, 30.0, 0.0), SquadGiveWayTestLocal::CrossCm, Tuning),
		SquadGiveWayTestLocal::CrossCm, Tuning);
	TestTrue(TEXT("straight: first his own side (left), perpendicular, step_aside_cm off"),
		StraightSpots.Num() > 0 && StraightSpots[0].Equals(FVector2D(220.0, Tuning.StepAsideCm), 0.01));
	// In the stair mouth, left of the approach - the flight's side: the straight-out spots lie on the flight's corridor and are dropped.
	const FCrossing Mouth = FindCrossing(Path, FVector(220.0, 30.0, 0.0), SquadGiveWayTestLocal::CrossCm, Tuning);
	const TArray<FVector2D> Spots = AsideSpots(Path, Mouth, SquadGiveWayTestLocal::CrossCm, Tuning);
	TestTrue(TEXT("mouth: spots found"), Spots.Num() > 0);
	TestFalse(TEXT("mouth: the straight-out spot next to the flight is not offered"), Spots.ContainsByPredicate([&Tuning](const FVector2D& Spot)
		{ return Spot.Equals(FVector2D(220.0, Tuning.StepAsideCm), 0.01); }));
	for (const FVector2D& Spot : Spots)
	{
		TestTrue(*FString::Printf(TEXT("mouth: (%.0f, %.0f) off the whole path corridor - the flight up included"), Spot.X, Spot.Y),
			DistanceToPath(Path, Spot, 0.f) >= SquadGiveWayTestLocal::CrossCm);
	}
	// Right at the corner (the flight's foot).
	const FCrossing Corner = FindCrossing(Path, FVector(290.0, 10.0, 0.0), SquadGiveWayTestLocal::CrossCm, Tuning);
	const TArray<FVector2D> CornerSpots = AsideSpots(Path, Corner, SquadGiveWayTestLocal::CrossCm, Tuning);
	TestTrue(TEXT("corner: a spot"), CornerSpots.Num() > 0);
	for (const FVector2D& Spot : CornerSpots)
	{
		TestTrue(*FString::Printf(TEXT("corner: (%.0f, %.0f) off the approach and the flight"), Spot.X, Spot.Y),
			DistanceToPath(Path, Spot, 0.f) >= SquadGiveWayTestLocal::CrossCm);
	}
	TestEqual(TEXT("no crossing: no spot"), AsideSpots(Path, FCrossing(), SquadGiveWayTestLocal::CrossCm, Tuning).Num(), 0);
	return true;
}

SQUAD_GIVE_WAY_TEST(FSquadGiveWayTuningTest, "TuningFromJson")
bool FSquadGiveWayTuningTest::RunTest(const FString&)
{
	TSharedPtr<FJsonObject> Json;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(TEXT("{\"enabled\": 0, \"look_ahead_cm\": 500, \"step_aside_cm\": 9999, \"retry_s\": 2}"))), Json);
	const FSquadGiveWayTuning Tuning = SquadGiveWayTuning::FromJson(*Json);
	TestFalse(TEXT("enabled 0"), Tuning.bEnabled);
	TestEqual(TEXT("look_ahead_cm"), Tuning.LookAheadCm, 500.f);
	TestEqual(TEXT("step_aside_cm clamped"), Tuning.StepAsideCm, 500.f);
	TestEqual(TEXT("retry_s"), Tuning.RetrySeconds, 2.f);
	TestEqual(TEXT("missing key keeps the default"), Tuning.ClearanceCm, FSquadGiveWayTuning().ClearanceCm);

	FString Text;
	TSharedPtr<FJsonObject> File;
	const TSharedPtr<FJsonObject>* GiveWay = nullptr;
	TestTrue(TEXT("squad_regroup.json has a give_way object"), FFileHelper::LoadFileToString(Text, *SquadGiveWayTuning::GetDefaultPath())
		&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), File) && File.IsValid() && File->TryGetObjectField(TEXT("give_way"), GiveWay));
	if (GiveWay)
	{
		const FSquadGiveWayTuning Shipped = SquadGiveWayTuning::FromJson(**GiveWay);
		TestTrue(TEXT("shipped: on"), Shipped.bEnabled);
		TestTrue(TEXT("shipped: the spot clears two capsules + the clearance"), Shipped.StepAsideCm > 70.f + Shipped.ClearanceCm);
		TestTrue(TEXT("shipped v2: a zone before the foot"), Shipped.ApproachBeforeCm > 0.f && Shipped.ApproachHalfWidthCm > 0.f);
		TestTrue(TEXT("shipped v2: blocked orders retried"), Shipped.BlockedRetries > 0);
	}
	return true;
}

namespace SquadGiveWayTestLocal
{
	// A flight going up +X from a foot at (0, 0) (X6 B_x1_4 shape: lane 150 cm wide), the mover 200 cm before it on the floor.
	SquadGiveWayRules::FFlightApproach FlightAlongX(float ToFootCm = 200.f)
	{
		SquadGiveWayRules::FFlightApproach Approach;
		Approach.bValid = true;
		Approach.Foot = FVector2D(0.0, 0.0);
		Approach.Direction = FVector2D(1.0, 0.0);
		Approach.ToFootCm = ToFootCm;
		return Approach;
	}
}

SQUAD_GIVE_WAY_TEST(FSquadGiveWayFlightFeetTest, "FlightFeet")
bool FSquadGiveWayFlightFeetTest::RunTest(const FString&)
{
	using namespace SquadGiveWayRules;
	// Floor x < 300, a flight (uphill +X... axis given as -X: signed along the walk) from x 300 to 700, floor again after.
	const TArray<FVector> Path = { FVector(0, 0, 0), FVector(300, 0, 0), FVector(700, 0, 300), FVector(900, 0, 300) };
	auto IsRamp = [](const FVector& Point, FVector2D& OutUphill)
	{
		OutUphill = FVector2D(-1.0, 0.0);
		return Point.X > 300.0 && Point.X < 700.0;
	};
	const TArray<FFlightFoot> Feet = FindFlightFeet(Path, IsRamp, 40.f);
	TestEqual(TEXT("one flight"), Feet.Num(), 1);
	if (Feet.Num() == 1)
	{
		TestTrue(TEXT("its foot just past x 300"), Feet[0].Foot.X > 300.0 && Feet[0].Foot.X <= 345.0);
		TestTrue(TEXT("the axis signed along his walk"), Feet[0].Direction.Equals(FVector2D(1.0, 0.0), 1e-3));
	}
	auto AllRamp = [](const FVector&, FVector2D& OutUphill) { OutUphill = FVector2D::ZeroVector; return true; };
	TestEqual(TEXT("starting on the flight: no foot"), FindFlightFeet(Path, AllRamp, 40.f).Num(), 0);

	FSquadGiveWayTuning Tuning;
	FFlightFoot Foot;
	Foot.Foot = FVector(0, 0, 0);
	Foot.Direction = FVector2D(1.0, 0.0);
	const TArray<FFlightFoot> One = { Foot };
	const FFlightApproach Near = SelectApproach(One, FVector(-200, 0, 0), Tuning);
	TestTrue(TEXT("200 cm before the foot: its approach"), Near.bValid && FMath::IsNearlyEqual(Near.ToFootCm, 200.f, 1.f));
	TestTrue(TEXT("50 cm up the flight: still"), SelectApproach(One, FVector(50, 0, 40), Tuning).bValid);
	TestFalse(TEXT("past it"), SelectApproach(One, FVector(Tuning.ApproachAfterCm + 50.f, 0, 100), Tuning).bValid);
	TestFalse(TEXT("too far before it"), SelectApproach(One, FVector(-Tuning.LookAheadCm - 50.f, 0, 0), Tuning).bValid);
	TestFalse(TEXT("another level's flight"), SelectApproach(One, FVector(-200, 0, 400), Tuning).bValid);
	return true;
}

SQUAD_GIVE_WAY_TEST(FSquadGiveWayApproachZoneTest, "ApproachZone")
bool FSquadGiveWayApproachZoneTest::RunTest(const FString&)
{
	using namespace SquadGiveWayRules;
	using namespace SquadGiveWayTestLocal;
	const FSquadGiveWayTuning Tuning;
	const FFlightApproach Approach = FlightAlongX();
	// X6 B_x1_4: the Engineer 69 cm before the foot, 48 cm beside its axis - off the string-pulled line, in the mouth.
	TestTrue(TEXT("beside the line in the mouth"), InApproachZone(Approach, FVector2D(-69.0, 48.0), 35.f, Tuning));
	TestTrue(TEXT("on the flight's first metre"), InApproachZone(Approach, FVector2D(80.0, 0.0), 35.f, Tuning));
	TestFalse(TEXT("far up the flight"), InApproachZone(Approach, FVector2D(300.0, 0.0), 35.f, Tuning));
	TestFalse(TEXT("well to the side"), InApproachZone(Approach, FVector2D(-69.0, 250.0), 35.f, Tuning));
	TestFalse(TEXT("far before it"), InApproachZone(Approach, FVector2D(-400.0, 0.0), 35.f, Tuning));

	const FCrossing Crossing = ApproachCrossing(Approach, FVector2D(-69.0, 48.0));
	TestTrue(TEXT("a crossing"), Crossing.bCrosses);
	TestEqual(TEXT("his side (left of +X)"), Crossing.Side, 1);
	TestTrue(TEXT("on the axis abreast of him"), Crossing.OnPath.Equals(FVector2D(-69.0, 0.0), 0.1));

	// The spots: out of the zone and a real step from where he stands.
	const TArray<FVector> Path = { FVector(-200, 0, 0), FVector(0, 0, 0), FVector(400, 0, 300) };
	const FVector2D Idle(-69.0, 48.0);
	const TArray<FVector2D> Spots = AsideSpots(Path, Crossing, CrossCm, Tuning, &Idle, &Approach, 35.f);
	TestTrue(TEXT("some spot"), Spots.Num() > 0);
	for (const FVector2D& Spot : Spots)
	{
		TestFalse(TEXT("spot out of the zone"), InApproachZone(Approach, Spot, 35.f, Tuning));
		TestTrue(TEXT("spot min_step_cm+ from him"), FVector2D::Distance(Spot, Idle) >= Tuning.MinStepCm);
	}
	if (Spots.Num() > 0)
	{
		TestTrue(TEXT("his own side first"), Spots[0].Y > 0.0);
	}
	return true;
}

SQUAD_GIVE_WAY_TEST(FSquadGiveWayMinStepTest, "MinStep")
bool FSquadGiveWayMinStepTest::RunTest(const FString&)
{
	using namespace SquadGiveWayRules;
	using namespace SquadGiveWayTestLocal;
	const FSquadGiveWayTuning Tuning;
	// X6 B_x1_4 v1: he stands 85 cm off the line; 120 cm off the line was a 35 cm "step" (inside the walk's acceptance radius).
	const TArray<FVector> Path = { FVector(0, 0, 0), FVector(400, 0, 0) };
	const FCrossing Crossing = FindCrossing(Path, FVector(200, 85, 0), CrossCm, Tuning);
	TestTrue(TEXT("crosses"), Crossing.bCrosses);
	const FVector2D Idle(200.0, 85.0);
	for (const FVector2D& Spot : AsideSpots(Path, Crossing, CrossCm, Tuning, &Idle))
	{
		TestTrue(TEXT("no token step"), FVector2D::Distance(Spot, Idle) >= Tuning.MinStepCm);
	}
	TestEqual(TEXT("v1 overload unchanged"), AsideSpots(Path, Crossing, CrossCm, Tuning).Num() > 0, true);
	return true;
}

SQUAD_GIVE_WAY_TEST(FSquadGiveWayBehindTest, "Behind")
bool FSquadGiveWayBehindTest::RunTest(const FString&)
{
	using namespace SquadGiveWayRules;
	using namespace SquadGiveWayTestLocal;
	const FSquadGiveWayTuning Tuning;
	// X6 B_x2_13: the mover at his path's start, a mate on it beside him (0 cm ahead), the sides walled: spots behind the mover.
	const TArray<FVector> Path = { FVector(0, 0, 0), FVector(400, 0, 0), FVector(800, 0, 300) };
	const FCrossing Crossing = FindCrossing(Path, FVector(10, -40, 0), CrossCm, Tuning);
	TestTrue(TEXT("crosses"), Crossing.bCrosses);
	const FVector2D Idle(10.0, -40.0);
	FFlightApproach Approach;
	Approach.bValid = true;
	Approach.Foot = FVector2D(400.0, 0.0);
	Approach.Direction = FVector2D(1.0, 0.0);
	const TArray<FVector2D> Spots = BehindSpots(Path, Crossing, CrossCm, Tuning, &Idle, &Approach, 35.f);
	TestTrue(TEXT("some spot"), Spots.Num() > 0);
	for (const FVector2D& Spot : Spots)
	{
		TestTrue(TEXT("behind the mover (against his way)"), Spot.X < 0.0);
		TestTrue(TEXT("off his path"), DistanceToPath(Path, Spot, Crossing.AlongCm + Tuning.LookAheadCm) >= CrossCm);
		TestTrue(TEXT("a real step"), FVector2D::Distance(Spot, Idle) >= Tuning.MinStepCm);
	}
	if (Spots.Num() > 0)
	{
		TestTrue(TEXT("straight back first"), FMath::IsNearlyZero(Spots[0].Y, 0.1));
	}
	TestEqual(TEXT("no crossing: none"), BehindSpots(Path, FCrossing(), CrossCm, Tuning, &Idle).Num(), 0);
	return true;
}

SQUAD_GIVE_WAY_TEST(FSquadGiveWaySlotWalkTest, "SlotWalk")
bool FSquadGiveWaySlotWalkTest::RunTest(const FString&)
{
	using namespace SquadGiveWayRules;
	using namespace SquadGiveWayTestLocal;
	const FSquadGiveWayTuning Tuning;
	const FFlightApproach Approach = FlightAlongX();
	// X6 B_x2_13: the Commander walks his slot across the mouth.
	TestTrue(TEXT("across the mouth"), SlotWalkEntersApproach(Approach, FVector2D(-100.0, 300.0), FVector2D(-100.0, -300.0), 35.f, Tuning));
	TestTrue(TEXT("into the mouth"), SlotWalkEntersApproach(Approach, FVector2D(-100.0, 300.0), FVector2D(-100.0, 20.0), 35.f, Tuning));
	TestFalse(TEXT("well before it"), SlotWalkEntersApproach(Approach, FVector2D(-500.0, 300.0), FVector2D(-500.0, -300.0), 35.f, Tuning));
	TestFalse(TEXT("alongside, off the side"), SlotWalkEntersApproach(Approach, FVector2D(-300.0, 250.0), FVector2D(200.0, 250.0), 35.f, Tuning));
	TestFalse(TEXT("already in it: walks out"), SlotWalkEntersApproach(Approach, FVector2D(-50.0, 0.0), FVector2D(-50.0, 400.0), 35.f, Tuning));
	TestFalse(TEXT("no flight"), SlotWalkEntersApproach(FFlightApproach(), FVector2D(-100.0, 300.0), FVector2D(-100.0, -300.0), 35.f, Tuning));
	return true;
}

SQUAD_GIVE_WAY_TEST(FSquadGiveWayBlockedRetryTest, "BlockedRetry")
bool FSquadGiveWayBlockedRetryTest::RunTest(const FString&)
{
	using namespace SquadGiveWayRules;
	const FSquadGiveWayTuning Tuning;
	TestTrue(TEXT("own order far from the goal: asked again"), ShouldRetryBlockedMove(true, 1000.f, 0, Tuning));
	TestFalse(TEXT("a formation move: the formation re-paths it"), ShouldRetryBlockedMove(false, 1000.f, 0, Tuning));
	TestFalse(TEXT("near the goal: arrived enough"), ShouldRetryBlockedMove(true, Tuning.BlockedRetryMinGoalCm - 1.f, 0, Tuning));
	TestFalse(TEXT("retries used up"), ShouldRetryBlockedMove(true, 1000.f, Tuning.BlockedRetries, Tuning));
	FSquadGiveWayTuning Off = Tuning;
	Off.bEnabled = false;
	TestFalse(TEXT("give way off"), ShouldRetryBlockedMove(true, 1000.f, 0, Off));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS