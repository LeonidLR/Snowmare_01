#include "Misc/AutomationTest.h"
#include "Characters/EnemyAIRules.h"
#include "Vehicles/VehicleHullRules.h"
#include "Vehicles/VehicleRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// APC P4 (hull damage, disabled / repair, enemies vs the manned APC, cannon shots at objects) - pure rules
// (docs/vehicles/APC_02_DESIGN.md §1.5, APC_00_DECISIONS.md, user decisions 2026-10-12). UE-only: design-number tests.

#define VEHICLE_HULL_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Vehicle." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

VEHICLE_HULL_TEST(FVehicleHullArmourTest, "Hull.DamageBySource")
bool FVehicleHullArmourTest::RunTest(const FString&)
{
	const FVehicleHullConfig Config;
	TestEqual(TEXT("hull 1200 HP"), Config.MaxHealth, 1200.f);
	// Hound 12 / frostbitten 15 / cutter 18 claws x0.1; the Brute's 35 in full; spitter acid x0.5; marksman 45 x0.25.
	TestEqual(TEXT("hound bite x0.1"), VehicleHullRules::GetHullDamage(12.f, EVehicleDamageSource::SmallMelee, Config), 1.2f, 0.001f);
	TestEqual(TEXT("Brute blow in full"), VehicleHullRules::GetHullDamage(35.f, EVehicleDamageSource::HeavyMelee, Config), 35.f);
	TestEqual(TEXT("spitter acid x0.5"), VehicleHullRules::GetHullDamage(18.f, EVehicleDamageSource::Acid, Config), 9.f);
	TestEqual(TEXT("marksman x0.25"), VehicleHullRules::GetHullDamage(45.f, EVehicleDamageSource::Rifle, Config), 11.25f);
	TestEqual(TEXT("barrel blast in full"), VehicleHullRules::GetHullDamage(120.f, EVehicleDamageSource::Explosion, Config), 120.f);
	TestEqual(TEXT("a mine under the wheels: flat 250"), VehicleHullRules::GetHullDamage(0.f, EVehicleDamageSource::Mine, Config), 250.f);
	TestEqual(TEXT("ram: no armour factor"), VehicleHullRules::GetHullDamage(30.f, EVehicleDamageSource::Ram, Config), 30.f);
	TestEqual(TEXT("never negative"), VehicleHullRules::GetHullDamage(-5.f, EVehicleDamageSource::HeavyMelee, Config), 0.f);
	// Who hits with what.
	TestTrue(TEXT("Brute = heavy melee"), VehicleHullRules::ClassifyEnemyAttack(EEnemyArchetype::Brute) == EVehicleDamageSource::HeavyMelee);
	TestTrue(TEXT("hound / frostbitten / cutter = small melee"), VehicleHullRules::ClassifyEnemyAttack(EEnemyArchetype::FrostHound) == EVehicleDamageSource::SmallMelee
		&& VehicleHullRules::ClassifyEnemyAttack(EEnemyArchetype::Frostbitten) == EVehicleDamageSource::SmallMelee
		&& VehicleHullRules::ClassifyEnemyAttack(EEnemyArchetype::Cutter) == EVehicleDamageSource::SmallMelee);
	TestTrue(TEXT("spitter / drone = acid"), VehicleHullRules::ClassifyEnemyAttack(EEnemyArchetype::Spitter) == EVehicleDamageSource::Acid
		&& VehicleHullRules::ClassifyEnemyAttack(EEnemyArchetype::CryoDrone) == EVehicleDamageSource::Acid);
	TestTrue(TEXT("marksman = rifle"), VehicleHullRules::ClassifyEnemyAttack(EEnemyArchetype::Marksman) == EVehicleDamageSource::Rifle);
	// Blasts: the vehicle that set the mine off itself takes the flat mine damage, the others the blast's falloff.
	EVehicleDamageSource Source = EVehicleDamageSource::Other;
	TestEqual(TEXT("own mine: 250"), VehicleHullRules::GetBlastHullDamage(40.f, true, Config, Source), 250.f);
	TestTrue(TEXT("own mine: source Mine"), Source == EVehicleDamageSource::Mine);
	TestEqual(TEXT("a blast beside it: its falloff"), VehicleHullRules::GetBlastHullDamage(40.f, false, Config, Source), 40.f);
	TestTrue(TEXT("a blast beside it: Explosion"), Source == EVehicleDamageSource::Explosion);
	return true;
}

