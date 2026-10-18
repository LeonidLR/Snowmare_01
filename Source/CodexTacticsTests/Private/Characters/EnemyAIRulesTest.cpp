#include "Misc/AutomationTest.h"
#include "Characters/EnemyAIRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Godot Scenes/movements/enemy_base.gd / enemy_frost_*.gd parity: target weights, blocking obstacles, melee reach,
// fire-fear steering, spitter targeting / movement / line of fire, per-type affinities and armor.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyAIRulesTest, "CodexTactics.Characters.EnemyAI.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyAIRulesTest::RunTest(const FString&)
{
	const FEnemyAIConfig Config;
	const FVector Enemy(0.f, 0.f, 0.f);
	TArray<FEnemyTargetCandidate> Candidates;
	Candidates.Add({ EEnemyTargetKind::Operative, FVector(500.f, 0.f, 100.f), true });
	Candidates.Add({ EEnemyTargetKind::Generator, FVector(1000.f, 0.f, 0.f), true });
	Candidates.Add({ EEnemyTargetKind::Turret, FVector(0.f, 900.f, 0.f), true });
	// Small: generator 10 m x 0.4 = 4 m beats the operative (5.1 m) and the turret (9 m x 0.5).
	TestEqual(TEXT("Hound goes for the generator"), EnemyAIRules::SelectTarget(Config, true, Enemy, Candidates, false), 1);
	// Large: operative 5.1 x 0.7 = 3.6 m; turret 9 m x 0.85 = 7.65 and beyond the 10 m threat? no (9 m <= 10 m) but not closer than 3.6 x 1.25.
	TestEqual(TEXT("Brute goes for the operative"), EnemyAIRules::SelectTarget(Config, false, Enemy, Candidates, false), 0);
	Candidates[1].bUsable = false; // broken generator
	TestEqual(TEXT("Broken generator ignored: the hound goes for the turret (9 m x 0.5)"), EnemyAIRules::SelectTarget(Config, true, Enemy, Candidates, false), 2);
	TArray<FEnemyTargetCandidate> TurretClose = { { EEnemyTargetKind::Operative, FVector(1500.f, 0.f, 100.f), true }, { EEnemyTargetKind::Turret, FVector(0.f, 800.f, 0.f), true } };
	TestEqual(TEXT("Turret within the threat distance pulls a brute"), EnemyAIRules::SelectTarget(Config, false, Enemy, TurretClose, false), 1);

	const FVector Target(1000.f, 0.f, 0.f);
	TestEqual(TEXT("Barricade ahead blocks"), EnemyAIRules::SelectBlockingObstacle(Enemy, &Target, { FVector(150.f, 30.f, 0.f) }), 0);
	TestEqual(TEXT("Barricade behind ignored"), EnemyAIRules::SelectBlockingObstacle(Enemy, &Target, { FVector(-150.f, 0.f, 0.f) }), INDEX_NONE);
	TestEqual(TEXT("Beyond 2.2 m ignored"), EnemyAIRules::SelectBlockingObstacle(Enemy, &Target, { FVector(230.f, 0.f, 0.f) }), INDEX_NONE);

	TestTrue(TEXT("Hound reaches 1.8 m"), EnemyAIRules::CanMelee(170.f, 100.f, 180.f));
	TestFalse(TEXT("Operative on a platform: no bite"), EnemyAIRules::CanMelee(170.f, 130.f, 180.f));

	const FVector Flee = EnemyAIRules::FleeDirection(FVector(100.f, 0.f, 0.f), FVector::ZeroVector, &Target);
	TestTrue(TEXT("Flee away from the fire"), Flee.X > 0.8f);
	const FVector Side = FVector(0.f, 1000.f, 0.f);
	const FVector Bent = EnemyAIRules::FleeDirection(FVector(100.f, 0.f, 0.f), FVector::ZeroVector, &Side);
	TestTrue(TEXT("Flee bends towards the target's side"), Bent.X > 0.7f && Bent.Y > 0.4f);

	TestEqual(TEXT("Spitter prefers the elevated sniper"), EnemyAIRules::SelectSpitterTarget(FVector::ZeroVector,
		{ FVector(600.f, 0.f, 100.f), FVector(1500.f, 0.f, 400.f) }, { false, true }), 1);
	TestEqual(TEXT("Spitter skips a target right above it"), EnemyAIRules::SelectSpitterTarget(FVector::ZeroVector,
		{ FVector(100.f, 0.f, 300.f), FVector(1500.f, 0.f, 100.f) }, { false, false }), 1);

	TestEqual(TEXT("No line of sight: approach"), EnemyAIRules::SpitterMove(false, 800.f, 1200.f), ESpitterMove::Approach);
	TestEqual(TEXT("Too far: approach"), EnemyAIRules::SpitterMove(true, 1500.f, 1200.f), ESpitterMove::Approach);
	TestEqual(TEXT("Too close: back off"), EnemyAIRules::SpitterMove(true, 800.f, 1200.f), ESpitterMove::Retreat);
	TestEqual(TEXT("In the band: hold"), EnemyAIRules::SpitterMove(true, 1200.f, 1200.f), ESpitterMove::Hold);

	TestFalse(TEXT("Prone behind a barricade: hidden"), EnemyAIRules::JudgeSpitterLine(EShotLineHit::Barricade, EOperativeStance::Prone, 0.35f).bHasLos);
	const FSpitterLine Crouch = EnemyAIRules::JudgeSpitterLine(EShotLineHit::Barricade, EOperativeStance::Crouching, 0.35f);
	TestTrue(TEXT("Crouched behind a barricade: cover 0.65"), Crouch.bHasLos && FMath::IsNearlyEqual(Crouch.Cover, 0.65f));
	TestFalse(TEXT("Wall blocks"), EnemyAIRules::JudgeSpitterLine(EShotLineHit::Blocked, EOperativeStance::Standing, 0.35f).bHasLos);

	TestEqual(TEXT("Brute kinetic 0.25"), EnemyAIRules::GetAffinities(EEnemyArchetype::Brute).Kinetic, 0.25f);
	TestEqual(TEXT("Hound melee 1.3"), EnemyAIRules::GetAffinities(EEnemyArchetype::FrostHound).Melee, 1.3f);
	TestEqual(TEXT("Frostbitten armor 0.15"), EnemyAIRules::GetBaseArmor(EEnemyArchetype::Frostbitten), 0.15f);
	TestTrue(TEXT("Small enemies"), EnemyAIRules::IsSmallEnemy(EEnemyArchetype::Frostbitten) && !EnemyAIRules::IsSmallEnemy(EEnemyArchetype::Spitter));
	// Fire zone detour: around the zone, not into it and back (no shaking at the edge).
	{
		const FVector Fire(0.f, 0.f, 0.f);
		FVector Waypoint;
		TestFalse(TEXT("Detour: clear way, walk straight"),
			EnemyAIRules::FireDetourWaypoint(FVector(-1000.f, 900.f, 0.f), Fire, 600.f, FVector(1000.f, 900.f, 0.f), Waypoint));
		TestTrue(TEXT("Detour: the way crosses the zone"),
			EnemyAIRules::FireDetourWaypoint(FVector(-900.f, 100.f, 0.f), Fire, 600.f, FVector(900.f, 100.f, 0.f), Waypoint));
		TestTrue(TEXT("Detour: the waypoint is outside the zone"), FVector::Dist2D(Waypoint, Fire) > 600.f);
		TestTrue(TEXT("Detour: it goes round on the target's side (+Y), 35 degrees at most"), Waypoint.Y > 100.f
			&& FMath::Abs(FMath::RadiansToDegrees(FMath::Atan2(Waypoint.Y, Waypoint.X)) - FMath::RadiansToDegrees(FMath::Atan2(100.f, -900.f))) <= 35.5f);
		// A target inside the zone (the running generator): wait at the nearest edge point.
		TestTrue(TEXT("Detour: target inside the zone"),
			EnemyAIRules::FireDetourWaypoint(FVector(750.f, 0.f, 0.f), Fire, 600.f, FVector(100.f, 0.f, 0.f), Waypoint));
		TestTrue(TEXT("Detour: waits at the edge facing the target"), Waypoint.Equals(FVector(700.f, 0.f, 0.f), 1.f));
	}
	return true;
}

