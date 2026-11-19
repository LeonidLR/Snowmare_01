#include "Misc/AutomationTest.h"
#include "Combat/SquadFireRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Godot Scenes/movements/player.gd parity: _find_shoot_target (posture range, barricade rules, flank switch),
// get_elevation_advantage, is_target_in_dead_zone, _shoot_at_target damage.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSquadFireRulesTest, "CodexTactics.Combat.SquadFire.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSquadFireRulesTest::RunTest(const FString&)
{
	TestEqual(TEXT("Range standing x1"), SquadFireRules::GetPostureRangeMultiplier(EOperativeStance::Standing), 1.f);
	TestEqual(TEXT("Range crouching x1.15"), SquadFireRules::GetPostureRangeMultiplier(EOperativeStance::Crouching), 1.15f);
	TestEqual(TEXT("Range prone x1.35"), SquadFireRules::GetPostureRangeMultiplier(EOperativeStance::Prone), 1.35f);
	TestEqual(TEXT("Damage crouching x1.25"), SquadFireRules::GetStanceDamageMultiplier(EOperativeStance::Crouching), 1.25f);
	TestEqual(TEXT("Damage prone x1.6"), SquadFireRules::GetStanceDamageMultiplier(EOperativeStance::Prone), 1.6f);

	// Godot: centre (feet + 1 m) at least 1.5 m above the enemy's feet.
	TestFalse(TEXT("Same floor: not elevated"), SquadFireRules::GetElevationAdvantage(0.f, 0.f).bElevated);
	TestFalse(TEXT("0.4 m up: not elevated"), SquadFireRules::GetElevationAdvantage(40.f, 0.f).bElevated);
	const FElevationAdvantage High = SquadFireRules::GetElevationAdvantage(50.f, 0.f);
	TestTrue(TEXT("0.5 m up: elevated"), High.bElevated && High.RangeMultiplier == 1.25f && High.DamageMultiplier == 1.15f && High.bBypassLowCover);

	TestFalse(TEXT("Dead zone needs 1.8 m"), SquadFireRules::IsInDeadZone(FVector(0.f, 0.f, 70.f), FVector(100.f, 0.f, 0.f)));
	TestTrue(TEXT("Right under the platform"), SquadFireRules::IsInDeadZone(FVector(0.f, 0.f, 200.f), FVector(150.f, 0.f, 0.f)));
	TestFalse(TEXT("Too far out"), SquadFireRules::IsInDeadZone(FVector(0.f, 0.f, 200.f), FVector(300.f, 0.f, 0.f)));
	// G007: the same geometry is a dead zone only when the floor blocks. A clear trace on a shared slope is a shot.
	TestFalse(TEXT("Clear slope: no dead zone"), SquadFireRules::AppliesDeadZone(FVector(0.f, 0.f, 200.f), FVector(150.f, 0.f, 0.f), false, false));
	TestTrue(TEXT("Blocked trace: the platform zone applies"), SquadFireRules::AppliesDeadZone(FVector(0.f, 0.f, 200.f), FVector(150.f, 0.f, 0.f), true, false));
	TestTrue(TEXT("Level edge: the platform zone applies"), SquadFireRules::AppliesDeadZone(FVector(0.f, 0.f, 200.f), FVector(150.f, 0.f, 0.f), false, true));
	// G007 API review 2-4: the floor profile like CombatGround::ProbeFloor ([0] = the shooter's feet, probes, last = the target's
	// feet); FloorAt < -1000 = a probe that found no floor (unknown).
	auto Profile = [](const FVector& Shooter, const FVector& Target, TFunctionRef<float(float)> FloorAt, float& OutSpacing)
	{
		const float Horizontal = FVector::Dist2D(Shooter, Target);
		TArray<FFloorSample> Samples;
		Samples.Add({ 0.f, static_cast<float>(Shooter.Z), true });
		for (const float Distance : CombatGroundRules::ProbeDistances(Horizontal, FGroundProbeConfig(), OutSpacing))
		{
			const float Z = FloorAt(Distance);
			Samples.Add({ Distance, Z, Z > -1000.f });
		}
		Samples.Add({ Horizontal, static_cast<float>(Target.Z), true });
		return Samples;
	};
	auto DeadZoneHolds = [&](const FVector& Shooter, const FVector& Target, TFunctionRef<float(float)> FloorAt)
	{
		float Spacing = 0.f;
		const TArray<FFloorSample> Floor = Profile(Shooter, Target, FloorAt, Spacing);
		return SquadFireRules::AppliesDeadZone(Shooter, Target, false, SquadFireRules::IsLevelEdgeBetween(Floor, Spacing));
	};
	// 2. Platforms 0.8 / 1.2 / 1.8 m, lip 50 cm out, target 1 m out below: an edge (its own edge_drop_cm, not min_delta_z_cm).
	for (const float Height : { 80.f, 120.f, 180.f })
	{
		TestTrue(*FString::Printf(TEXT("%.1f m platform: dead zone"), Height / 100.f), DeadZoneHolds(FVector(0.f, 0.f, Height), FVector(100.f, 0.f, 0.f),
			[Height](float D) { return D <= 50.f ? Height : 0.f; }));
	}
	// 3. The lip 30-60 cm out (the old probe loop took none there): a lip at 55 cm with the target further out, and the
	// review's case - the target 55 cm out right under the lip - and one 20 cm out (no probe at all: feet to feet).
	TestTrue(TEXT("lip at 55 cm, target 1.2 m out: dead zone"), DeadZoneHolds(FVector(0.f, 0.f, 200.f), FVector(120.f, 0.f, 0.f),
		[](float D) { return D <= 55.f ? 200.f : 0.f; }));
	TestTrue(TEXT("on the lip, target 55 cm out: dead zone"), DeadZoneHolds(FVector(0.f, 0.f, 200.f), FVector(55.f, 0.f, 0.f),
		[](float D) { return D <= 40.f ? 200.f : 0.f; }));
	TestTrue(TEXT("on the lip, target 20 cm out: dead zone"), DeadZoneHolds(FVector(0.f, 0.f, 200.f), FVector(20.f, 0.f, 0.f),
		[](float) { return 200.f; }));
	TestTrue(TEXT("target right under the platform: dead zone"), DeadZoneHolds(FVector(0.f, 0.f, 200.f), FVector(150.f, 0.f, 0.f),
		[](float) { return 200.f; }));
	// 4. Real stairs: a steep stair (2 m over 1.5 m) is no edge; with open risers / grating / a probe off the side the missed probes
	// are unknown, never an edge (the old code read them as a 5 m drop).
	auto StairAt = [](float D) { return 200.f - D * (200.f / 150.f); };
	TestFalse(TEXT("steep stair: no dead zone"), DeadZoneHolds(FVector(0.f, 0.f, 200.f), FVector(150.f, 0.f, 0.f), StairAt));
	TestFalse(TEXT("open stair, every other probe missed: no dead zone"), DeadZoneHolds(FVector(0.f, 0.f, 200.f), FVector(150.f, 0.f, 0.f),
		[StairAt](float D) { return FMath::RoundToInt(D / 25.f) % 2 == 0 ? -5000.f : StairAt(D); }));
	TestFalse(TEXT("open stair, every probe missed: no dead zone"), DeadZoneHolds(FVector(0.f, 0.f, 200.f), FVector(150.f, 0.f, 0.f),
		[](float) { return -5000.f; }));
	// Final review 6: melee bridges an isolated missed probe (IsSharedGround), the dead-zone edge does not - a lone miss right
	// at a 2 m lip stays unknown (no edge -> the shot is not held by the edge test), and melee does not bridge it either (200 cm).
	{
		auto LipMissed = [](float D) { return FMath::IsNearlyEqual(D, 50.f, 1.f) ? -5000.f : (D < 50.f ? 200.f : 0.f); };
		TestFalse(TEXT("lone miss at the lip: unknown, no dead-zone edge"), DeadZoneHolds(FVector(0.f, 0.f, 200.f), FVector(150.f, 0.f, 0.f), LipMissed));
		float Spacing = 0.f;
		const TArray<FFloorSample> Floor = Profile(FVector(0.f, 0.f, 200.f), FVector(150.f, 0.f, 0.f), LipMissed, Spacing);
		TestFalse(TEXT("lone miss at the lip: no edge"), CombatGroundRules::HasLevelEdge(Floor, SquadFireRules::GetDeadZoneConfig().EdgeDropCm, Spacing));
		TestFalse(TEXT("lone miss at the lip: melee does not bridge 2 m"), CombatGroundRules::IsSharedGround(Floor, FMeleeReachConfig().StepMaxCm));
		// The open stair above (every other probe missed): shared ground for melee, still no edge for the dead zone.
		const TArray<FFloorSample> Open = Profile(FVector(0.f, 0.f, 200.f), FVector(150.f, 0.f, 0.f),
			[StairAt](float D) { return FMath::RoundToInt(D / 25.f) % 2 == 0 ? -5000.f : StairAt(D); }, Spacing);
		TestFalse(TEXT("open stair: no dead-zone edge"), CombatGroundRules::HasLevelEdge(Open, SquadFireRules::GetDeadZoneConfig().EdgeDropCm, Spacing));
	}
	TestFalse(TEXT("shared slope: no dead zone"), DeadZoneHolds(FVector(0.f, 0.f, 200.f), FVector(150.f, 0.f, 0.f),
		[](float D) { return 200.f - D * (200.f / 150.f); }));
	const FDeadZoneConfig& DeadZone = SquadFireRules::GetDeadZoneConfig();
	TestTrue(TEXT("Dead zone numbers from the fire config"), DeadZone.ChestOffsetCm == 100.f && DeadZone.MinDeltaZCm == 180.f
		&& DeadZone.MaxHorizontalCm == 240.f && DeadZone.MinAngleDeg == 45.f && DeadZone.EdgeDropCm == 60.f);

	const FElevationAdvantage Flat;
	TestTrue(TEXT("Clear line"), SquadFireRules::JudgeLine(EShotLineHit::Clear, EOperativeStance::Prone, Flat).bCanHit);
	TestFalse(TEXT("Wall blocks"), SquadFireRules::JudgeLine(EShotLineHit::Blocked, EOperativeStance::Standing, Flat).bCanHit);
	const FShotLineVerdict Prone = SquadFireRules::JudgeLine(EShotLineHit::Barricade, EOperativeStance::Prone, Flat);
	TestTrue(TEXT("Prone behind a barricade: blocked"), !Prone.bCanHit && Prone.bBarricadeBlocked);
	const FShotLineVerdict Crouched = SquadFireRules::JudgeLine(EShotLineHit::Barricade, EOperativeStance::Crouching, Flat);
	TestTrue(TEXT("Crouched behind a barricade: cover 0.8"), Crouched.bCanHit && Crouched.Cover == 0.8f);
	const FShotLineVerdict Standing = SquadFireRules::JudgeLine(EShotLineHit::Barricade, EOperativeStance::Standing, Flat);
	TestTrue(TEXT("Standing over a barricade: cover 1"), Standing.bCanHit && Standing.Cover == 1.f);
	TestTrue(TEXT("Elevated prone over a barricade"), SquadFireRules::JudgeLine(EShotLineHit::Barricade, EOperativeStance::Prone, High).bCanHit);

	TestTrue(TEXT("Closer than 0.9 x current"), SquadFireRules::IsSignificantlyCloser(800.f, 1000.f, 0.9f));
	TestFalse(TEXT("Not closer enough"), SquadFireRules::IsSignificantlyCloser(950.f, 1000.f, 0.9f));
	TestTrue(TEXT("Within 3.5 m always"), SquadFireRules::IsSignificantlyCloser(340.f, 360.f, 0.65f));

	TestTrue(TEXT("Crit: roll 0.24 < luck 25"), SquadFireRules::IsCrit(25.f, 0.24f));
	TestFalse(TEXT("No crit: roll 0.25"), SquadFireRules::IsCrit(25.f, 0.25f));

	TestEqual(TEXT("Crouched crit elevated: 18 * 1.25 * 0.8 * 2 * 1.15 * 0.9"),
		SquadFireRules::ComputeShotDamage(18.f, EOperativeStance::Crouching, 0.8f, true, High, 0.9f), 18.f * 1.25f * 0.8f * 2.f * 1.15f * 0.9f, 0.001f);
	TestEqual(TEXT("Plain standing shot"), SquadFireRules::ComputeShotDamage(18.f, EOperativeStance::Standing, 1.f, false, Flat, 1.f), 18.f);

	TestEqual(TEXT("1 m: 1 cell"), SquadFireRules::GetDistanceCells(100.f), 1);
	TestEqual(TEXT("7.5 m: 5 cells"), SquadFireRules::GetDistanceCells(750.f), 5);
	TestEqual(TEXT("2.2 m: 1 cell"), SquadFireRules::GetDistanceCells(220.f), 1);

	const FSquadFireConfig Defaults = SquadFireRules::ConfigFromBalance(nullptr);
	TestTrue(TEXT("Default switching"), Defaults.SwitchDelay[2] == 1.2f && Defaults.SwitchRatio[1] == 0.8f);
	return true;
}

