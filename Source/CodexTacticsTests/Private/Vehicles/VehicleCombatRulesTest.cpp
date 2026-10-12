#include "Misc/AutomationTest.h"
#include "Combat/KnockdownRules.h"
#include "Vehicles/VehicleCombatRules.h"
#include "Vehicles/VehicleRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// APC P2 (turret) + the 2026-10-12 user requests (run over small enemies, ram the Brute, boarding fade, wheel spin) —
// pure rules (docs/vehicles/APC_02_DESIGN.md §1.4, APC_00_DECISIONS.md). UE-only: design-number tests.

#define VEHICLE_COMBAT_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Vehicle." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace VehicleCombatTest
{
	VehicleCombatRules::FTurretCandidate Enemy(float DistanceCm, float HullCm, bool bLarge = false)
	{
		VehicleCombatRules::FTurretCandidate Candidate;
		Candidate.DistanceCm = DistanceCm;
		Candidate.HullDistanceCm = HullCm;
		Candidate.bLarge = bLarge;
		return Candidate;
	}
}

VEHICLE_COMBAT_TEST(FVehicleTurretPostureTest, "Turret.TargetByPosture")
bool FVehicleTurretPostureTest::RunTest(const FString&)
{
	using namespace VehicleCombatTest;
	const FVehicleTurretConfig Config;
	// A hound at 25 m and one at 30 m (both well clear of the hull), a Brute at 35 m.
	const TArray<VehicleCombatRules::FTurretCandidate> Candidates = { Enemy(2500.f, 2100.f), Enemy(3000.f, 2600.f), Enemy(3500.f, 3100.f, true) };
	VehicleCombatRules::FTurretContext Context;
	Context.ShellsLeft = 60;
	Context.Posture = ESquadFirePosture::Aggressive;
	TestEqual(TEXT("aggressive: the nearest"), VehicleCombatRules::ChooseTarget(Candidates, Context, Config), 0);
	Context.Posture = ESquadFirePosture::Passive;
	TestEqual(TEXT("passive: holds fire"), VehicleCombatRules::ChooseTarget(Candidates, Context, Config), INDEX_NONE);
	Context.PriorityIndex = 2;
	TestEqual(TEXT("passive + Ctrl+click: the priority target"), VehicleCombatRules::ChooseTarget(Candidates, Context, Config), 2);
	Context.PriorityIndex = INDEX_NONE;
	Context.Posture = ESquadFirePosture::Defensive;
	TestEqual(TEXT("defensive, not provoked: holds"), VehicleCombatRules::ChooseTarget(Candidates, Context, Config), INDEX_NONE);
	Context.bProvoked = true;
	TestEqual(TEXT("defensive, provoked: nobody within 20 m"), VehicleCombatRules::ChooseTarget(Candidates, Context, Config), INDEX_NONE);
	const TArray<VehicleCombatRules::FTurretCandidate> Close = { Enemy(1500.f, 1100.f), Enemy(1800.f, 1400.f, true) };
	TestEqual(TEXT("defensive, provoked: a Brute within 20 m first"), VehicleCombatRules::ChooseTarget(Close, Context, Config), 1);
	// Out of sight / dead / out of range never.
	TArray<VehicleCombatRules::FTurretCandidate> Hidden = { Enemy(2000.f, 1600.f), Enemy(4500.f, 4100.f) };
	Hidden[0].bVisible = false;
	Context.Posture = ESquadFirePosture::Aggressive;
	TestEqual(TEXT("hidden and out of range: none"), VehicleCombatRules::ChooseTarget(Hidden, Context, Config), INDEX_NONE);
	return true;
}

