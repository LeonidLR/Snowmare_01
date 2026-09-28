#include "Misc/AutomationTest.h"
#include "Characters/OperativeMovementRules.h"
#include "Characters/SquadFormation.h"

#if WITH_DEV_AUTOMATION_TESTS

// Expected values come from the Godot build (player.gd, balance.tres) converted to cm.

#define MOVEMENT_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// --- Movement rules ---

MOVEMENT_TEST(FMovementStanceSpeedsTest, "Movement.StanceSpeeds")
bool FMovementStanceSpeedsTest::RunTest(const FString&)
{
	const FOperativeMovementConfig Config;
	using namespace OperativeMovementRules;
	TestEqual(TEXT("Walk"), ComputeMaxSpeed(Config, EOperativeStance::Standing, false, false, false), 220.f);
	TestEqual(TEXT("Crouch"), ComputeMaxSpeed(Config, EOperativeStance::Crouching, false, false, false), 125.f, 0.01f);
	TestEqual(TEXT("Prone"), ComputeMaxSpeed(Config, EOperativeStance::Prone, false, false, false), 220.f * 0.28f, 0.01f);
	return true;
}

MOVEMENT_TEST(FMovementSprintSpeedTest, "Movement.SprintOnlyWhileStanding")
bool FMovementSprintSpeedTest::RunTest(const FString&)
{
	const FOperativeMovementConfig Config;
	using namespace OperativeMovementRules;
	TestEqual(TEXT("Run"), ComputeMaxSpeed(Config, EOperativeStance::Standing, true, false, false), 725.f, 0.01f);
	TestEqual(TEXT("Sprint flag ignored while crouching"), ComputeMaxSpeed(Config, EOperativeStance::Crouching, true, false, false), 125.f, 0.01f);
	TestEqual(TEXT("Sprint flag ignored while prone"), ComputeMaxSpeed(Config, EOperativeStance::Prone, true, false, false), 220.f * 0.28f, 0.01f);
	return true;
}

MOVEMENT_TEST(FMovementPenaltiesTest, "Movement.WoundAndCarryPenaltiesStack")
bool FMovementPenaltiesTest::RunTest(const FString&)
{
	const FOperativeMovementConfig Config;
	using namespace OperativeMovementRules;
	TestEqual(TEXT("Wounded"), ComputeMaxSpeed(Config, EOperativeStance::Standing, false, true, false), 220.f * 0.65f, 0.01f);
	TestEqual(TEXT("Carrying"), ComputeMaxSpeed(Config, EOperativeStance::Standing, false, false, true), 220.f * 0.30f, 0.01f);
	TestEqual(TEXT("Both"), ComputeMaxSpeed(Config, EOperativeStance::Standing, false, true, true), 220.f * 0.65f * 0.30f, 0.01f);
	return true;
}

MOVEMENT_TEST(FMovementCanSprintTest, "Movement.CanSprintRules")
bool FMovementCanSprintTest::RunTest(const FString&)
{
	const FOperativeMovementConfig Config;
	using namespace OperativeMovementRules;
	TestTrue(TEXT("Healthy and warm"), CanSprint(Config, EOperativeStance::Standing, 0.f, false));
	TestTrue(TEXT("Crouching can sprint (stands up)"), CanSprint(Config, EOperativeStance::Crouching, 0.f, false));
	TestTrue(TEXT("Cold just below threshold"), CanSprint(Config, EOperativeStance::Standing, 59.9f, false));
	TestFalse(TEXT("Cold at threshold"), CanSprint(Config, EOperativeStance::Standing, 60.f, false));
	TestFalse(TEXT("Wounded"), CanSprint(Config, EOperativeStance::Standing, 0.f, true));
	TestFalse(TEXT("Prone"), CanSprint(Config, EOperativeStance::Prone, 0.f, false));
	return true;
}