// fix/corner-peek-standoff (game designer play-test 2026-11-09, L_Gen_Depot_1): our man and a PMC fighter peeking round two corners saw each
// other (the honest sight looks at the leaned-out head) but the shot line ran to the body behind the corner - nobody fired. UE-only.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSquadFirePeekAimPointTest, "CodexTactics.Combat.SquadFire.PeekAimPoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSquadFirePeekAimPointTest::RunTest(const FString&)
{
	using SquadFireRules::ChooseShotAimPoint;
	// The body stays the aim point while its line is clear (an exposed head changes nothing then).
	TestTrue(TEXT("a clear body line: the body"), ChooseShotAimPoint(EShotLineHit::Clear, true, EShotLineHit::Clear) == EShotAimPoint::Body);
	TestTrue(TEXT("a clear body line, nobody peeking: the body"), ChooseShotAimPoint(EShotLineHit::Clear, false, EShotLineHit::Blocked) == EShotAimPoint::Body);
	// The bug: the body behind his corner, the head and shoulder out with a clear line - aim there.
	TestTrue(TEXT("body behind his corner, his head out with a clear line: the exposed part"),
		ChooseShotAimPoint(EShotLineHit::Blocked, true, EShotLineHit::Clear) == EShotAimPoint::Exposed);
	// Corners still protect: not peeking (nothing shows), or the line to the head is blocked too -> the blocked body line stands (no shot).
	TestTrue(TEXT("behind his corner, not peeking: the body (blocked, no shot)"), ChooseShotAimPoint(EShotLineHit::Blocked, false, EShotLineHit::Clear) == EShotAimPoint::Body);
	TestTrue(TEXT("peeking, but the wall blocks the head too: the body (blocked)"), ChooseShotAimPoint(EShotLineHit::Blocked, true, EShotLineHit::Blocked) == EShotAimPoint::Body);
	TestTrue(TEXT("peeking, a barricade in front of the head: the body (blocked)"), ChooseShotAimPoint(EShotLineHit::Blocked, true, EShotLineHit::Barricade) == EShotAimPoint::Body);
	// A barricade keeps its own stance rules (JudgeLine) on the body.
	TestTrue(TEXT("a barricade before the body: the body (JudgeLine decides)"), ChooseShotAimPoint(EShotLineHit::Barricade, true, EShotLineHit::Clear) == EShotAimPoint::Body);
	return true;
}