VEHICLE_COMBAT_TEST(FVehicleTurretDeadZoneTest, "Turret.DeadZone")
bool FVehicleTurretDeadZoneTest::RunTest(const FString&)
{
	using namespace VehicleCombatTest;
	const FVehicleTurretConfig Config;
	TestTrue(TEXT("3 m off the hull: dead zone"), VehicleCombatRules::IsInDeadZone(300.f, Config));
	TestFalse(TEXT("4 m: clear"), VehicleCombatRules::IsInDeadZone(400.f, Config));
	const TArray<VehicleCombatRules::FTurretCandidate> AtWheels = { Enemy(500.f, 150.f) };
	VehicleCombatRules::FTurretContext Context;
	Context.ShellsLeft = 60;
	TestEqual(TEXT("a hound at the wheels is never engaged"), VehicleCombatRules::ChooseTarget(AtWheels, Context, Config), INDEX_NONE);
	Context.PriorityIndex = 0;
	TestEqual(TEXT("not even as the priority target"), VehicleCombatRules::ChooseTarget(AtWheels, Context, Config), INDEX_NONE);
	TestEqual(TEXT("elevation clamped to -4"), VehicleCombatRules::ClampElevation(-20.f, Config), -4.f);
	TestEqual(TEXT("elevation clamped to +60"), VehicleCombatRules::ClampElevation(80.f, Config), 60.f);
	TestEqual(TEXT("traverse 60 deg/s"), VehicleCombatRules::StepAngle(0.f, 90.f, 60.f, 0.5f), 30.f);
	TestEqual(TEXT("traverse wraps the short way"), VehicleCombatRules::StepAngle(170.f, -170.f, 60.f, 0.5f), -170.f);
	TestTrue(TEXT("aimed within 4 deg"), VehicleCombatRules::IsAimed(3.f, 0.f, Config));
	TestFalse(TEXT("not aimed at 10 deg"), VehicleCombatRules::IsAimed(10.f, 0.f, Config));
	return true;
}

