#include "Misc/AutomationTest.h"
#include "Characters/EnemyAIRules.h"
#include "Data/AITuning.h"
#include "HAL/IConsoleManager.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

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
	// G007 + API review 1: "shared slope" is the probed floor rising smoothly, not the angle. A floor profile every 25 cm from the
	// attacker's feet (0) to the target's: a ledge steps up 110 cm in one sample, a ramp rises a little per sample.
	const FMeleeReachConfig Reach; // the fire_gate.json "melee_reach" defaults
	// Like CombatGround::ProbeFloor: [0] = the attacker's feet (z 0), the probes, last = the target's feet.
	auto Profile = [](float Horizontal, float TargetZ, TFunctionRef<float(float)> FloorAt)
	{
		TArray<FFloorSample> Samples;
		FGroundProbeConfig Probe;
		float Spacing = 0.f;
		Samples.Add({ 0.f, 0.f, true });
		for (const float Distance : CombatGroundRules::ProbeDistances(Horizontal, Probe, Spacing))
		{
			Samples.Add({ Distance, FloorAt(Distance), true });
		}
		Samples.Add({ Horizontal, TargetZ, true });
		return Samples;
	};
	for (const float Horizontal : { 0.f, 40.f, 115.f, 140.f, 170.f })
	{
		// The ledge face 10 cm short of the target: the floor jumps 0 -> 110 between two samples.
		const TArray<FFloorSample> Ledge = Profile(Horizontal, 110.f, [Horizontal](float D) { return D >= Horizontal - 10.f ? 110.f : 0.f; });
		const FVector TargetFeet(Horizontal, 0.f, 110.f);
		TestFalse(*FString::Printf(TEXT("1.1 m ledge at %.0f cm horizontal: no bite"), Horizontal),
			EnemyAIRules::CanMeleeOverProfile(FVector::ZeroVector, TargetFeet, 180.f, Ledge, Reach));
		// Unknown floor (no probe) is no shared ground either: the ledge stays out of reach.
		TestFalse(*FString::Printf(TEXT("1.1 m ledge at %.0f cm, floor unknown: no bite"), Horizontal),
			EnemyAIRules::CanMeleeOnGround(FVector::ZeroVector, TargetFeet, 180.f, false, Reach));
	}
	// The review's failing case: (115, 0, 110) is 43.7 deg, the old angle test called it a slope and bit.
	TestFalse(TEXT("review case: 1.1 m ledge 115 cm out, off shared ground"),
		EnemyAIRules::CanMeleeOnGround(FVector::ZeroVector, FVector(115.f, 0.f, 110.f), 180.f, false, Reach));
	// Ramps 15-35 deg within 1.8 m: shared ground, in reach.
	for (const float SlopeDeg : { 15.f, 20.f, 25.f, 30.f, 35.f })
	{
		const float Tan = FMath::Tan(FMath::DegreesToRadians(SlopeDeg));
		for (const float Distance : { 100.f, 140.f, 170.f })
		{
			const float Horizontal = Distance * FMath::Cos(FMath::DegreesToRadians(SlopeDeg));
			const TArray<FFloorSample> Ramp = Profile(Horizontal, Horizontal * Tan, [Tan](float D) { return D * Tan; });
			TestTrue(*FString::Printf(TEXT("%.0f deg ramp, %.0f cm away: shared ground"), SlopeDeg, Distance), CombatGroundRules::IsSharedGround(Ramp, Reach.StepMaxCm));
			TestTrue(*FString::Printf(TEXT("%.0f deg ramp, %.0f cm away: bites"), SlopeDeg, Distance),
				EnemyAIRules::CanMeleeOverProfile(FVector::ZeroVector, FVector(Horizontal, 0.f, Horizontal * Tan), 180.f, Ramp, Reach));
		}
	}
	// G007 final review 1: downhill keeps the old body-point reach (the blend is uphill only). The attacker above on a 20 / 34 deg
	// ramp, the target's feet 100 cm lower 150 cm out: vertical 0, distance 150 -> bites, on or off shared ground.
	for (const float SlopeDeg : { 20.f, 34.f })
	{
		const float Tan = FMath::Tan(FMath::DegreesToRadians(SlopeDeg));
		// The ramp falls toward the target and levels out at -100 cm (34 deg reaches it at ~148 cm, 20 deg never: a drop at the end).
		const TArray<FFloorSample> Down = Profile(150.f, -100.f, [Tan](float D) { return FMath::Max(-D * Tan, -100.f); });
		TestTrue(*FString::Printf(TEXT("%.0f deg ramp, target 100 cm lower at 150 cm: bites (profile)"), SlopeDeg),
			EnemyAIRules::CanMeleeOverProfile(FVector::ZeroVector, FVector(150.f, 0.f, -100.f), 180.f, Down, Reach));
		// The target on the ramp itself, 150 cm out.
		const TArray<FFloorSample> OnRamp = Profile(150.f, -150.f * Tan, [Tan](float D) { return -D * Tan; });
		TestTrue(*FString::Printf(TEXT("%.0f deg ramp, target down the ramp 150 cm out: shared"), SlopeDeg), CombatGroundRules::IsSharedGround(OnRamp, Reach.StepMaxCm));
		TestTrue(*FString::Printf(TEXT("%.0f deg ramp, target down the ramp 150 cm out: bites"), SlopeDeg),
			EnemyAIRules::CanMeleeOverProfile(FVector::ZeroVector, FVector(150.f, 0.f, -150.f * Tan), 180.f, OnRamp, Reach));
	}
	TestTrue(TEXT("downhill 100 cm at 150 cm, shared ground: bites"), EnemyAIRules::CanMeleeOnGround(FVector::ZeroVector, FVector(150.f, 0.f, -100.f), 180.f, true, Reach));
	TestTrue(TEXT("downhill 100 cm at 150 cm, off shared ground: bites"), EnemyAIRules::CanMeleeOnGround(FVector::ZeroVector, FVector(150.f, 0.f, -100.f), 180.f, false, Reach));
	TestEqual(TEXT("downhill 100 cm: the old ~180 cm planar reach, shared or not"), EnemyAIRules::MeleeReachPlanarOnGround(180.f, -100.f, true, Reach),
		EnemyAIRules::MeleeReachPlanarOnGround(180.f, -100.f, false, Reach), 0.01f);
	TestTrue(TEXT("downhill 100 cm: planar reach ~180 cm"), EnemyAIRules::MeleeReachPlanarOnGround(180.f, -100.f, true, Reach) > 179.f);
	// The ramp stop from RampFightSmoke: 1.24 m out, feet 83 cm up.
	TestTrue(TEXT("Shared slope: in reach"), EnemyAIRules::CanMeleeOnGround(FVector::ZeroVector, FVector(124.f, 0.f, 83.f), 180.f, true, Reach));
	// A stair (18 cm risers every 25 cm) is shared ground; a 50 cm crate is not.
	const TArray<FFloorSample> Stair = Profile(150.f, 108.f, [](float D) { return FMath::FloorToFloat(D / 25.f + 0.01f) * 18.f; });
	TestTrue(TEXT("Stair: shared ground"), CombatGroundRules::IsSharedGround(Stair, Reach.StepMaxCm));
	// Final review 6: an open / grated stair. Isolated missed probes are bridged when the hits on both sides are within
	// step_max_cm; two misses in a row (or a gap the bridge would hide) stay unknown.
	TArray<FFloorSample> Missed = Stair;
	Missed[2].bHit = false;
	TestTrue(TEXT("Stair, one missed probe: shared (bridged)"), CombatGroundRules::IsSharedGround(Missed, Reach.StepMaxCm));
	TArray<FFloorSample> TwoMissed = Missed;
	TwoMissed[3].bHit = false;
	TestFalse(TEXT("Stair, two missed probes in a row: not shared (unknown)"), CombatGroundRules::IsSharedGround(TwoMissed, Reach.StepMaxCm));
	{
		// The reported stairs case: 83 cm up at 124 cm planar (34 deg), every other probe through an open riser.
		const float Tan = 83.f / 124.f;
		TArray<FFloorSample> OpenStair = Profile(124.f, 83.f, [Tan](float D) { return FMath::FloorToFloat(D * Tan / 16.6f + 0.01f) * 16.6f; });
		for (int32 Index = 1; Index < OpenStair.Num() - 1; Index += 2)
		{
			OpenStair[Index].bHit = false;
		}
		TestTrue(TEXT("open stair, every other probe missed: shared"), CombatGroundRules::IsSharedGround(OpenStair, Reach.StepMaxCm));
		TestTrue(TEXT("open stair, 83 cm up at 124 cm: bites"), EnemyAIRules::CanMeleeOverProfile(FVector::ZeroVector, FVector(124.f, 0.f, 83.f), 180.f, OpenStair, Reach));
		// A ledge hidden behind a miss: the hits on both sides are 110 cm apart -> not bridged.
		TArray<FFloorSample> HiddenLedge = Profile(124.f, 110.f, [](float D) { return D >= 60.f ? 110.f : 0.f; });
		for (FFloorSample& Sample : HiddenLedge)
		{
			if (Sample.DistanceCm > 45.f && Sample.DistanceCm < 70.f)
			{
				Sample.bHit = false;
			}
		}
		TestFalse(TEXT("ledge behind a missed probe: not shared"), CombatGroundRules::IsSharedGround(HiddenLedge, Reach.StepMaxCm));
	}
	// Flat ground is exactly the old body-point rule either way: 1.6 m planar is past the 1.8 m body reach, 1.49 m is in.
	TestFalse(TEXT("Flat, past the chest: no bite"), EnemyAIRules::CanMeleeOnGround(FVector::ZeroVector, FVector(160.f, 0.f, 0.f), 180.f, true, Reach));
	TestTrue(TEXT("Flat, 1.49 m: bites"), EnemyAIRules::CanMeleeOnGround(FVector::ZeroVector, FVector(149.f, 0.f, 0.f), 180.f, false, Reach));
	TestFalse(TEXT("Over the height cap on a ramp: no bite"), EnemyAIRules::CanMeleeOnGround(FVector::ZeroVector, FVector(100.f, 0.f, 125.f), 180.f, true, Reach));
	// No reach jump at the old 15 deg threshold: the planar reach changes smoothly with the height.
	const float Below = EnemyAIRules::MeleeReachPlanarOnGround(180.f, 36.f, true, Reach);
	const float Above = EnemyAIRules::MeleeReachPlanarOnGround(180.f, 40.f, true, Reach);
	TestTrue(*FString::Printf(TEXT("planar reach is continuous on a shared slope (%.1f / %.1f)"), Below, Above), FMath::Abs(Below - Above) < 15.f);
	TestEqual(TEXT("flat planar reach = the old body-point reach"), EnemyAIRules::MeleeReachPlanarOnGround(180.f, 0.f, true, Reach), 149.666f, 0.1f);
	TestTrue(TEXT("ramp (83 cm up) leaves planar reach (the close-in stops)"), EnemyAIRules::MeleeReachPlanarOnGround(180.f, 83.f, true, Reach) > 120.f);
	TestEqual(TEXT("ledge (110 cm up, off shared ground): no planar reach"), EnemyAIRules::MeleeReachPlanarOnGround(180.f, 110.f, false, Reach), 0.f);
	// Turn-based: a 1.5 m grid neighbour on flat ground bites, one up a 1.1 m ledge does not.
	const float Grid = EnemyAIRules::GridMeleeRange(180.f, 150.f, Reach);
	TestTrue(TEXT("turn-based flat neighbour bites"), EnemyAIRules::CanMeleeOnGround(FVector::ZeroVector, FVector(150.f, 0.f, 0.f), Grid, true, Reach));
	TestFalse(TEXT("turn-based neighbour up a 1.1 m ledge: no bite"), EnemyAIRules::CanMeleeOnGround(FVector::ZeroVector, FVector(150.f, 0.f, 110.f), Grid, false, Reach));

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
// operative's centre 1 m up). Codex.Enemy.MeleeCloseIn 1 uses MeleeApproachAcceptance (on by data since the user's approval
// 2026-10-18: CodexTactics.Characters.EnemyAI.MeleeCloseInOnByData).
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