// G008b: X4 tactical seed 11, the kneeling sniper on B_hall_n sees a man at ~85 m (the sight line starts past the roof rim) and the
// crouch muzzle hits the parapet. The retry is allowed only for that low rim. A 4 m wall and a man on the ground stay blocked.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSquadFireRoofLipTest, "CodexTactics.Combat.SquadFire.RoofLip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSquadFireRoofLipTest::RunTest(const FString&)
{
	const FRoofLipConfig Lip;
	SquadFireRules::FRoofLipShot Shot;
	Shot.ShooterFeetZ = 900.f;
	Shot.TargetFeetZ = 0.f;
	Shot.HitDistanceCm = 2700.f;
	Shot.TargetDistanceCm = 8544.f;
	Shot.RimTopZ = 1000.f;
	TestTrue(TEXT("roof, parapet top 100 cm, target 85 m past a rim at 27 m: retry"), SquadFireRules::MayClearRoofLip(Shot, Lip));
	const FVector Origin = SquadFireRules::RoofLipRetryOrigin(FVector(5600.f, -750.f, 985.f), FVector(4532.f, -3250.f, 985.f), Lip.PastCm);
	TestTrue(TEXT("the retry keeps the muzzle height"), FMath::IsNearlyEqual(static_cast<float>(Origin.Z), 985.f, 0.1f));
	TestTrue(TEXT("the retry steps past the near face"), FVector::Dist2D(Origin, FVector(4532.f, -3250.f, 985.f)) > 79.f);
	TestTrue(TEXT("80 cm does not cross a 4 m wall"), Lip.PastCm < 400.f);

	Shot.ShooterFeetZ = 0.f;
	Shot.TargetFeetZ = 0.f;
	Shot.HitDistanceCm = 400.f;
	Shot.RimTopZ = 300.f;
	TestFalse(TEXT("on the ground, a wall at 4 m: no retry"), SquadFireRules::MayClearRoofLip(Shot, Lip));

	Shot.ShooterFeetZ = 900.f;
	Shot.TargetFeetZ = 0.f;
	Shot.HitDistanceCm = 7000.f;
	Shot.TargetDistanceCm = 9000.f;
	Shot.RimTopZ = 1000.f;
	TestFalse(TEXT("a rim past lip_cm is another building"), SquadFireRules::MayClearRoofLip(Shot, Lip));

	Shot.HitDistanceCm = 1500.f;
	Shot.RimTopZ = 900.f + 300.f;
	TestFalse(TEXT("a tall wall on the roof: no retry"), SquadFireRules::MayClearRoofLip(Shot, Lip));

	Shot.RimTopZ = 1000.f;
	Shot.TargetDistanceCm = 1000.f;
	TestFalse(TEXT("the target is not beyond the rim"), SquadFireRules::MayClearRoofLip(Shot, Lip));

	Shot.TargetDistanceCm = 8544.f;
	Shot.RimTopZ = 900.f - 50.f;
	TestFalse(TEXT("a floor well below his feet is not the rim"), SquadFireRules::MayClearRoofLip(Shot, Lip));
	return true;
}

#endif