VEHICLE_COMBAT_TEST(FVehicleTurretWeaponTest, "Turret.WeaponAmmoHeat")
bool FVehicleTurretWeaponTest::RunTest(const FString&)
{
	using namespace VehicleCombatTest;
	const FVehicleTurretConfig Config;
	const VehicleCombatRules::FTurretCandidate Hound = Enemy(2500.f, 2100.f);
	const VehicleCombatRules::FTurretCandidate Brute = Enemy(3000.f, 2600.f, true);
	VehicleCombatRules::FTurretCandidate Pack = Enemy(2500.f, 2100.f);
	Pack.NeighbourCount = 2;
	TestEqual(TEXT("hound: MG"), VehicleCombatRules::ChooseWeapon(Hound, false, 60, false, Config), EVehicleTurretWeapon::MachineGun);
	TestEqual(TEXT("Brute: cannon"), VehicleCombatRules::ChooseWeapon(Brute, false, 60, false, Config), EVehicleTurretWeapon::Cannon);
	TestEqual(TEXT("a pack of 3: cannon"), VehicleCombatRules::ChooseWeapon(Pack, false, 60, false, Config), EVehicleTurretWeapon::Cannon);
	TestEqual(TEXT("priority: cannon"), VehicleCombatRules::ChooseWeapon(Hound, true, 60, false, Config), EVehicleTurretWeapon::Cannon);
	TestEqual(TEXT("no shells: Brute gets the MG"), VehicleCombatRules::ChooseWeapon(Brute, false, 0, false, Config), EVehicleTurretWeapon::MachineGun);
	TestEqual(TEXT("MG hot: a hound waits (shells kept)"), VehicleCombatRules::ChooseWeapon(Hound, false, 60, true, Config), EVehicleTurretWeapon::None);
	TestEqual(TEXT("MG hot: a Brute still gets the cannon"), VehicleCombatRules::ChooseWeapon(Brute, false, 60, true, Config), EVehicleTurretWeapon::Cannon);
	TestEqual(TEXT("beyond the MG's 35 m: a hound is not shot"), VehicleCombatRules::ChooseWeapon(Enemy(3800.f, 3400.f), false, 60, false, Config), EVehicleTurretWeapon::None);
	// Heat: 4 s of fire overheats, silent until it has cooled to 0 (3 s).
	bool bHot = false;
	float Heat = 0.f;
	for (int32 Step = 0; Step < 39; ++Step)
	{
		Heat = VehicleCombatRules::StepHeat(Heat, true, 0.1f, bHot, Config);
	}
	TestFalse(TEXT("3.9 s of fire: not yet"), bHot);
	Heat = VehicleCombatRules::StepHeat(Heat, true, 0.2f, bHot, Config);
	TestTrue(TEXT("4 s: overheated"), bHot);
	Heat = VehicleCombatRules::StepHeat(Heat, true, 1.5f, bHot, Config);
	TestTrue(TEXT("firing while hot adds nothing, it cools"), Heat < 1.f && bHot);
	Heat = VehicleCombatRules::StepHeat(Heat, false, 1.f, bHot, Config);
	TestTrue(TEXT("still latched until cold"), bHot && Heat > 0.f);
	Heat = VehicleCombatRules::StepHeat(Heat, false, 1.f, bHot, Config);
	TestTrue(TEXT("cold again: free"), !bHot && Heat <= 0.f);
	// Shells: the level JSON overrides the per-map default; damage + splash.
	TestEqual(TEXT("level override"), VehicleCombatRules::GetShellsForMap(12, Config), 12);
	TestEqual(TEXT("default 60"), VehicleCombatRules::GetShellsForMap(-1, Config), 60);
	// User 2026-10-12 ("too powerful"): 45 direct / 20 splash (radius 1.5 m kept), bursts of 2.
	TestEqual(TEXT("direct hit 45"), VehicleCombatRules::GetCannonDamage(0.f, true, Config), 45.f);
	TestEqual(TEXT("splash 20 within 1.5 m"), VehicleCombatRules::GetCannonDamage(120.f, false, Config), 20.f);
	TestEqual(TEXT("bursts of 2 shells"), Config.CannonBurstShots, 2);
	TestEqual(TEXT("0.5 s apart, 1.2 s pause"), Config.CannonShotIntervalSeconds + Config.CannonBurstPauseSeconds, 1.7f);
	TestEqual(TEXT("nothing beyond"), VehicleCombatRules::GetCannonDamage(200.f, false, Config), 0.f);
	TestTrue(TEXT("HUD line"), VehicleCombatRules::GetHudLine(true, 57, 60, 0.4f, false, ESquadFirePosture::Aggressive).Contains(TEXT("57/60")));
	TestTrue(TEXT("HUD overheat"), VehicleCombatRules::GetHudLine(true, 57, 60, 1.f, true, ESquadFirePosture::Aggressive).Contains(TEXT("OVERHEAT")));
	TestTrue(TEXT("HUD no gunner"), VehicleCombatRules::GetHudLine(false, 57, 60, 0.f, false, ESquadFirePosture::Aggressive).Contains(TEXT("NO GUNNER")));
	return true;
}