VEHICLE_HULL_TEST(FVehicleHullDisableTest, "Hull.DisabledAtZero")
bool FVehicleHullDisableTest::RunTest(const FString&)
{
	const FVehicleHullConfig Config;
	FVehicleHullState State = VehicleHullRules::MakeFullHull(Config);
	TestTrue(TEXT("fresh: full, running"), State.Health == 1200.f && !State.bDisabled);
	float Dealt = 0.f;
	TestTrue(TEXT("a hit: damaged"), VehicleHullRules::ApplyDamage(State, 250.f, Config, Dealt) == EVehicleHullChange::Damaged && Dealt == 250.f);
	TestTrue(TEXT("650 more: 300 left = the 25 % low mark"), VehicleHullRules::ApplyDamage(State, 650.f, Config, Dealt) == EVehicleHullChange::DamagedLow);
	TestTrue(TEXT("the low mark is reported once"), VehicleHullRules::ApplyDamage(State, 10.f, Config, Dealt) == EVehicleHullChange::Damaged);
	TestTrue(TEXT("down to 0: disabled (never destroyed)"), VehicleHullRules::ApplyDamage(State, 1000.f, Config, Dealt) == EVehicleHullChange::Disabled
		&& State.bDisabled && State.Health == 0.f && Dealt == 290.f);
	TestTrue(TEXT("a disabled hull takes nothing more"), VehicleHullRules::ApplyDamage(State, 100.f, Config, Dealt) == EVehicleHullChange::None && Dealt == 0.f);
	TestTrue(TEXT("hud: disabled"), VehicleHullRules::GetHudLine(State, Config, 0.f).Contains(TEXT("DISABLED")));
	TestTrue(TEXT("hud: repairing"), VehicleHullRules::GetHudLine(State, Config, 0.6f).Contains(TEXT("60%")));
	// Boarding / driving are refused while disabled.
	VehicleRules::FBoardCheck Board;
	Board.bVehicleDisabled = true;
	TestTrue(TEXT("no boarding a disabled hull"), VehicleRules::CanBoard(Board, FVehicleCrewConfig()) == EVehicleRefusal::Disabled);
	TestTrue(TEXT("no driving a disabled hull"), VehicleRules::CanDrive(true, false, false, true) == EVehicleRefusal::Disabled);
	TestTrue(TEXT("a running hull drives"), VehicleRules::CanDrive(true, false, false, false) == EVehicleRefusal::None);
	return true;
}

VEHICLE_HULL_TEST(FVehicleHullRepairTest, "Hull.Repair")
bool FVehicleHullRepairTest::RunTest(const FString&)
{
	const FVehicleHullConfig Config;
	TestEqual(TEXT("the Engineer: 5 s"), VehicleHullRules::GetRepairSeconds(EOperativeRole::Engineer, Config), 5.f);
	TestTrue(TEXT("the others cannot (0)"), VehicleHullRules::GetRepairSeconds(EOperativeRole::Commander, Config) <= 0.f
		&& VehicleHullRules::GetRepairSeconds(EOperativeRole::MedicSapper, Config) <= 0.f);
	FVehicleHullConfig Anyone = Config;
	Anyone.OtherRepairSeconds = 12.f;
	TestEqual(TEXT("data: others may repair, slower"), VehicleHullRules::GetRepairSeconds(EOperativeRole::Recruit, Anyone), 12.f);
	FVehicleHullState State;
	State.Health = 0.f;
	State.bDisabled = true;
	VehicleHullRules::Repair(State, Config);
	TestTrue(TEXT("repaired: running at 50 % (600)"), !State.bDisabled && State.Health == 600.f);
	TestTrue(TEXT("hud: health"), VehicleHullRules::GetHudLine(State, Config, 0.f) == TEXT("HULL 600/1200"));
	// Save / load.
	const FVehicleHullState Saved = VehicleHullRules::RestoreHull(840.f, false, Config);
	TestTrue(TEXT("load: 840 running"), Saved.Health == 840.f && !Saved.bDisabled);
	const FVehicleHullState Dead = VehicleHullRules::RestoreHull(300.f, true, Config);
	TestTrue(TEXT("load: disabled = 0 HP"), Dead.bDisabled && Dead.Health == 0.f);
	TestTrue(TEXT("load: 0 HP = disabled"), VehicleHullRules::RestoreHull(0.f, false, Config).bDisabled);
	TestEqual(TEXT("load: clamped to the max"), VehicleHullRules::RestoreHull(5000.f, false, Config).Health, 1200.f);
	return true;
}

