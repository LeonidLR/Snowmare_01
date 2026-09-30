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

#endif