VEHICLE_COMBAT_TEST(FVehicleCrushTest, "RunOver.CrushEligibility")
bool FVehicleCrushTest::RunTest(const FString&)
{
	const FVehicleCrushConfig Config;
	VehicleCombatRules::FContactInput Contact;
	for (const EEnemyArchetype Small : { EEnemyArchetype::FrostHound, EEnemyArchetype::Frostbitten, EEnemyArchetype::Cutter, EEnemyArchetype::Spitter,
		EEnemyArchetype::Marksman })
	{
		Contact.Archetype = Small;
		TestEqual(TEXT("small enemy at speed: crushed"), VehicleCombatRules::EvaluateContact(Contact, 600.f, Config), EVehicleContact::Crush);
		TestEqual(TEXT("small enemy at 1 m/s: pushed aside, no brake"), VehicleCombatRules::EvaluateContact(Contact, 100.f, Config), EVehicleContact::Push);
	}
	Contact.Archetype = EEnemyArchetype::FrostHound;
	TestEqual(TEXT("exactly the 1.5 m/s threshold: crushed"), VehicleCombatRules::EvaluateContact(Contact, 150.f, Config), EVehicleContact::Crush);
	TestEqual(TEXT("reverse counts by its magnitude"), VehicleCombatRules::EvaluateContact(Contact, -250.f, Config), EVehicleContact::Crush);
	Contact.bAlive = false;
	TestEqual(TEXT("a corpse: nothing"), VehicleCombatRules::EvaluateContact(Contact, 600.f, Config), EVehicleContact::None);
	Contact.bAlive = true;
	Contact.Archetype = EEnemyArchetype::CryoDrone;
	Contact.bFlying = true;
	TestEqual(TEXT("a flying drone: the hull passes under"), VehicleCombatRules::EvaluateContact(Contact, 600.f, Config), EVehicleContact::None);
	// Never the squad, never recruits / NPCs, at any speed.
	VehicleCombatRules::FContactInput Operative;
	Operative.Kind = VehicleCombatRules::EContactKind::Operative;
	TestEqual(TEXT("operative: brake"), VehicleCombatRules::EvaluateContact(Operative, 1000.f, Config), EVehicleContact::Brake);
	Operative.Kind = VehicleCombatRules::EContactKind::Friendly;
	TestEqual(TEXT("recruit / NPC: brake"), VehicleCombatRules::EvaluateContact(Operative, 1000.f, Config), EVehicleContact::Brake);
	// Data-driven list.
	FVehicleCrushConfig NoHounds;
	NoHounds.CrushableArchetypes.Remove(EEnemyArchetype::FrostHound);
	Contact = VehicleCombatRules::FContactInput();
	Contact.Archetype = EEnemyArchetype::FrostHound;
	TestEqual(TEXT("off the list: blocks"), VehicleCombatRules::EvaluateContact(Contact, 600.f, NoHounds), EVehicleContact::Block);
	TestEqual(TEXT("each crush keeps 90 %"), VehicleCombatRules::GetSpeedAfterImpacts(1000.f, 2, false, Config), 810.f, 0.5f);
	return true;
}

VEHICLE_COMBAT_TEST(FVehicleRamBruteTest, "RunOver.BruteRam")
bool FVehicleRamBruteTest::RunTest(const FString&)
{
	const FVehicleCrushConfig Config;
	VehicleCombatRules::FContactInput Brute;
	Brute.Archetype = EEnemyArchetype::Brute;
	TestFalse(TEXT("the Brute is never crushable"), VehicleCombatRules::IsCrushable(EEnemyArchetype::Brute, Config));
	TestEqual(TEXT("fast (>= 4 m/s): knocked down"), VehicleCombatRules::EvaluateContact(Brute, 400.f, Config), EVehicleContact::KnockDown);
	TestEqual(TEXT("slow: blocks, the hull stops"), VehicleCombatRules::EvaluateContact(Brute, 399.f, Config), EVehicleContact::Block);
	Brute.bKnockdownImmune = true;
	TestEqual(TEXT("re-knock immunity: blocks"), VehicleCombatRules::EvaluateContact(Brute, 1000.f, Config), EVehicleContact::Block);
	Brute.bKnockdownImmune = false;
	Brute.bDowned = true;
	TestEqual(TEXT("lying Brute: blocks (default: not run over)"), VehicleCombatRules::EvaluateContact(Brute, 1000.f, Config), EVehicleContact::Block);
	FVehicleCrushConfig CrushDowned;
	CrushDowned.bCrushDownedRammed = true;
	TestEqual(TEXT("option: a lying Brute is run over"), VehicleCombatRules::EvaluateContact(Brute, 1000.f, CrushDowned), EVehicleContact::Crush);
	TestEqual(TEXT("the ram costs 75 % of the speed"), VehicleCombatRules::GetSpeedAfterImpacts(1000.f, 0, true, Config), 250.f, 0.5f);
	// The slower, heavier fall: clips at 0.65x, downed 1.5x.
	FKnockdownState State;
	State.FallDuration = 0.9f;
	State.DownedDuration = 0.6f;
	State.GetUpDuration = 0.8f;
	float GetUpRate = 1.2f;
	KnockdownRules::ScaleTimeline(State, GetUpRate, Config.RamKnockdownPlayRate, Config.RamDownedTimeScale);
	TestEqual(TEXT("fall clip at 0.65x: lasts 1 / 0.65 longer"), State.FallDuration, 0.9f / 0.65f, 0.001f);
	TestEqual(TEXT("downed 1.5x"), State.DownedDuration, 0.9f, 0.001f);
	TestEqual(TEXT("get-up = clip length (0.8 s x 1.2) / 0.65"), State.GetUpDuration, 0.8f * 1.2f / 0.65f, 0.001f);
	TestEqual(TEXT("get-up clip rate 0.65 (absolute)"), GetUpRate, 0.65f, 0.001f);
	return true;
}