// Placed enemies (not from a spawner): the archetype from the instance, the game mode's class mapping, else the class
// default (user bug 2026-10-12: a placed BP_Enemy_Brute played a 45 HP hound).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyPlacedArchetypeTest, "CodexTactics.Characters.EnemyAI.PlacedArchetype",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyPlacedArchetypeTest::RunTest(const FString&)
{
	const FString Brute = TEXT("/Game/Characters/Enemies/Brute/BP_Enemy_Brute.BP_Enemy_Brute_C");
	const FString Hound = TEXT("/Game/Characters/Enemies/Hound/BP_Enemy_Hound.BP_Enemy_Hound_C");
	const FString Base = TEXT("/Script/CodexTactics.EnemyCharacter");
	const TArray<TPair<EEnemyArchetype, FString>> Mapping = { { EEnemyArchetype::FrostHound, Hound }, { EEnemyArchetype::Brute, Brute } };
	FString Source;
	// A placed BP_Enemy_Brute, the field left at the property default (FrostHound): the class mapping makes it a Brute.
	TestTrue(TEXT("BP_Enemy_Brute untouched -> Brute"), EnemyAIRules::ResolvePlacedArchetype(EEnemyArchetype::FrostHound, EEnemyArchetype::FrostHound,
		{ Brute, Base }, Mapping, Source) == EEnemyArchetype::Brute && Source == TEXT("class mapping"));
	// A child Blueprint of BP_Enemy_Brute (IsChildOf): its parent is mapped.
	TestTrue(TEXT("a child of BP_Enemy_Brute -> Brute"), EnemyAIRules::ResolvePlacedArchetype(EEnemyArchetype::FrostHound, EEnemyArchetype::FrostHound,
		{ TEXT("/Game/Test/BP_Enemy_BruteElite.BP_Enemy_BruteElite_C"), Brute, Base }, Mapping, Source) == EEnemyArchetype::Brute);
	// The designer set the instance to Cutter: it wins.
	TestTrue(TEXT("instance override wins"), EnemyAIRules::ResolvePlacedArchetype(EEnemyArchetype::Cutter, EEnemyArchetype::FrostHound,
		{ Brute, Base }, Mapping, Source) == EEnemyArchetype::Cutter && Source == TEXT("instance"));
	// An unmapped class (the C++ marksman sets its own default): the class default.
	TestTrue(TEXT("unmapped C++ marksman -> its class default"), EnemyAIRules::ResolvePlacedArchetype(EEnemyArchetype::Marksman, EEnemyArchetype::Marksman,
		{ TEXT("/Script/CodexTactics.MarksmanEnemyCharacter"), Base }, Mapping, Source) == EEnemyArchetype::Marksman && Source == TEXT("class default"));
	TestTrue(TEXT("plain C++ enemy -> the property default"), EnemyAIRules::ResolvePlacedArchetype(EEnemyArchetype::FrostHound, EEnemyArchetype::FrostHound,
		{ Base }, Mapping, Source) == EEnemyArchetype::FrostHound);
	return true;
}