MOVEMENT_TEST(FMovementTurnRatesTest, "Movement.TurnRateByStance")
bool FMovementTurnRatesTest::RunTest(const FString&)
{
	const FOperativeMovementConfig Config;
	using namespace OperativeMovementRules;
	TestEqual(TEXT("Standing ~14 rad/s"), GetTurnRate(Config, EOperativeStance::Standing), FMath::RadiansToDegrees(14.f), 1.f);
	TestEqual(TEXT("Crouching ~7.5 rad/s"), GetTurnRate(Config, EOperativeStance::Crouching), FMath::RadiansToDegrees(7.5f), 1.f);
	TestEqual(TEXT("Prone ~2.5 rad/s"), GetTurnRate(Config, EOperativeStance::Prone), FMath::RadiansToDegrees(2.5f), 1.f);
	return true;
}

// --- Formation ---

MOVEMENT_TEST(FFormationTriangleSlotsTest, "Formation.TriangleSlots")
bool FFormationTriangleSlotsTest::RunTest(const FString&)
{
	const FSquadFormationConfig Config;
	const FVector Leader(1000.f, 500.f, 0.f);
	const FVector Forward = FVector::ForwardVector; // +X
	TestEqual(TEXT("Slot 0 back-left"), SquadFormation::ComputeSlotPosition(Config, Leader, Forward, 0, false), FVector(720.f, 240.f, 0.f));
	TestEqual(TEXT("Slot 1 back-right"), SquadFormation::ComputeSlotPosition(Config, Leader, Forward, 1, false), FVector(720.f, 760.f, 0.f));
	TestEqual(TEXT("Slot 2 second row left"), SquadFormation::ComputeSlotPosition(Config, Leader, Forward, 2, false), FVector(440.f, 240.f, 0.f));
	return true;
}

MOVEMENT_TEST(FFormationRotatesWithHeadingTest, "Formation.SlotsRotateWithHeading")
bool FFormationRotatesWithHeadingTest::RunTest(const FString&)
{
	const FSquadFormationConfig Config;
	const FVector Leader = FVector::ZeroVector;
	const FVector Forward = FVector::RightVector; // facing +Y, so "left" is +X
	TestEqual(TEXT("Slot 0"), SquadFormation::ComputeSlotPosition(Config, Leader, Forward, 0, false), FVector(260.f, -280.f, 0.f), 0.01f);
	TestEqual(TEXT("Slot 1"), SquadFormation::ComputeSlotPosition(Config, Leader, Forward, 1, false), FVector(-260.f, -280.f, 0.f), 0.01f);
	return true;
}

MOVEMENT_TEST(FFormationColumnTest, "Formation.ColumnModeLinesUpBehind")
bool FFormationColumnTest::RunTest(const FString&)
{
	const FSquadFormationConfig Config;
	const FVector Leader = FVector::ZeroVector;
	TestEqual(TEXT("Slot 0"), SquadFormation::ComputeSlotPosition(Config, Leader, FVector::ForwardVector, 0, true), FVector(-330.f, 0.f, 0.f));
	TestEqual(TEXT("Slot 1"), SquadFormation::ComputeSlotPosition(Config, Leader, FVector::ForwardVector, 1, true), FVector(-480.f, 0.f, 0.f));
	return true;
}

MOVEMENT_TEST(FFormationSlotSwapTest, "Formation.SlotSwapNeedsClearGain")
bool FFormationSlotSwapTest::RunTest(const FString&)
{
	const FSquadFormationConfig Config;
	const FVector Leader = FVector::ZeroVector;
	const FVector Forward = FVector::ForwardVector;
	const FVector Left = SquadFormation::ComputeSlotPosition(Config, Leader, Forward, 0, false);
	const FVector Right = SquadFormation::ComputeSlotPosition(Config, Leader, Forward, 1, false);

	TestFalse(TEXT("Followers in their own slots"), SquadFormation::ShouldSwapSlots(Config, Leader, Forward, Left, Right));
	TestTrue(TEXT("Followers on opposite sides"), SquadFormation::ShouldSwapSlots(Config, Leader, Forward, Right, Left));
	// Both followers near the centre line: swapping gains less than the 2.25 m^2 hysteresis.
	TestFalse(TEXT("Small gain is ignored"), SquadFormation::ShouldSwapSlots(Config, Leader, Forward, FVector(-280.f, 5.f, 0.f), FVector(-280.f, -5.f, 0.f)));
	return true;
}