VEHICLE_COMBAT_TEST(FVehicleFadeTimelineTest, "Fade.Timeline")
bool FVehicleFadeTimelineTest::RunTest(const FString&)
{
	const FVehicleFadeConfig Config;
	FVehicleFadeTimeline Fade;
	TestEqual(TEXT("idle: clear"), Fade.GetAlpha(Config), 0.f);
	TestTrue(TEXT("a request starts it"), Fade.Request(Config));
	TestFalse(TEXT("half way out: not black yet"), Fade.Advance(0.125f, Config));
	TestEqual(TEXT("half dark"), Fade.GetAlpha(Config), 0.5f, 0.01f);
	TestTrue(TEXT("a second boarder joins the fade-out"), Fade.Request(Config));
	TestTrue(TEXT("0.25 s: black (move them now)"), Fade.Advance(0.13f, Config));
	TestTrue(TEXT("black"), Fade.IsBlack() && FMath::IsNearlyEqual(Fade.GetAlpha(Config), 1.f));
	Fade.Advance(0.2f, Config);
	TestEqual(TEXT("then back"), static_cast<int32>(Fade.Phase), static_cast<int32>(EVehicleFadePhase::FadeIn));
	Fade.Advance(0.3f, Config);
	TestFalse(TEXT("clear again after 0.25 s"), Fade.IsActive());
	TestEqual(TEXT("alpha 0"), Fade.GetAlpha(Config), 0.f);
	// Joining during the fade-in queues one more cycle (never two overlapping fades).
	Fade.Request(Config);
	Fade.Advance(0.3f, Config);
	Fade.Advance(0.2f, Config);
	TestFalse(TEXT("a late request during the fade-in does not join it"), Fade.Request(Config));
	Fade.Advance(0.3f, Config);
	TestEqual(TEXT("the queued cycle starts"), static_cast<int32>(Fade.Phase), static_cast<int32>(EVehicleFadePhase::FadeOut));
	// Never stuck black: late joiners stretch the hold, at most MaxHoldSeconds.
	FVehicleFadeTimeline Held;
	Held.Request(Config);
	Held.Advance(0.3f, Config);
	float Black = 0.f;
	while (Held.IsBlack() && Black < 5.f)
	{
		Held.Request(Config);
		Held.Advance(0.05f, Config);
		Black += 0.05f;
	}
	TestTrue(TEXT("hold bounded by MaxHoldSeconds"), Black <= Config.MaxHoldSeconds + 0.06f);
	return true;
}