// Bug hunt 2026-10-18 (CornerHoldSmoke 1 in 3): MoveToActor's reach test adds both capsule radii, so the old acceptance
// AttackRange x 0.5 stopped a hound 90 + 40 + 40 = 170 cm off while its bite needs <= 150 cm planar (1.8 m to the
// operative's centre 1 m up). Codex.Enemy.MeleeCloseIn 1 uses MeleeApproachAcceptance (user decision pending).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyMeleeApproachTest, "CodexTactics.Characters.EnemyAI.MeleeApproach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyMeleeApproachTest::RunTest(const FString&)
{
	TestEqual(TEXT("hound planar reach at 1 m height difference"), EnemyAIRules::MeleeReachPlanar(180.f, 100.f), 149.666f, 0.01f);
	TestEqual(TEXT("flat: the whole range"), EnemyAIRules::MeleeReachPlanar(180.f, 0.f), 180.f, 0.01f);
	TestEqual(TEXT("too high: no planar reach"), EnemyAIRules::MeleeReachPlanar(180.f, 200.f), 0.f);
	// The stall: the old stop distance is outside the bite.
	const float OldStop = 180.f * 0.5f + 40.f + 40.f;
	TestFalse(TEXT("old stop 170 cm: no bite"), EnemyAIRules::CanMelee(FMath::Sqrt(FMath::Square(OldStop) + FMath::Square(100.f)), 100.f, 180.f));
	// The new acceptance: 149.7 - 80 - 15 = 54.7 -> 50 (5 cm steps); the stop (50 + radii) is inside the bite.
	const float Acceptance = EnemyAIRules::MeleeApproachAcceptance(180.f, 100.f, 40.f, 40.f);
	TestEqual(TEXT("hound acceptance"), Acceptance, 50.f);
	const float NewStop = Acceptance + 40.f + 40.f;
	TestTrue(TEXT("new stop 130 cm: bites"), EnemyAIRules::CanMelee(FMath::Sqrt(FMath::Square(NewStop) + FMath::Square(100.f)), 100.f, 180.f));
	TestEqual(TEXT("brute (2.4 m): at most range x 0.5"), EnemyAIRules::MeleeApproachAcceptance(240.f, 100.f, 40.f, 40.f), 120.f);
	TestEqual(TEXT("cutter (2 m)"), EnemyAIRules::MeleeApproachAcceptance(200.f, 100.f, 40.f, 40.f), 75.f);
	TestEqual(TEXT("never negative (huge capsules)"), EnemyAIRules::MeleeApproachAcceptance(180.f, 100.f, 90.f, 90.f), 0.f);
	TestEqual(TEXT("a few cm of height jitter keep the request"), EnemyAIRules::MeleeApproachAcceptance(180.f, 101.5f, 40.f, 40.f), 50.f);
	return true;
}

#endif
