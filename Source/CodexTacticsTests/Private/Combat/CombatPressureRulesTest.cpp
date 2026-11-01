#include "Misc/AutomationTest.h"
#include "Combat/CombatCheats.h"
#include "Combat/CombatPressureRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// The combat lab's endless enemy pressure and its cheats (game designer 2026-11-01, docs/design/COMBAT_LAB.md).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatPressureScheduleTest, "CodexTactics.Combat.Pressure.Schedule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCombatPressureScheduleTest::RunTest(const FString&)
{
	TestTrue(TEXT("Before the due time: not due"), CombatPressureRules::DecideEvent(100.f, 120.f, 0, 4) == EPressureEventDecision::NotDue);
	TestTrue(TEXT("Due with room: spawn"), CombatPressureRules::DecideEvent(120.f, 120.f, 3, 4) == EPressureEventDecision::Spawn);
	TestTrue(TEXT("Due, cap full: the event waits"), CombatPressureRules::DecideEvent(130.f, 120.f, 4, 4) == EPressureEventDecision::DelayedCapFull);
	TestTrue(TEXT("A cap below 1 counts as 1"), CombatPressureRules::DecideEvent(130.f, 120.f, 0, 0) == EPressureEventDecision::Spawn);
	// Group size 1..3, cut to the room under the cap.
	TestEqual(TEXT("Rand 0 -> min"), CombatPressureRules::GroupSize(0.f, 1, 3, 0, 4), 1);
	TestEqual(TEXT("Rand 0.5 -> 2"), CombatPressureRules::GroupSize(0.5f, 1, 3, 0, 4), 2);
	TestEqual(TEXT("Rand 0.99 -> max"), CombatPressureRules::GroupSize(0.99f, 1, 3, 0, 4), 3);
	TestEqual(TEXT("Rand 1 stays max"), CombatPressureRules::GroupSize(1.f, 1, 3, 0, 4), 3);
	TestEqual(TEXT("Cut to the room (3 alive of 4)"), CombatPressureRules::GroupSize(0.99f, 1, 3, 3, 4), 1);
	TestEqual(TEXT("No room: 0"), CombatPressureRules::GroupSize(0.5f, 1, 3, 4, 4), 0);
	TestEqual(TEXT("Max below min is lifted to min"), CombatPressureRules::GroupSize(0.9f, 2, 1, 0, 4), 2);
	// Kind by the patrol share.
	TestTrue(TEXT("Rand below the share: patrol"), CombatPressureRules::PickKind(0.59f, 0.6f) == EPressureSpawnKind::Patrol);
	TestTrue(TEXT("Rand at the share: ambush"), CombatPressureRules::PickKind(0.6f, 0.6f) == EPressureSpawnKind::Ambush);
	TestTrue(TEXT("Share 0: always ambush"), CombatPressureRules::PickKind(0.f, 0.f) == EPressureSpawnKind::Ambush);
	TestTrue(TEXT("Share 1: always patrol"), CombatPressureRules::PickKind(0.999f, 1.f) == EPressureSpawnKind::Patrol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatPressureTypesTest, "CodexTactics.Combat.Pressure.Types",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCombatPressureTypesTest::RunTest(const FString&)
{
	TestTrue(TEXT("Hound allowed"), CombatPressureRules::IsAllowedType(EEnemyArchetype::FrostHound));
	TestTrue(TEXT("Frostbitten allowed"), CombatPressureRules::IsAllowedType(EEnemyArchetype::Frostbitten));
	TestTrue(TEXT("Spitter allowed"), CombatPressureRules::IsAllowedType(EEnemyArchetype::Spitter));
	TestTrue(TEXT("Cutter allowed"), CombatPressureRules::IsAllowedType(EEnemyArchetype::Cutter));
	TestTrue(TEXT("Marksman allowed"), CombatPressureRules::IsAllowedType(EEnemyArchetype::Marksman));
	TestFalse(TEXT("Never the Brute"), CombatPressureRules::IsAllowedType(EEnemyArchetype::Brute));
	TestFalse(TEXT("Never the Cryo Drone"), CombatPressureRules::IsAllowedType(EEnemyArchetype::CryoDrone));
	TestFalse(TEXT("Never the Base"), CombatPressureRules::IsAllowedType(EEnemyArchetype::Base));
	TestEqual(TEXT("Empty weights: the five types"), CombatPressureRules::EffectiveWeights({}).Num(), 5);
	const TArray<FPressureTypeWeight> WithBrute = { { EEnemyArchetype::Brute, 5.f }, { EEnemyArchetype::Spitter, 1.f } };
	TestEqual(TEXT("A Brute weight is dropped"), CombatPressureRules::EffectiveWeights(WithBrute).Num(), 1);
	TestEqual(TEXT("Only Brute / zero weights: the five types"), CombatPressureRules::EffectiveWeights({ { EEnemyArchetype::Brute, 1.f },
		{ EEnemyArchetype::Cutter, 0.f } }).Num(), 5);
	// Weighted pick: hound 1, marksman 3.
	const TArray<FPressureTypeWeight> Weights = { { EEnemyArchetype::FrostHound, 1.f }, { EEnemyArchetype::Marksman, 3.f } };
	TestTrue(TEXT("Rand 0.2 -> hound (first quarter)"), CombatPressureRules::PickType(Weights, 0.2f) == EEnemyArchetype::FrostHound);
	TestTrue(TEXT("Rand 0.3 -> marksman"), CombatPressureRules::PickType(Weights, 0.3f) == EEnemyArchetype::Marksman);
	TestTrue(TEXT("Rand 1 -> the last"), CombatPressureRules::PickType(Weights, 1.f) == EEnemyArchetype::Marksman);
	int32 Brutes = 0;
	for (int32 Step = 0; Step < 100; ++Step)
	{
		Brutes += CombatPressureRules::PickType(WithBrute, Step / 100.f) == EEnemyArchetype::Brute ? 1 : 0;
	}
	TestEqual(TEXT("A Brute is never picked"), Brutes, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatPressureSpotTest, "CodexTactics.Combat.Pressure.SpawnSpot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCombatPressureSpotTest::RunTest(const FString&)
{
	// The lab wall W_A_1: (4500, -5400) -> (4500, -3600), 40 cm thick: a box centred at (4500, -4500), 1800 x 40 along +Y.
	FPressureBox Wall;
	Wall.Center = FVector2D(4500.0, -4500.0);
	Wall.AxisX = FVector2D(0.0, 1.0);
	Wall.HalfSize = FVector2D(900.0, 20.0);
	TestEqual(TEXT("Inside the wall: 0"), CombatPressureRules::DistanceToBox(FVector2D(4500.0, -4500.0), Wall), 0.f);
	TestEqual(TEXT("1 m off its face"), CombatPressureRules::DistanceToBox(FVector2D(4620.0, -4500.0), Wall), 100.f, 0.01f);
	TestEqual(TEXT("Past its end, diagonal"), CombatPressureRules::DistanceToBox(FVector2D(4820.0, -3200.0), Wall), 500.f, 0.01f);
	// A rotated box (45 deg, 200 x 200): a point on its diagonal 100 cm past the corner.
	FPressureBox Rotated;
	Rotated.AxisX = FVector2D(1.0, 1.0).GetSafeNormal();
	Rotated.HalfSize = FVector2D(100.0, 100.0);
	TestEqual(TEXT("Rotated box: 100 cm off a face"), CombatPressureRules::DistanceToBox(FVector2D(200.0, 0.0).GetRotated(45.0), Rotated), 100.f, 0.05f);
	TestEqual(TEXT("No boxes: MAX clearance"), CombatPressureRules::Clearance(FVector2D::ZeroVector, {}), MAX_flt);

	const TArray<FPressureBox> Boxes = { Wall };
	const TArray<FVector2D> Squad = { FVector2D(0.0, 0.0) };
	const FBox2D Bounds(FVector2D(-6000.0, -6000.0), FVector2D(6000.0, 6000.0));
	TestTrue(TEXT("Open ground far from the squad: ok"),
		CombatPressureRules::CheckSpawnPoint(FVector2D(3000.0, -3000.0), Boxes, 250.f, Squad, 2500.f, Bounds) == ESpawnPointVerdict::Ok);
	TestTrue(TEXT("2 m from the wall: refused (clearance 2.5 m)"),
		CombatPressureRules::CheckSpawnPoint(FVector2D(4700.0, -4500.0), Boxes, 250.f, Squad, 2500.f, Bounds) == ESpawnPointVerdict::NearWall);
	TestTrue(TEXT("Inside the wall: refused"),
		CombatPressureRules::CheckSpawnPoint(FVector2D(4500.0, -4000.0), Boxes, 250.f, Squad, 2500.f, Bounds) == ESpawnPointVerdict::NearWall);
	TestTrue(TEXT("On top of the squad: refused"),
		CombatPressureRules::CheckSpawnPoint(FVector2D(1000.0, 0.0), Boxes, 250.f, Squad, 2500.f, Bounds) == ESpawnPointVerdict::NearSquad);
	TestTrue(TEXT("Outside the bounds: refused"),
		CombatPressureRules::CheckSpawnPoint(FVector2D(7000.0, 0.0), Boxes, 250.f, Squad, 2500.f, Bounds) == ESpawnPointVerdict::OutOfBounds);
	TestTrue(TEXT("Invalid bounds: no bounds check"),
		CombatPressureRules::CheckSpawnPoint(FVector2D(7000.0, 0.0), Boxes, 250.f, Squad, 2500.f, FBox2D(ForceInit)) == ESpawnPointVerdict::Ok);

	// Scores: an ambush spot must be concealed; ahead beats behind.
	FPressureSpotInput Ambush;
	Ambush.Kind = EPressureSpawnKind::Ambush;
	Ambush.MinDistanceCm = 1400.f;
	Ambush.MaxDistanceCm = 3000.f;
	Ambush.DistanceCm = 2200.f;
	Ambush.AheadCos = 1.f;
	Ambush.bConcealed = false;
	TestTrue(TEXT("An unconcealed ambush spot is never taken"), CombatPressureRules::ScoreSpot(Ambush) <= -100.f);
	Ambush.bConcealed = true;
	const float Ahead = CombatPressureRules::ScoreSpot(Ambush);
	Ambush.AheadCos = -1.f;
	TestTrue(TEXT("Ambush: ahead of the squad's way beats behind it"), Ahead > CombatPressureRules::ScoreSpot(Ambush));
	FPressureSpotInput Patrol;
	Patrol.MinDistanceCm = 2500.f;
	Patrol.MaxDistanceCm = 5000.f;
	Patrol.DistanceCm = 3750.f;
	Patrol.bConcealed = true;
	const float Hidden = CombatPressureRules::ScoreSpot(Patrol);
	Patrol.bConcealed = false;
	Patrol.bWatched = true;
	TestTrue(TEXT("Patrol: out of the squad's sight and looks preferred"), Hidden > CombatPressureRules::ScoreSpot(Patrol));
	Patrol.DistanceCm = 6000.f;
	TestTrue(TEXT("Patrol: out of the band is penalised"), CombatPressureRules::ScoreSpot(Patrol) < Hidden - 2.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatPressureLoopTest, "CodexTactics.Combat.Pressure.LoopAndRecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCombatPressureLoopTest::RunTest(const FString&)
{
	const TArray<FVector2D> Loop = CombatPressureRules::PatrolLoop(FVector2D(100.0, 0.0), 1000.f, 4, 0.f);
	TestEqual(TEXT("4 waypoints"), Loop.Num(), 4);
	if (Loop.Num() == 4)
	{
		TestTrue(TEXT("First at the phase"), Loop[0].Equals(FVector2D(1100.0, 0.0), 0.1));
		TestTrue(TEXT("Second 90 deg on"), Loop[1].Equals(FVector2D(100.0, 1000.0), 0.1));
	}
	TestEqual(TEXT("At least 2 waypoints"), CombatPressureRules::PatrolLoop(FVector2D::ZeroVector, 500.f, 1, 0.f).Num(), 2);
	TestTrue(TEXT("Group member 0 on the point"), CombatPressureRules::GroupOffset(0, 200.f).IsNearlyZero());
	TestEqual(TEXT("Group member 1 at the spacing"), CombatPressureRules::GroupOffset(1, 200.f).Size(), 200.0, 0.01);
	TestTrue(TEXT("Members 1 and 2 apart"), FVector2D::Distance(CombatPressureRules::GroupOffset(1, 200.f), CombatPressureRules::GroupOffset(2, 200.f)) > 300.0);

	FCombatPressureConfig Config;
	Config.RecycleDistanceCm = 6000.f;
	Config.RecycleAfterSeconds = 60.f;
	TestTrue(TEXT("Far, unseen, long enough, unengaged: removed"), CombatPressureRules::ShouldRecycle(false, false, 7000.f, 61.f, Config));
	TestFalse(TEXT("Engaged: never removed"), CombatPressureRules::ShouldRecycle(true, false, 7000.f, 600.f, Config));
	TestFalse(TEXT("Seen: kept"), CombatPressureRules::ShouldRecycle(false, true, 7000.f, 600.f, Config));
	TestFalse(TEXT("Not far enough: kept"), CombatPressureRules::ShouldRecycle(false, false, 5000.f, 600.f, Config));
	TestFalse(TEXT("Not long enough: kept"), CombatPressureRules::ShouldRecycle(false, false, 7000.f, 30.f, Config));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatCheatsResolveTest, "CodexTactics.Combat.Cheats.Resolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCombatCheatsResolveTest::RunTest(const FString&)
{
	TestTrue(TEXT("Console -1: the level's flag on"), CombatCheats::Resolve(-1, true, false));
	TestFalse(TEXT("Console -1: the level's flag off"), CombatCheats::Resolve(-1, false, false));
	TestTrue(TEXT("Console 1 forces it on"), CombatCheats::Resolve(1, false, false));
	TestFalse(TEXT("Console 0 forces it off"), CombatCheats::Resolve(0, true, false));
	TestFalse(TEXT("Shipping: always off (level flag)"), CombatCheats::Resolve(-1, true, true));
	TestFalse(TEXT("Shipping: always off (console)"), CombatCheats::Resolve(1, true, true));
	TestFalse(TEXT("No world: no cheat"), CombatCheats::IsImmortal(nullptr) || CombatCheats::IsInfiniteAmmo(nullptr) || CombatCheats::IsImmortalActor(nullptr));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