// User approval 2026-10-18: the close-in is the game value, set by DATA (ai_tuning.json "cvars"), the code default (0) stays for A/B.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyMeleeCloseInDataTest, "CodexTactics.Characters.EnemyAI.MeleeCloseInOnByData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyMeleeCloseInDataTest::RunTest(const FString&)
{
	FString Text;
	if (!TestTrue(TEXT("ai_tuning.json readable"), FFileHelper::LoadFileToString(Text, *AITuning::GetDefaultPath())))
	{
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedPtr<FJsonObject>* CVars = nullptr;
	if (!TestTrue(TEXT("ai_tuning.json parses with a \"cvars\" object"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root)
		&& Root.IsValid() && Root->TryGetObjectField(TEXT("cvars"), CVars) && CVars))
	{
		return false;
	}
	FString Value;
	TestTrue(TEXT("ai_tuning.json sets Codex.Enemy.MeleeCloseIn"), (*CVars)->TryGetStringField(TEXT("Codex.Enemy.MeleeCloseIn"), Value));
	TestEqual(TEXT("melee enemies close in to their bite (1)"), Value, FString(TEXT("1")));
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Enemy.MeleeCloseIn"));
	TestNotNull(TEXT("the A/B console variable exists"), CVar);
	return true;
}

// Game designer 2026-10-20: BP_Enemy_FrozenPMC_Fighter is a variant body of the Frostbitten (a child Blueprint on another
// skeleton): it keeps the archetype through the class mapping, shows its own name and brings its own knockdown clips.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyVariantBodyTest, "CodexTactics.Characters.EnemyAI.VariantBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyVariantBodyTest::RunTest(const FString&)
{
	TestEqual(TEXT("no override -> the archetype's name"), EnemyAIRules::ResolveDisplayName(TEXT("Frostbitten"), FString()), FString(TEXT("Frostbitten")));
	TestEqual(TEXT("blank override -> the archetype's name"), EnemyAIRules::ResolveDisplayName(TEXT("Frostbitten"), TEXT("   ")), FString(TEXT("Frostbitten")));
	TestEqual(TEXT("override wins, trimmed"), EnemyAIRules::ResolveDisplayName(TEXT("Frostbitten"), TEXT(" Frozen PMC ")), FString(TEXT("Frozen PMC")));

	TestEqual(TEXT("empty folder = /Game/Animations_KnockDown itself"), EnemyAIRules::NormalizeKnockdownClipFolder(FString()), FString());
	TestEqual(TEXT("sub-folder kept"), EnemyAIRules::NormalizeKnockdownClipFolder(TEXT("Frostbitten")), FString(TEXT("Frostbitten")));
	TestEqual(TEXT("slashes stripped"), EnemyAIRules::NormalizeKnockdownClipFolder(TEXT("/PMC/")), FString(TEXT("PMC")));
	TestEqual(TEXT("pasted full path"), EnemyAIRules::NormalizeKnockdownClipFolder(TEXT("/Game/Animations_KnockDown/Brute")), FString(TEXT("Brute")));
	TestEqual(TEXT("pasted root path"), EnemyAIRules::NormalizeKnockdownClipFolder(TEXT("/Game/Animations_KnockDown/")), FString());
	TestEqual(TEXT("backslashes"), EnemyAIRules::NormalizeKnockdownClipFolder(TEXT("\\Frostbitten\\")), FString(TEXT("Frostbitten")));

	const FString Frostbitten = TEXT("/Game/Characters/Enemies/Frostbitten/BP_Enemy_Frostbitten.BP_Enemy_Frostbitten_C");
	const FString Pmc = TEXT("/Game/Characters/Enemies/PMC/FrozenPMC_Fighter/BP_Enemy_FrozenPMC_Fighter.BP_Enemy_FrozenPMC_Fighter_C");
	const TArray<TPair<EEnemyArchetype, FString>> Mapping = { { EEnemyArchetype::Frostbitten, Frostbitten } };
	FString Source;
	TestTrue(TEXT("a placed BP_Enemy_FrozenPMC_Fighter -> Frostbitten (its parent is mapped)"), EnemyAIRules::ResolvePlacedArchetype(
		EEnemyArchetype::FrostHound, EEnemyArchetype::FrostHound, { Pmc, Frostbitten, TEXT("/Script/CodexTactics.EnemyCharacter") }, Mapping, Source)
		== EEnemyArchetype::Frostbitten && Source == TEXT("class mapping"));
	return true;
}

// Game designer play-test 2026-10-22: hounds outside the lake perimeter said they feared the running generator's heat through
// two walls. Every enemy type feels a heat source only with a clear line to it (AEnemyCharacter::HasClearHeatLine).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyHeatLineTest, "CodexTactics.Characters.EnemyAI.HeatFearNeedsClearLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyHeatLineTest::RunTest(const FString&)
{
	TestTrue(TEXT("in the open, 3 m from the generator (reach 6 m): fear"), EnemyAIRules::FeelsHeat(300.f, 600.f, true));
	TestFalse(TEXT("3 m away, two walls in between: no fear"), EnemyAIRules::FeelsHeat(300.f, 600.f, false));
	TestFalse(TEXT("in the open but 7 m away: no fear"), EnemyAIRules::FeelsHeat(700.f, 600.f, true));
	TestTrue(TEXT("the line is traced above a 60 cm barricade"), EnemyAIRules::HeatLineHeightCm > 60.f);
	return true;
}

#endif