MOVEMENT_TEST(FFormationHeadingSmoothingTest, "Formation.HeadingTurnsGradually")
bool FFormationHeadingSmoothingTest::RunTest(const FString&)
{
	const FSquadFormationConfig Config;
	const FVector Start = FVector::ForwardVector;
	const FVector Target = FVector::RightVector;
	const FVector Step = SquadFormation::SmoothHeading(Config, Start, Target, 0.1f);
	const float Angle = FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(Start, Step)));
	TestEqual(TEXT("35% of a 90 degree turn"), Angle, 90.f * 0.35f, 0.5f);
	TestEqual(TEXT("Large delta snaps to target"), SquadFormation::SmoothHeading(Config, Start, Target, 1.f), Target, 0.001f);
	TestEqual(TEXT("Zero current heading takes target"), SquadFormation::SmoothHeading(Config, FVector::ZeroVector, Target, 0.01f), Target, 0.001f);
	return true;
}

MOVEMENT_TEST(FFormationWanderBoundedTest, "Formation.WanderStaysWithinAmplitude")
bool FFormationWanderBoundedTest::RunTest(const FString&)
{
	const FSquadFormationConfig Config;
	for (int32 Step = 0; Step < 600; ++Step)
	{
		const FVector Offset = SquadFormation::ComputeWanderOffset(Config, FVector::ForwardVector, Step % 3, Step * 0.05f);
		if (FMath::Abs(Offset.Y) > Config.WanderSideAmplitude + 0.01f || FMath::Abs(Offset.X) > Config.WanderBackAmplitude + 0.01f)
		{
			AddError(FString::Printf(TEXT("Offset %s out of bounds at step %d"), *Offset.ToString(), Step));
			return false;
		}
	}
	return true;
}

MOVEMENT_TEST(FFormationFollowerSpeedTest, "Formation.FollowerSpeedBands")
bool FFormationFollowerSpeedTest::RunTest(const FString&)
{
	const FSquadFormationConfig Config;
	const float Max = 220.f;
	using namespace SquadFormation;
	TestEqual(TEXT("Parked inside stop radius"), ComputeFollowerSpeed(Config, Max, 20.f, 0, 0.f, false), 0.f);
	TestEqual(TEXT("Slows down near slot"), ComputeFollowerSpeed(Config, Max, 125.f, 0, 0.f, false), Max * 0.95f * 0.5f, 0.01f);
	TestEqual(TEXT("Never slower than min approach"), ComputeFollowerSpeed(Config, Max, 26.f, 0, 0.f, false), Config.MinApproachSpeed, 0.01f);
	TestEqual(TEXT("Cruise"), ComputeFollowerSpeed(Config, Max, 400.f, 0, 0.f, false), Max * 0.95f, 0.01f);
	TestEqual(TEXT("Catch-up capped by regroup limit"), ComputeFollowerSpeed(Config, Max, 800.f, 0, 0.f, false), Max * 1.15f, 0.01f);
	TestEqual(TEXT("Far away runs at catch-up speed"), ComputeFollowerSpeed(Config, Max, 1500.f, 0, 0.f, false), Max * 1.25f, 0.01f);
	return true;
}

MOVEMENT_TEST(FFormationSpeedJitterTest, "Formation.SpeedJitterOnlyWhileLeaderMoves")
bool FFormationSpeedJitterTest::RunTest(const FString&)
{
	const FSquadFormationConfig Config;
	const float Max = 220.f;
	for (int32 Step = 0; Step < 200; ++Step)
	{
		const float Speed = SquadFormation::ComputeFollowerSpeed(Config, Max, 400.f, 1, Step * 0.1f, true);
		if (Speed < Max * 0.95f * (1.f - Config.SpeedJitter) - 0.01f || Speed > Max * 0.95f * (1.f + Config.SpeedJitter) + 0.01f)
		{
			AddError(FString::Printf(TEXT("Speed %f outside jitter band at step %d"), Speed, Step));
			return false;
		}
	}
	return true;
}

#undef MOVEMENT_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
