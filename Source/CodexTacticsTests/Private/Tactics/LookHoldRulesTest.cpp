// CodexTactics.Tactics.LookHold.* — Look hold outside stealth (059, solo walk / jog / crouch / prone strafe).
// Docs: docs/design/LOOK_HOLD_SOLO.md.

#include "Misc/AutomationTest.h"
#include "Tactics/LookHoldRules.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLookHoldSectorsTest, "CodexTactics.Tactics.LookHold.Sectors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLookHoldSectorsTest::RunTest(const FString&)
{
	using namespace LookHoldRules;

	// 8 sectors: 0=F, 1=FR, 2=R, 3=BR, 4=B, 5=BL, 6=L, 7=FL
	TestEqual(TEXT("0 deg = F (0)"), GetDirectionSector(0.f), 0);
	TestEqual(TEXT("20 deg = F (0)"), GetDirectionSector(20.f), 0);
	TestEqual(TEXT("-20 deg = F (0)"), GetDirectionSector(-20.f), 0);

	TestEqual(TEXT("45 deg = FR (1)"), GetDirectionSector(45.f), 1);
	TestEqual(TEXT("30 deg = FR (1)"), GetDirectionSector(30.f), 1);
	TestEqual(TEXT("60 deg = FR (1)"), GetDirectionSector(60.f), 1);

	TestEqual(TEXT("90 deg = R (2)"), GetDirectionSector(90.f), 2);
	TestEqual(TEXT("135 deg = BR (3)"), GetDirectionSector(135.f), 3);

	TestEqual(TEXT("180 deg = B (4)"), GetDirectionSector(180.f), 4);
	TestEqual(TEXT("-180 deg = B (4)"), GetDirectionSector(-180.f), 4);
	TestEqual(TEXT("170 deg = B (4)"), GetDirectionSector(170.f), 4);
	TestEqual(TEXT("-170 deg = B (4)"), GetDirectionSector(-170.f), 4);

	TestEqual(TEXT("-135 deg = BL (5)"), GetDirectionSector(-135.f), 5);
	TestEqual(TEXT("-90 deg = L (6)"), GetDirectionSector(-90.f), 6);
	TestEqual(TEXT("-45 deg = FL (7)"), GetDirectionSector(-45.f), 7);
	TestEqual(TEXT("-40 deg = FL (7)"), GetDirectionSector(-40.f), 7);

	// Angle wrapping
	TestEqual(TEXT("360 deg wraps to F (0)"), GetDirectionSector(360.f), 0);
	TestEqual(TEXT("405 deg wraps to FR (1)"), GetDirectionSector(405.f), 1);
	TestEqual(TEXT("-360 deg wraps to F (0)"), GetDirectionSector(-360.f), 0);
	TestEqual(TEXT("-405 deg wraps to FL (7)"), GetDirectionSector(-405.f), 7);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLookHoldGaitStartTest, "CodexTactics.Tactics.LookHold.GaitStart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLookHoldGaitStartTest::RunTest(const FString&)
{
	using namespace LookHoldRules;

	// Without look hold: always starts forward (0) because body turns to travel
	TestEqual(TEXT("No look: sector 0 starts F"), GetGaitStartSector(0, false), 0);
	TestEqual(TEXT("No look: sector 1 starts F"), GetGaitStartSector(1, false), 0);
	TestEqual(TEXT("No look: sector 2 starts F"), GetGaitStartSector(2, false), 0);
	TestEqual(TEXT("No look: sector 4 starts F"), GetGaitStartSector(4, false), 0);

	// With look hold: body faces look point, strafe starts are preserved
	TestEqual(TEXT("Look hold: sector 0 starts F"), GetGaitStartSector(0, true), 0);
	TestEqual(TEXT("Look hold: sector 1 starts FR"), GetGaitStartSector(1, true), 1);
	TestEqual(TEXT("Look hold: sector 2 starts R"), GetGaitStartSector(2, true), 2);
	TestEqual(TEXT("Look hold: sector 4 starts B"), GetGaitStartSector(4, true), 4);
	TestEqual(TEXT("Look hold: sector 6 starts L"), GetGaitStartSector(6, true), 6);
	TestEqual(TEXT("Look hold: sector -1 wraps to 7"), GetGaitStartSector(-1, true), 7);
	TestEqual(TEXT("Look hold: sector 8 wraps to 0"), GetGaitStartSector(8, true), 0);

	// Off-sector check
	TestFalse(TEXT("0 deg is on sector 0"), IsOffGaitStartSector(0, 0.f, 30.f));
	TestFalse(TEXT("20 deg is on sector 0"), IsOffGaitStartSector(0, 20.f, 30.f));
	TestTrue(TEXT("35 deg is off sector 0"), IsOffGaitStartSector(0, 35.f, 30.f));
	TestFalse(TEXT("90 deg is on sector 2"), IsOffGaitStartSector(2, 90.f, 30.f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLookHoldValidityTest, "CodexTactics.Tactics.LookHold.Validity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLookHoldValidityTest::RunTest(const FString&)
{
	using namespace LookHoldRules;

	const FVector Origin(100.f, 200.f, 50.f);

	// Deadzone is 50 cm
	TestFalse(TEXT("Point exactly at origin is invalid"), IsValidLookPoint(Origin, Origin, 50.f));
	TestFalse(TEXT("Point 30 cm away is invalid"), IsValidLookPoint(Origin + FVector(30.f, 0.f, 0.f), Origin, 50.f));
	TestFalse(TEXT("Point 50 cm away is invalid"), IsValidLookPoint(Origin + FVector(50.f, 0.f, 0.f), Origin, 50.f));
	TestTrue(TEXT("Point 51 cm away is valid"), IsValidLookPoint(Origin + FVector(51.f, 0.f, 0.f), Origin, 50.f));
	TestTrue(TEXT("Point 200 cm away is valid"), IsValidLookPoint(Origin + FVector(0.f, 200.f, 0.f), Origin, 50.f));

	// Distance check is 2D: high Z does not fool the 2D proximity deadzone
	TestFalse(TEXT("Point 30 cm in XY but 500 cm in Z is still in deadzone"),
		IsValidLookPoint(Origin + FVector(30.f, 0.f, 500.f), Origin, 50.f));

	// Default distance from config
	TestFalse(TEXT("Default config threshold: 30 cm invalid"), IsValidLookPoint(Origin + FVector(30.f, 0.f, 0.f), Origin));
	TestTrue(TEXT("Default config threshold: 60 cm valid"), IsValidLookPoint(Origin + FVector(60.f, 0.f, 0.f), Origin));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLookHoldConfigTest, "CodexTactics.Tactics.LookHold.Config",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLookHoldConfigTest::RunTest(const FString&)
{
	using namespace LookHoldRules;

	const FLookHoldConfig& ActiveConfig = GetConfig();
	TestEqual(TEXT("Config min distance defaults to 50"), ActiveConfig.MinPlacementDistanceCm, 50.f);
	TestEqual(TEXT("Config standing twist defaults to 45"), ActiveConfig.StandingMaxTwistDeg, 45.f);
	TestEqual(TEXT("Config prone twist defaults to 0"), ActiveConfig.ProneMaxTwistDeg, 0.f);

	FLookHoldConfig Parsed;
	const FString Json = TEXT("{\"min_placement_distance_cm\": 75.0, \"standing_max_twist_deg\": 30.0, \"prone_max_twist_deg\": 10.0}");
	TestTrue(TEXT("Load valid JSON config"), LoadConfigFromJson(Json, Parsed));
	TestEqual(TEXT("Parsed min distance"), Parsed.MinPlacementDistanceCm, 75.f);
	TestEqual(TEXT("Parsed standing twist"), Parsed.StandingMaxTwistDeg, 30.f);
	TestEqual(TEXT("Parsed prone twist"), Parsed.ProneMaxTwistDeg, 10.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLookHoldClearingMatrixTest, "CodexTactics.Tactics.LookHold.ClearingMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLookHoldClearingMatrixTest::RunTest(const FString&)
{
	using namespace LookHoldRules;

	// Sprint
	TestTrue(TEXT("Sprint clears look hold"), ShouldClearOnSprint(true));
	TestFalse(TEXT("Walking/jogging does not clear look hold"), ShouldClearOnSprint(false));

	// Cover
	TestTrue(TEXT("Cover clears look hold"), ShouldClearOnCover(true));
	TestFalse(TEXT("Open ground does not clear look hold"), ShouldClearOnCover(false));

	// Turn-based combat
	TestTrue(TEXT("Turn-based start clears look hold"), ShouldClearOnTurnBased());

	// Incapacitated
	TestTrue(TEXT("Death clears look hold"), ShouldClearOnIncapacitated(true, false, false, false, false, false));
	TestTrue(TEXT("Knockdown clears look hold"), ShouldClearOnIncapacitated(false, true, false, false, false, false));
	TestFalse(TEXT("Panic does not erase look point (pauses)"), ShouldClearOnIncapacitated(false, false, true, false, false, false));
	TestFalse(TEXT("Ladder does not erase look point (pauses)"), ShouldClearOnIncapacitated(false, false, false, true, false, false));
	TestFalse(TEXT("Vault does not erase look point (pauses)"), ShouldClearOnIncapacitated(false, false, false, false, true, false));
	TestFalse(TEXT("Carry does not erase look point (pauses)"), ShouldClearOnIncapacitated(false, false, false, false, false, true));
	TestFalse(TEXT("Healthy mobile operative keeps look hold"), ShouldClearOnIncapacitated(false, false, false, false, false, false));

	// Fire order does NOT clear look hold (real-time combat)
	TestFalse(TEXT("Fire order does not clear look point"), ShouldClearOnFireOrder());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLookHoldEligibilityTest, "CodexTactics.Tactics.LookHold.Eligibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLookHoldEligibilityTest::RunTest(const FString&)
{
	using namespace LookHoldRules;

	// Stealth mode: all members eligible
	TestTrue(TEXT("Stealth mode member is eligible"), IsLookHoldEligible(true, false, false));
	TestTrue(TEXT("Stealth mode follower is eligible"), IsLookHoldEligible(true, false, false));

	// Outside stealth: solo mode
	TestTrue(TEXT("Solo mode: operative is eligible"), IsLookHoldEligible(false, true, false));
	TestTrue(TEXT("Solo mode leader is eligible"), IsLookHoldEligible(false, true, true));

	// Outside stealth: formation mode (leader only per spec)
	TestTrue(TEXT("Formation leader is eligible"), IsLookHoldEligible(false, false, true));
	TestFalse(TEXT("Formation follower is not eligible"), IsLookHoldEligible(false, false, false));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLookHoldTwistLimitsTest, "CodexTactics.Tactics.LookHold.TwistLimits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLookHoldTwistLimitsTest::RunTest(const FString&)
{
	using namespace LookHoldRules;

	// Standing twist clamped to 45 deg
	TestEqual(TEXT("Standing twist 20 clamped to 20"), ClampResidualTwist(20.f, 45.f), 20.f);
	TestEqual(TEXT("Standing twist -20 clamped to -20"), ClampResidualTwist(-20.f, 45.f), -20.f);
	TestEqual(TEXT("Standing twist 60 clamped to 45"), ClampResidualTwist(60.f, 45.f), 45.f);
	TestEqual(TEXT("Standing twist -60 clamped to -45"), ClampResidualTwist(-60.f, 45.f), -45.f);

	// Prone twist clamped to 0 deg (body turns completely)
	TestEqual(TEXT("Prone twist 30 clamped to 0"), ClampResidualTwist(30.f, 0.f), 0.f);
	TestEqual(TEXT("Prone twist -30 clamped to 0"), ClampResidualTwist(-30.f, 0.f), 0.f);

	// Default limit uses config (StandingMaxTwistDeg = 45.f)
	TestEqual(TEXT("Default twist clamp 60 -> 45"), ClampResidualTwist(60.f), 45.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLookHoldDirectionLockTest, "CodexTactics.Tactics.LookHold.DirectionLock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLookHoldDirectionLockTest::RunTest(const FString&)
{
	using namespace LookHoldRules;

	const FVector Start(0.f, 0.f, 0.f);
	const FVector ClickEast(600.f, 0.f, 100.f);
	const FLookDirection Dir = DirectionFromClick(Start, ClickEast);
	TestTrue(TEXT("Click east gives yaw ~0"), FMath::Abs(Dir.YawDeg) < 0.01f);
	TestTrue(TEXT("Click above gives positive pitch"), Dir.PitchDeg > 5.f && Dir.PitchDeg < 20.f);

	// L-shaped walk: leg 1 moves north (+Y), leg 2 moves east (+X); projected look stays east (~0 deg) on both legs
	const FVector Leg1Pos(0.f, 500.f, 0.f);
	const FVector Leg2Pos(400.f, 500.f, 0.f);
	const float Yaw1 = static_cast<float>((ProjectLookPoint(Leg1Pos, Dir.YawDeg, Dir.PitchDeg) - Leg1Pos).Rotation().Yaw);
	const float Yaw2 = static_cast<float>((ProjectLookPoint(Leg2Pos, Dir.YawDeg, Dir.PitchDeg) - Leg2Pos).Rotation().Yaw);
	TestTrue(TEXT("Leg 1 keeps locked east yaw"), FMath::Abs(FMath::FindDeltaAngleDegrees(Yaw1, Dir.YawDeg)) < 0.01f);
	TestTrue(TEXT("Leg 2 keeps locked east yaw"), FMath::Abs(FMath::FindDeltaAngleDegrees(Yaw2, Dir.YawDeg)) < 0.01f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
