#include "Misc/AutomationTest.h"
#include "Vehicles/VehicleRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// APC P0 + P1 pure rules (docs/vehicles/APC_01_TECHNICAL.md, APC_02_DESIGN.md; user decisions 2026-10-12). UE-only:
// Godot had no vehicles, so these are design-number tests, not parity tests.

#define VEHICLE_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Vehicle." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

VEHICLE_TEST(FVehicleSeatsSquadTest, "Seats.EngineerDrivesCommanderGuns")
bool FVehicleSeatsSquadTest::RunTest(const FString&)
{
	const FVehicleCrewConfig Config;
	TestEqual(TEXT("7 seats"), VehicleRules::GetCapacity(Config), 7);
	// Boarding order Commander, Engineer, Medic-Sapper: the Engineer drives, the Commander guns, the medic rides.
	const TArray<EVehicleSeat> Seats = VehicleRules::AssignSeats({ EOperativeRole::Commander, EOperativeRole::Engineer, EOperativeRole::MedicSapper }, Config);
	TestEqual(TEXT("Commander gunner"), Seats[0], EVehicleSeat::Gunner);
	TestEqual(TEXT("Engineer driver"), Seats[1], EVehicleSeat::Driver);
	TestEqual(TEXT("Medic passenger"), Seats[2], EVehicleSeat::Passenger);
	// The recruit too (4 aboard).
	const TArray<EVehicleSeat> Four = VehicleRules::AssignSeats({ EOperativeRole::Recruit, EOperativeRole::MedicSapper, EOperativeRole::Engineer, EOperativeRole::Commander }, Config);
	TestEqual(TEXT("recruit rides"), Four[0], EVehicleSeat::Passenger);
	TestEqual(TEXT("engineer drives"), Four[2], EVehicleSeat::Driver);
	TestEqual(TEXT("commander guns"), Four[3], EVehicleSeat::Gunner);
	return true;
}

VEHICLE_TEST(FVehicleSeatsAbsentTest, "Seats.AnyoneIfAbsent")
bool FVehicleSeatsAbsentTest::RunTest(const FString&)
{
	const FVehicleCrewConfig Config;
	// A crew of one always drives (the Commander alone too).
	TestEqual(TEXT("commander alone drives"), VehicleRules::AssignSeats({ EOperativeRole::Commander }, Config)[0], EVehicleSeat::Driver);
	TestEqual(TEXT("medic alone drives"), VehicleRules::AssignSeats({ EOperativeRole::MedicSapper }, Config)[0], EVehicleSeat::Driver);
	// No engineer: the first non-commander drives, the commander still guns.
	const TArray<EVehicleSeat> NoEngineer = VehicleRules::AssignSeats({ EOperativeRole::Commander, EOperativeRole::MedicSapper, EOperativeRole::Recruit }, Config);
	TestEqual(TEXT("medic drives"), NoEngineer[1], EVehicleSeat::Driver);
	TestEqual(TEXT("commander guns"), NoEngineer[0], EVehicleSeat::Gunner);
	TestEqual(TEXT("recruit rides"), NoEngineer[2], EVehicleSeat::Passenger);
	// No commander: the next one aboard guns.
	const TArray<EVehicleSeat> NoCommander = VehicleRules::AssignSeats({ EOperativeRole::MedicSapper, EOperativeRole::Engineer }, Config);
	TestEqual(TEXT("engineer drives"), NoCommander[1], EVehicleSeat::Driver);
	TestEqual(TEXT("medic guns"), NoCommander[0], EVehicleSeat::Gunner);
	// Beyond the capacity: no seat.
	FVehicleCrewConfig Small;
	Small.PassengerSeats = 0;
	const TArray<EVehicleSeat> Full = VehicleRules::AssignSeats({ EOperativeRole::Engineer, EOperativeRole::Commander, EOperativeRole::Recruit }, Small);
	TestEqual(TEXT("third has no seat"), Full[2], EVehicleSeat::None);
	TestEqual(TEXT("seat names round trip"), VehicleRules::ParseSeatName(VehicleRules::GetSeatName(EVehicleSeat::Gunner)), EVehicleSeat::Gunner);
	return true;
}