VEHICLE_COMBAT_TEST(FVehicleFadeGroupTest, "Fade.GroupBoarding")
bool FVehicleFadeGroupTest::RunTest(const FString&)
{
	const FVehicleFadeConfig Config;
	TestTrue(TEXT("alone: fade at once"), VehicleCombatRules::ShouldStartGroupFade(0.f, {}, Config));
	// The squad ran from 10 m: the mates arrive in ~3 s -> the first one waits for them (one fade).
	const float Eta = VehicleCombatRules::EstimateArrivalSeconds(1000.f, 350.f);
	TestEqual(TEXT("10 m at 3.5 m/s round the hull (x1.8) = 5.1 s"), Eta, 1000.f * 1.8f / 350.f, 0.01f);
	TestFalse(TEXT("a mate arriving in time: wait for him"), VehicleCombatRules::ShouldStartGroupFade(0.5f, { Eta }, Config));
	TestTrue(TEXT("a straggler 30 m away: no wait"), VehicleCombatRules::ShouldStartGroupFade(0.5f, { VehicleCombatRules::EstimateArrivalSeconds(3000.f, 350.f) }, Config));
	TestTrue(TEXT("waited the max: go"), VehicleCombatRules::ShouldStartGroupFade(Config.GroupWaitSeconds, { 0.5f }, Config));
	TestEqual(TEXT("a standing walker counts at the floor speed"), VehicleCombatRules::EstimateArrivalSeconds(300.f, 0.f, 150.f, 1.f), 2.f, 0.01f);
	return true;
}

VEHICLE_COMBAT_TEST(FVehicleWheelSpinTest, "Drive.WheelSpinFromGroundSpeed")
bool FVehicleWheelSpinTest::RunTest(const FString&)
{
	FVehicleDriveConfig Config;
	// 6 m/s on 60 cm wheels: 10 rad/s = 573 deg/s; the ABP value scales by its integration factor.
	TestEqual(TEXT("no-slip spin"), VehicleRules::GetWheelSpinDegPerSec(600.f, 60.f), FMath::RadiansToDegrees(10.f), 0.01f);
	TestEqual(TEXT("spin x sign"), VehicleRules::GetAnimWheelSpeed(600.f, 60.f, -1.f), -FMath::RadiansToDegrees(10.f), 0.01f);
	// The vendor ABP shows WheelSpeed x its clock: with the clock pinned to 1 (+ this frame), the angle comes out exact,
	// whatever the speed did before (no whirl when it changes).
	const float Angle = VehicleRules::StepWheelAngle(350.f, 573.f, 0.05f);
	TestEqual(TEXT("angle wraps at 360"), Angle, FMath::Fmod(350.f + 573.f * 0.05f, 360.f), 0.01f);
	TestEqual(TEXT("backwards wraps too"), VehicleRules::StepWheelAngle(5.f, -200.f, 0.05f), 355.f, 0.01f);
	TestEqual(TEXT("WheelSpeed x clock = the angle"), VehicleRules::GetAnimWheelSpeedForAngle(Angle, 1.f, 0.016f) * (1.f + 0.016f), Angle, 0.01f);
	TestEqual(TEXT("standing: the same angle, no spin"), VehicleRules::GetAnimWheelSpeedForAngle(42.f, 1.f, 0.016f) * 1.016f, 42.f, 0.01f);
	TestEqual(TEXT("reverse spins backwards"), VehicleRules::GetAnimWheelSpeed(-250.f, 60.f, 1.f), -FMath::RadiansToDegrees(250.f / 60.f), 0.01f);
	TestEqual(TEXT("standing: exactly 0"), VehicleRules::GetAnimWheelSpeed(0.5f, 60.f, 1.f), 0.f);
	TestEqual(TEXT("proportional"), VehicleRules::GetAnimWheelSpeed(300.f, 60.f, 1.f) * 2.f, VehicleRules::GetAnimWheelSpeed(600.f, 60.f, 1.f), 0.01f);
	// Pulling away: the acceleration ramps up from 30 % to full in 0.8 s.
	TestEqual(TEXT("launch 30 %"), VehicleRules::GetLaunchAccelerationFactor(0.f, Config), 0.3f, 0.001f);
	TestEqual(TEXT("half way"), VehicleRules::GetLaunchAccelerationFactor(0.4f, Config), 0.65f, 0.001f);
	TestEqual(TEXT("full after the ramp"), VehicleRules::GetLaunchAccelerationFactor(2.f, Config), 1.f, 0.001f);
	Config.AccelerationRampSeconds = 0.f;
	TestEqual(TEXT("no ramp: full at once"), VehicleRules::GetLaunchAccelerationFactor(0.f, Config), 1.f, 0.001f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