VEHICLE_HULL_TEST(FVehicleHullEnemyTargetTest, "Hull.EnemiesTargetTheMannedApc")
bool FVehicleHullEnemyTargetTest::RunTest(const FString&)
{
	const FVehicleHullConfig Config;
	TestTrue(TEXT("crew inside, 20 m: a target"), VehicleHullRules::IsEnemyTarget(2, false, false, 2000.f, Config));
	TestFalse(TEXT("empty: no target"), VehicleHullRules::IsEnemyTarget(0, false, false, 500.f, Config));
	TestFalse(TEXT("disabled: no target (the crew is out)"), VehicleHullRules::IsEnemyTarget(2, true, false, 500.f, Config));
	TestFalse(TEXT("60 m off, a wanderer: unnoticed"), VehicleHullRules::IsEnemyTarget(2, false, false, 6000.f, Config));
	TestTrue(TEXT("60 m off, a horde: knows"), VehicleHullRules::IsEnemyTarget(2, false, true, 6000.f, Config));
	// The target pick: the APC competes with the operatives at its weight (hound 1.0, Brute 0.85).
	const FEnemyAIConfig AI;
	TArray<FEnemyTargetCandidate> Candidates;
	Candidates.Add({ EEnemyTargetKind::Operative, FVector(1500.f, 0.f, 0.f), true });
	FEnemyTargetCandidate Apc{ EEnemyTargetKind::Vehicle, FVector(1000.f, 0.f, 0.f), true };
	Apc.Weight = VehicleHullRules::GetEnemyTargetWeight(true, Config);
	Candidates.Add(Apc);
	TestEqual(TEXT("hound: the APC is the closest threat"), EnemyAIRules::SelectTarget(AI, true, FVector::ZeroVector, Candidates, false), 1);
	Candidates[1].Location = FVector(2000.f, 0.f, 0.f);
	TestEqual(TEXT("hound: an operative closer than the hull"), EnemyAIRules::SelectTarget(AI, true, FVector::ZeroVector, Candidates, false), 0);
	// A Brute weighs operatives 0.7: an operative at 15 m (10.5) beats the hull at 13 m (x0.85 = 11.05)...
	Candidates[1].Location = FVector(1300.f, 0.f, 0.f);
	Candidates[1].Weight = VehicleHullRules::GetEnemyTargetWeight(false, Config);
	TestEqual(TEXT("Brute: the operative at 15 m vs the hull at 13 m"), EnemyAIRules::SelectTarget(AI, false, FVector::ZeroVector, Candidates, false), 0);
	// ... the hull at 10 m (8.5) wins; everyone aboard: the hull only.
	Candidates[1].Location = FVector(1000.f, 0.f, 0.f);
	TestEqual(TEXT("Brute: the hull at 10 m"), EnemyAIRules::SelectTarget(AI, false, FVector::ZeroVector, Candidates, false), 1);
	Candidates[0].bUsable = false;
	Candidates[1].Location = FVector(3000.f, 0.f, 0.f);
	TestEqual(TEXT("all aboard: the hull"), EnemyAIRules::SelectTarget(AI, true, FVector::ZeroVector, Candidates, false), 1);
	return true;
}

VEHICLE_HULL_TEST(FVehicleHullEjectionTest, "Hull.EjectionExits")
bool FVehicleHullEjectionTest::RunTest(const FString&)
{
	// Exits: left side (y -), right side (y +), the rear; a threat on the right.
	const TArray<FVector> Exits = { FVector(0.f, -260.f, 0.f), FVector(0.f, 260.f, 0.f), FVector(-500.f, 0.f, 0.f) };
	const TArray<FVector> Threats = { FVector(0.f, 3000.f, 0.f) };
	TArray<int32> Order = VehicleRules::GetEjectionOrder(Exits, { true, true, true }, FVector::ZeroVector, Threats, FVector::ZeroVector);
	TestEqual(TEXT("all exits used once"), Order.Num(), 3);
	TestEqual(TEXT("the safe side first (away from the threat)"), Order[0], 0);
	TestEqual(TEXT("the threat side last"), Order.Last(), 1);
	// The safe side blocked: still everyone gets out - the free ones first, then the blocked ones.
	Order = VehicleRules::GetEjectionOrder(Exits, { false, true, true }, FVector::ZeroVector, Threats, FVector::ZeroVector);
	TestEqual(TEXT("blocked: all three still listed"), Order.Num(), 3);
	TestEqual(TEXT("blocked: the rear (free, away) first"), Order[0], 2);
	TestEqual(TEXT("blocked: the blocked exit after the free ones"), Order[2], 0);
	return true;
}

VEHICLE_HULL_TEST(FVehicleObjectShotTest, "Turret.ObjectShotLegality")
bool FVehicleObjectShotTest::RunTest(const FString&)
{
	VehicleHullRules::FObjectShotCheck Check;
	Check.DistanceCm = 2000.f;
	Check.HullDistanceCm = 1600.f;
	TestTrue(TEXT("a barrel at 20 m, clear line: fire"), VehicleHullRules::CheckObjectShot(Check) == EVehicleObjectShot::None);
	VehicleHullRules::FObjectShotCheck Each = Check;
	Each.bShootable = false;
	TestTrue(TEXT("not a barrel / trap (or blown): ignored"), VehicleHullRules::CheckObjectShot(Each) == EVehicleObjectShot::NotATarget);
	Each = Check;
	Each.bHasGunner = false;
	TestTrue(TEXT("no gunner"), VehicleHullRules::CheckObjectShot(Each) == EVehicleObjectShot::NoGunner);
	Each = Check;
	Each.bDisabled = true;
	TestTrue(TEXT("disabled hull: no power"), VehicleHullRules::CheckObjectShot(Each) == EVehicleObjectShot::Disabled);
	Each = Check;
	Each.bHeld = true;
	TestTrue(TEXT("turn-based / AI pause: held"), VehicleHullRules::CheckObjectShot(Each) == EVehicleObjectShot::Held);
	Each = Check;
	Each.ShellsLeft = 0;
	TestTrue(TEXT("no shells"), VehicleHullRules::CheckObjectShot(Each) == EVehicleObjectShot::NoShells);
	Each = Check;
	Each.DistanceCm = 4100.f;
	TestTrue(TEXT("beyond 40 m"), VehicleHullRules::CheckObjectShot(Each) == EVehicleObjectShot::OutOfRange);
	Each = Check;
	Each.HullDistanceCm = 300.f;
	TestTrue(TEXT("within the 4 m dead zone"), VehicleHullRules::CheckObjectShot(Each) == EVehicleObjectShot::DeadZone);
	Each = Check;
	Each.bLineOfFire = false;
	TestTrue(TEXT("behind a wall"), VehicleHullRules::CheckObjectShot(Each) == EVehicleObjectShot::NoLineOfFire);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