VEHICLE_TEST(FVehicleBoardLegalityTest, "Board.Legality")
bool FVehicleBoardLegalityTest::RunTest(const FString&)
{
	const FVehicleCrewConfig Config;
	VehicleRules::FBoardCheck Ok;
	Ok.DistanceCm = 1200.f;
	TestEqual(TEXT("in range, stopped, seats free"), VehicleRules::CanBoard(Ok, Config), EVehicleRefusal::None);
	VehicleRules::FBoardCheck Check = Ok;
	Check.DistanceCm = 3100.f;
	TestEqual(TEXT("beyond 30 m"), VehicleRules::CanBoard(Check, Config), EVehicleRefusal::TooFar);
	Check = Ok;
	Check.VehicleSpeedCmS = 300.f;
	TestEqual(TEXT("moving"), VehicleRules::CanBoard(Check, Config), EVehicleRefusal::VehicleMoving);
	Check = Ok;
	Check.FreeSeats = 0;
	TestEqual(TEXT("full"), VehicleRules::CanBoard(Check, Config), EVehicleRefusal::NoSeat);
	Check = Ok;
	Check.bMounted = true;
	TestEqual(TEXT("already aboard"), VehicleRules::CanBoard(Check, Config), EVehicleRefusal::AlreadyMounted);
	Check = Ok;
	Check.bKnockedDown = true;
	TestEqual(TEXT("knocked down"), VehicleRules::CanBoard(Check, Config), EVehicleRefusal::KnockedDown);
	Check = Ok;
	Check.bAlive = false;
	TestEqual(TEXT("dead"), VehicleRules::CanBoard(Check, Config), EVehicleRefusal::Dead);
	Check = Ok;
	Check.bTurnBased = true;
	TestEqual(TEXT("turn-based (P3)"), VehicleRules::CanBoard(Check, Config), EVehicleRefusal::TurnBased);
	Check = Ok;
	Check.bTacticalPause = true;
	TestEqual(TEXT("tactical pause (P1: refused)"), VehicleRules::CanBoard(Check, Config), EVehicleRefusal::TacticalPause);
	return true;
}

VEHICLE_TEST(FVehicleDisembarkDriveLegalityTest, "Disembark.Legality")
bool FVehicleDisembarkDriveLegalityTest::RunTest(const FString&)
{
	TestEqual(TEXT("aboard, real time, exit free"), VehicleRules::CanDisembark(true, false, false, true), EVehicleRefusal::None);
	TestEqual(TEXT("not aboard"), VehicleRules::CanDisembark(false, false, false, true), EVehicleRefusal::NotMounted);
	TestEqual(TEXT("all exits blocked"), VehicleRules::CanDisembark(true, false, false, false), EVehicleRefusal::NoExit);
	TestEqual(TEXT("turn-based"), VehicleRules::CanDisembark(true, true, false, true), EVehicleRefusal::TurnBased);
	TestEqual(TEXT("pause"), VehicleRules::CanDisembark(true, false, true, true), EVehicleRefusal::TacticalPause);
	TestEqual(TEXT("drive with a driver"), VehicleRules::CanDrive(true, false, false), EVehicleRefusal::None);
	TestEqual(TEXT("no driver"), VehicleRules::CanDrive(false, false, false), EVehicleRefusal::NoDriver);
	TestEqual(TEXT("no driving in the pause (P1)"), VehicleRules::CanDrive(true, false, true), EVehicleRefusal::TacticalPause);
	TestEqual(TEXT("no driving turn-based (P3)"), VehicleRules::CanDrive(true, true, false), EVehicleRefusal::TurnBased);
	// The Y key.
	TestEqual(TEXT("nobody"), VehicleRules::ResolveKeyAction(0, 0), EVehicleKeyAction::None);
	TestEqual(TEXT("all outside: board"), VehicleRules::ResolveKeyAction(3, 0), EVehicleKeyAction::Board);
	TestEqual(TEXT("some aboard: the rest board"), VehicleRules::ResolveKeyAction(3, 2), EVehicleKeyAction::Board);
	TestEqual(TEXT("all aboard: get out"), VehicleRules::ResolveKeyAction(3, 3), EVehicleKeyAction::Disembark);
	return true;
}

VEHICLE_TEST(FVehicleExitChoiceTest, "Exit.AwayFromThreat")
bool FVehicleExitChoiceTest::RunTest(const FString&)
{
	// Vehicle at the origin heading +X; exits: left side (y -), right side (y +), rear.
	const TArray<FVector> Exits = { FVector(0.f, -260.f, 0.f), FVector(0.f, 260.f, 0.f), FVector(-450.f, 0.f, 0.f) };
	const TArray<bool> AllFree = { true, true, true };
	// A hound on the right (y +20 m): the left exit first, the right one last.
	TArray<int32> Ranked = VehicleRules::RankExitPoints(Exits, AllFree, FVector::ZeroVector, { FVector(300.f, 2000.f, 0.f) }, FVector(0.f, 3000.f, 2000.f));
	TestEqual(TEXT("threat right -> left first"), Ranked[0], 0);
	TestEqual(TEXT("threat right -> right last"), Ranked.Last(), 1);
	// Threat on the left: the right exit.
	Ranked = VehicleRules::RankExitPoints(Exits, AllFree, FVector::ZeroVector, { FVector(0.f, -1500.f, 0.f), FVector(0.f, 4000.f, 0.f) }, FVector::ZeroVector);
	TestEqual(TEXT("nearest threat left -> right first"), Ranked[0], 1);
	// Threat ahead: the rear hatch.
	Ranked = VehicleRules::RankExitPoints(Exits, AllFree, FVector::ZeroVector, { FVector(2500.f, 0.f, 0.f) }, FVector::ZeroVector);
	TestEqual(TEXT("threat ahead -> rear first"), Ranked[0], 2);
	// Blocked exit is never used.
	Ranked = VehicleRules::RankExitPoints(Exits, { false, true, true }, FVector::ZeroVector, { FVector(0.f, 2000.f, 0.f) }, FVector::ZeroVector);
	TestFalse(TEXT("blocked left skipped"), Ranked.Contains(0));
	TestEqual(TEXT("two left"), Ranked.Num(), 2);
	// No threats: the side nearest the camera.
	Ranked = VehicleRules::RankExitPoints(Exits, AllFree, FVector::ZeroVector, {}, FVector(0.f, -3000.f, 2000.f));
	TestEqual(TEXT("camera left -> left first"), Ranked[0], 0);
	// All blocked: nothing.
	TestEqual(TEXT("all blocked"), VehicleRules::RankExitPoints(Exits, { false, false, false }, FVector::ZeroVector, {}, FVector::ZeroVector).Num(), 0);
	return true;
}

VEHICLE_TEST(FVehicleTurningRadiusTest, "Drive.TurningRadius")
bool FVehicleTurningRadiusTest::RunTest(const FString&)
{
	FVehicleDriveConfig Config;
	Config.MinTurnRadiusCm = 700.f;
	// Full steering lock at cruise for 20 s with a goal always 90 deg to the left: the circle it drives has R >= 7 m.
	FVector2D Position(0.f, 0.f);
	float Yaw = 0.f;
	const float Speed = Config.CruiseSpeedCmS;
	float MinX = 0.f, MaxX = 0.f, MinY = 0.f, MaxY = 0.f;
	for (int32 Step = 0; Step < 2000; ++Step)
	{
		Yaw = VehicleRules::StepYaw(Yaw, Yaw + 90.f, Speed, Config.MinTurnRadiusCm, 0.01f);
		Position += FVector2D(FMath::Cos(FMath::DegreesToRadians(Yaw)), FMath::Sin(FMath::DegreesToRadians(Yaw))) * Speed * 0.01f;
		MinX = FMath::Min(MinX, Position.X); MaxX = FMath::Max(MaxX, Position.X);
		MinY = FMath::Min(MinY, Position.Y); MaxY = FMath::Max(MaxY, Position.Y);
	}
	const float Diameter = FMath::Max(MaxX - MinX, MaxY - MinY);
	TestTrue(*FString::Printf(TEXT("turning circle diameter %.0f cm >= 2 R"), Diameter), Diameter >= 2.f * Config.MinTurnRadiusCm - 20.f);
	TestTrue(TEXT("and not wider than the lock allows (no drift)"), Diameter <= 2.f * Config.MinTurnRadiusCm + 40.f);
	// Yaw rate = v / R: 600 / 700 rad/s = 49.1 deg/s; standing still: no turn at all (no pivot).
	TestEqual(TEXT("max yaw rate"), VehicleRules::GetMaxYawRateDeg(600.f, 700.f), FMath::RadiansToDegrees(600.f / 700.f), 0.01f);
	TestEqual(TEXT("no pivot when stopped"), VehicleRules::StepYaw(10.f, 120.f, 0.f, 700.f, 1.f), 10.f, 0.001f);
	TestEqual(TEXT("small corrections are exact"), VehicleRules::StepYaw(10.f, 12.f, 600.f, 700.f, 0.1f), 12.f, 0.001f);
	TestEqual(TEXT("wraps across 180"), VehicleRules::StepYaw(175.f, -175.f, 600.f, 700.f, 1.f), -175.f, 0.001f);
	return true;
}

VEHICLE_TEST(FVehicleReverseSpeedTest, "Drive.ReverseAndSpeed")
bool FVehicleReverseSpeedTest::RunTest(const FString&)
{
	const FVehicleDriveConfig Config;
	const FVector2D Forward(1.f, 0.f);
	TestTrue(TEXT("8 m straight behind: reverse"), VehicleRules::ShouldReverse(Forward, FVector2D(-800.f, 0.f), Config));
	TestTrue(TEXT("behind at 130 deg, 10 m"), VehicleRules::ShouldReverse(Forward, FVector2D(-643.f, 766.f), Config));
	TestFalse(TEXT("behind but 20 m: turn round"), VehicleRules::ShouldReverse(Forward, FVector2D(-2000.f, 0.f), Config));
	TestFalse(TEXT("to the side (90 deg): forward"), VehicleRules::ShouldReverse(Forward, FVector2D(0.f, 800.f), Config));
	TestFalse(TEXT("ahead: forward"), VehicleRules::ShouldReverse(Forward, FVector2D(800.f, 0.f), Config));
	// Speeds.
	TestEqual(TEXT("cruise"), VehicleRules::GetDesiredSpeed(false, false, 5000.f, 0.f, Config), Config.CruiseSpeedCmS);
	TestEqual(TEXT("rush"), VehicleRules::GetDesiredSpeed(true, false, 5000.f, 0.f, Config), Config.RushSpeedCmS);
	TestEqual(TEXT("reverse"), VehicleRules::GetDesiredSpeed(false, true, 5000.f, 0.f, Config), Config.ReverseSpeedCmS);
	TestEqual(TEXT("slows to turn"), VehicleRules::GetDesiredSpeed(true, false, 5000.f, 90.f, Config), Config.MinTurnSpeedCmS);
	TestTrue(TEXT("brakes for the goal"), VehicleRules::GetDesiredSpeed(true, false, 100.f, 0.f, Config) < 400.f);
	// Acceleration / braking.
	TestEqual(TEXT("accelerates 3 m/s2"), VehicleRules::StepSpeed(0.f, 600.f, Config, 1.f), Config.AccelerationCmS2, 0.01f);
	TestEqual(TEXT("brakes 6 m/s2"), VehicleRules::StepSpeed(600.f, 0.f, Config, 0.5f), 600.f - Config.BrakingCmS2 * 0.5f, 0.01f);
	TestEqual(TEXT("reaches the target"), VehicleRules::StepSpeed(590.f, 600.f, Config, 1.f), 600.f, 0.01f);
	TestEqual(TEXT("into reverse brakes first"), VehicleRules::StepSpeed(100.f, -250.f, Config, 0.1f), 100.f - Config.BrakingCmS2 * 0.1f, 0.01f);
	return true;
}

VEHICLE_TEST(FVehicleLookaheadTest, "Drive.PurePursuit")
bool FVehicleLookaheadTest::RunTest(const FString&)
{
	// An L-shaped path: 0 -> (1000, 0) -> (1000, 1000).
	const TArray<FVector> Path = { FVector(0.f, 0.f, 0.f), FVector(1000.f, 0.f, 0.f), FVector(1000.f, 1000.f, 0.f) };
	int32 Segment = 0;
	float Remaining = 0.f;
	FVector Look = VehicleRules::GetLookaheadPoint(Path, FVector(200.f, 30.f, 0.f), Segment, 400.f, Remaining);
	TestEqual(TEXT("4 m ahead on the first leg"), static_cast<float>(Look.X), 600.f, 1.f);
	TestEqual(TEXT("remaining 18 m"), Remaining, 1800.f, 1.f);
	Look = VehicleRules::GetLookaheadPoint(Path, FVector(800.f, 0.f, 0.f), Segment, 400.f, Remaining);
	// 2 m to the 90 deg corner, then only 1 m round it (no cutting the inside of the corner).
	TestTrue(TEXT("round the corner, but hugging it"), FMath::IsNearlyEqual(Look.X, 1000.f, 1.f) && FMath::IsNearlyEqual(Look.Y, 100.f, 1.f));
	Look = VehicleRules::GetLookaheadPoint(Path, FVector(1010.f, 300.f, 0.f), Segment, 400.f, Remaining);
	TestEqual(TEXT("segment moved on"), Segment, 1);
	TestEqual(TEXT("on the second leg"), static_cast<float>(Look.Y), 700.f, 1.f);
	Look = VehicleRules::GetLookaheadPoint(Path, FVector(1000.f, 900.f, 0.f), Segment, 400.f, Remaining);
	TestEqual(TEXT("clamped to the goal"), static_cast<float>(Look.Y), 1000.f, 1.f);
	TestEqual(TEXT("1 m left"), Remaining, 100.f, 1.f);
	return true;
}

VEHICLE_TEST(FVehicleHullAndVisualsTest, "Hull.CoverProbeAndVisuals")
bool FVehicleHullAndVisualsTest::RunTest(const FString&)
{
	const FVector2D Half(389.f, 153.f);
	FVector2D Point;
	FVector2D Normal;
	// Looking from the right side (+Y) towards -Y, a roof click: the right side faces the player.
	TestTrue(TEXT("probe found"), VehicleRules::GetHullSideFacing(FVector2D(100.f, 20.f), FVector2D(0.f, -1.f), Half, Point, Normal));
	TestEqual(TEXT("right side"), Normal, FVector2D(0.f, 1.f));
	TestEqual(TEXT("on the right surface"), static_cast<float>(Point.Y), 153.f, 0.01f);
	TestEqual(TEXT("at the clicked length"), static_cast<float>(Point.X), 100.f, 0.01f);
	// Looking from behind (+X view): the rear.
	VehicleRules::GetHullSideFacing(FVector2D(300.f, 0.f), FVector2D(1.f, 0.f), Half, Point, Normal);
	TestEqual(TEXT("rear"), Normal, FVector2D(-1.f, 0.f));
	TestEqual(TEXT("rear surface"), static_cast<float>(Point.X), -389.f, 0.01f);
	// Diagonal view onto a long side.
	VehicleRules::GetHullSideFacing(FVector2D(0.f, 0.f), FVector2D(0.5f, -0.866f), Half, Point, Normal);
	TestEqual(TEXT("diagonal: long side"), Normal, FVector2D(0.f, 1.f));
	TestFalse(TEXT("zero view"), VehicleRules::GetHullSideFacing(FVector2D::ZeroVector, FVector2D::ZeroVector, Half, Point, Normal));
	// Visuals: wheel spin deg/s = v / r, steering from the yaw rate (bicycle model, clamped).
	TestEqual(TEXT("spin"), VehicleRules::GetWheelSpinDegPerSec(600.f, 60.f), FMath::RadiansToDegrees(10.f), 0.01f);
	TestEqual(TEXT("reverse spins back"), VehicleRules::GetWheelSpinDegPerSec(-250.f, 60.f) < 0.f, true);
	const FVehicleDriveConfig Config;
	TestEqual(TEXT("straight"), VehicleRules::GetSteerAngleDeg(0.f, 600.f, Config), 0.f, 0.01f);
	TestEqual(TEXT("clamped lock"), VehicleRules::GetSteerAngleDeg(200.f, 300.f, Config), Config.MaxSteerAngleDeg, 0.01f);
	TestEqual(TEXT("stopped"), VehicleRules::GetSteerAngleDeg(30.f, 0.f, Config), 0.f, 0.01f);
	return true;
}

VEHICLE_TEST(FVehicleEngineMountedTest, "Engine.NoiseAndMountedEffects")
bool FVehicleEngineMountedTest::RunTest(const FString&)
{
	const FVehicleEngineConfig Config;
	TestEqual(TEXT("off"), VehicleRules::GetEngineState(false, 500.f, false), EVehicleEngineState::Off);
	TestEqual(TEXT("idle"), VehicleRules::GetEngineState(true, 0.f, true), EVehicleEngineState::Idle);
	TestEqual(TEXT("cruise"), VehicleRules::GetEngineState(true, 600.f, false), EVehicleEngineState::Cruise);
	TestEqual(TEXT("rush"), VehicleRules::GetEngineState(true, 900.f, true), EVehicleEngineState::Rush);
	TestEqual(TEXT("idle 20 m"), VehicleRules::GetNoiseRadiusCm(EVehicleEngineState::Idle, Config), 2000.f);
	TestEqual(TEXT("cruise 40 m"), VehicleRules::GetNoiseRadiusCm(EVehicleEngineState::Cruise, Config), 4000.f);
	TestEqual(TEXT("rush 55 m"), VehicleRules::GetNoiseRadiusCm(EVehicleEngineState::Rush, Config), 5500.f);
	TestEqual(TEXT("silent when off"), VehicleRules::GetNoiseRadiusCm(EVehicleEngineState::Off, Config), 0.f);
	// Mounted: hidden, no collision, no target, no damage, no auto fire / formation / move orders; warm while the engine runs.
	const FMountedOperativeEffects Running = VehicleRules::GetMountedEffects(true);
	TestTrue(TEXT("hidden"), Running.bHidden);
	TestFalse(TEXT("no collision"), Running.bCollision);
	TestFalse(TEXT("not targetable"), Running.bTargetableByEnemies);
	TestFalse(TEXT("no damage"), Running.bTakesDamage);
	TestFalse(TEXT("no auto fire"), Running.bAutoFire);
	TestFalse(TEXT("no formation"), Running.bFollowsFormation);
	TestFalse(TEXT("no move orders"), Running.bAcceptsMoveOrders);
	TestFalse(TEXT("no cold drain with the engine running"), Running.bColdDrain);
	TestTrue(TEXT("cold cabin with the engine off"), VehicleRules::GetMountedEffects(false).bColdDrain);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
