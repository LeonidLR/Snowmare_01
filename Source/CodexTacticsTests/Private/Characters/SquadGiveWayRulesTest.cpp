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
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS