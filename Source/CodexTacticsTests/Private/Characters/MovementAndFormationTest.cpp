#include "Misc/AutomationTest.h"
#include "Characters/OperativeMovementRules.h"
#include "Characters/SquadFormation.h"
#include "Characters/SquadSubsystem.h"
#include "Core/CodexTacticsGameMode.h"

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

// --- Solo Mode & Role Colors ---

MOVEMENT_TEST(FSoloModeLeashThresholdTest, "Squad.SoloMode.LeashThreshold")
bool FSoloModeLeashThresholdTest::RunTest(const FString&)
{
	TestEqual(TEXT("SoloModeMaxDistance is 25m (2500cm) matching Godot SOLO_MODE_MAX_DISTANCE"),
		USquadSubsystem::SoloModeMaxDistance, 2500.f);
	return true;
}

MOVEMENT_TEST(FOperativeRoleColorsTest, "Characters.OperativeRoleColors")
bool FOperativeRoleColorsTest::RunTest(const FString&)
{
	// The spawned roster carries the role colours: Commander = Godot COLOR_LEADER (#2080EC),
	// Engineer orange, Medic-sapper green (Godot role silhouette hues).
	const ACodexTacticsGameMode* GameMode = GetDefault<ACodexTacticsGameMode>();
	if (!TestEqual(TEXT("Roster size"), GameMode->SquadRoster.Num(), 3))
	{
		return false;
	}
	TestEqual(TEXT("Commander blue"), GameMode->SquadRoster[0].Color.ToFColorSRGB(), FColor(0x20, 0x80, 0xEC));
	TestEqual(TEXT("Engineer orange"), GameMode->SquadRoster[1].Color.ToFColorSRGB(), FColor(0xFF, 0x61, 0x0F));
	TestEqual(TEXT("Medic green"), GameMode->SquadRoster[2].Color.ToFColorSRGB(), FColor(0x1F, 0xB3, 0x33));
	return true;
}

MOVEMENT_TEST(FStanceMoveHoldRuleTest, "Squad.StanceMoveHold")
bool FStanceMoveHoldRuleTest::RunTest(const FString&)
{
	using namespace OperativeMovementRules;
	// Bug 2026-10-11: a follower rising from prone slid to the leader. The hold is the longer of the armed settle time and the
	// stance clip already playing; a move waits while it is above the threshold, and runs once the clip tail is reached.
	TestEqual(TEXT("no stance change: no hold"), ComputeMoveHold(0.f, 0.f), 0.f);
	TestEqual(TEXT("negative settle time is no hold"), ComputeMoveHold(-0.4f, 0.f), 0.f);
	TestEqual(TEXT("settle time before the clip starts"), ComputeMoveHold(1.6f, 0.f), 1.6f);
	TestEqual(TEXT("clip already playing"), ComputeMoveHold(0.f, 0.9f), 0.9f);
	TestEqual(TEXT("the longer of the two"), ComputeMoveHold(0.7f, 0.9f), 0.9f);
	TestTrue(TEXT("prone -> stand clip defers a follow move"), ShouldDeferMove(ComputeMoveHold(1.6f, 0.f)));
	TestTrue(TEXT("a clip with 0.2 s left still defers"), ShouldDeferMove(0.2f));
	TestFalse(TEXT("no hold: the move runs"), ShouldDeferMove(0.f));
	TestFalse(TEXT("the clip tail does not hold"), ShouldDeferMove(MoveHoldThreshold));
	return true;
}

MOVEMENT_TEST(FMovementProneDiveTest, "Movement.ProneDiveThresholds")
bool FMovementProneDiveTest::RunTest(const FString& Parameters)
{
	// Rev 5e item 6 (user request 2026-10-14; was "cannot go prone while moving"): walking >= 30 cm/s dives with the walk clip,
	// sprinting >= 75 cm/s with the run clip, slower for the gait (or crouched) stops first; still = in place; cover / carrying
	// (blocked) while moving = refused as before.
	using namespace OperativeMovementRules;
	const FOperativeMovementConfig Config;
	TestEqual(TEXT("walk threshold 30 cm/s (data: MovementConfig)"), Config.ProneDiveMinWalkSpeed, 30.f);
	TestEqual(TEXT("run threshold 75 cm/s"), Config.ProneDiveMinRunSpeed, 75.f);
	const EOperativeStance Up = EOperativeStance::Standing;
	TestTrue(TEXT("still: in place"), DecideProneEntry(Config, Up, 0.f, false, false, false) == EProneEntry::InPlace);
	TestTrue(TEXT("still and blocked: in place (cover)"), DecideProneEntry(Config, Up, 0.f, false, false, true) == EProneEntry::InPlace);
	TestTrue(TEXT("walking 220 cm/s: walk dive"), DecideProneEntry(Config, Up, 220.f, false, true, false) == EProneEntry::DiveFromWalk);
	TestTrue(TEXT("walking exactly 30: walk dive"), DecideProneEntry(Config, Up, 30.f, false, true, false) == EProneEntry::DiveFromWalk);
	TestTrue(TEXT("walking 25: stop, then prone"), DecideProneEntry(Config, Up, 25.f, false, true, false) == EProneEntry::StopThenProne);
	TestTrue(TEXT("sprinting 725: run dive"), DecideProneEntry(Config, Up, 725.f, true, true, false) == EProneEntry::DiveFromRun);
	TestTrue(TEXT("sprinting 75: run dive"), DecideProneEntry(Config, Up, 75.f, true, true, false) == EProneEntry::DiveFromRun);
	TestTrue(TEXT("sprinting but only 60 (accelerating): stop, then prone"), DecideProneEntry(Config, Up, 60.f, true, true, false) == EProneEntry::StopThenProne);
	TestTrue(TEXT("a move order just issued (speed 0): stop, then prone"), DecideProneEntry(Config, Up, 0.f, false, true, false) == EProneEntry::StopThenProne);
	TestTrue(TEXT("crouched walking 125: stop, then prone (the dives start upright)"),
		DecideProneEntry(Config, EOperativeStance::Crouching, 125.f, false, true, false) == EProneEntry::StopThenProne);
	TestTrue(TEXT("moving in cover / carrying: refused as before"), DecideProneEntry(Config, Up, 220.f, false, true, true) == EProneEntry::Refused);
	FOperativeMovementConfig Tuned = Config;
	Tuned.ProneDiveMinWalkSpeed = 250.f;
	TestTrue(TEXT("the threshold is data: 250 -> walking 220 stops first"), DecideProneEntry(Tuned, Up, 220.f, false, true, false) == EProneEntry::StopThenProne);
	return true;
}

MOVEMENT_TEST(FMovementProneDiveScaleTest, "Movement.ProneDiveMomentum")
bool FMovementProneDiveScaleTest::RunTest(const FString& Parameters)
{
	// Rev 5f (game designer 2026-10-14: "he stops dead, dives in place, then snaps back"): the dive carries him by root motion, stretched
	// by his momentum (entry speed / the clip's entry speed, at most MaxMomentumScale) and shortened by what lies ahead.
	using namespace OperativeMovementRules;
	const FOperativeMovementConfig Config;
	TestEqual(TEXT("momentum cap 1.4 (data: MovementConfig)"), Config.ProneDiveMaxMomentumScale, 1.4f);
	TestEqual(TEXT("sprint 725 into the run clip (entering at 355 cm/s), open ground: capped momentum"),
		ComputeProneDiveTranslationScale(725.f, 355.f, 1.4f, 344.f, 10000.f), 1.4f);
	TestEqual(TEXT("a walk at the clip's own pace: the clip's travel"), ComputeProneDiveTranslationScale(190.f, 195.f, 1.4f, 311.f, 10000.f), 1.f);
	TestTrue(TEXT("a slightly faster walk: a little farther"), FMath::IsNearlyEqual(ComputeProneDiveTranslationScale(234.f, 195.f, 1.4f, 311.f, 10000.f), 1.2f, 0.001f));
	TestTrue(TEXT("a wall / the navmesh edge 2 m ahead: shortened to 2 m"),
		FMath::IsNearlyEqual(ComputeProneDiveTranslationScale(725.f, 355.f, 1.4f, 344.f, 200.f) * 344.f, 200.f, 0.01f));
	TestEqual(TEXT("blocked right in front: no slide"), ComputeProneDiveTranslationScale(725.f, 355.f, 1.4f, 344.f, -20.f), 0.f);
	TestEqual(TEXT("no clip travel: the momentum only"), ComputeProneDiveTranslationScale(725.f, 355.f, 1.4f, 0.f, 100.f), 1.4f);
	// After a dive on a move order: crawl on only while the goal is still ahead.
	const FVector Start(0.f, 0.f, 0.f);
	const FVector End(480.f, 0.f, 0.f);
	TestTrue(TEXT("goal 10 m ahead: crawls on"), ShouldCrawlOnAfterDive(Start, End, FVector(1480.f, 0.f, 0.f), 50.f));
	TestTrue(TEXT("goal ahead to the side (the path turns): crawls on"), ShouldCrawlOnAfterDive(Start, End, FVector(600.f, 400.f, 0.f), 50.f));
	TestFalse(TEXT("goal reached by the dive: stays"), ShouldCrawlOnAfterDive(Start, End, FVector(500.f, 10.f, 0.f), 50.f));
	TestFalse(TEXT("goal behind (the dive carried him past it): stays"), ShouldCrawlOnAfterDive(Start, End, FVector(300.f, 0.f, 0.f), 50.f));
	return true;
}

#undef MOVEMENT_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
