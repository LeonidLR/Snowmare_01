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
	TestTrue(TEXT("Panic clears look hold"), ShouldClearOnIncapacitated(false, false, true, false, false, false));
	TestTrue(TEXT("Ladder clears look hold"), ShouldClearOnIncapacitated(false, false, false, true, false, false));
	TestTrue(TEXT("Vault clears look hold"), ShouldClearOnIncapacitated(false, false, false, false, true, false));
	TestTrue(TEXT("Carry clears look hold"), ShouldClearOnIncapacitated(false, false, false, false, false, true));
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
	TestTrue(TEXT("Stealth mode member is eligible"), IsLookHoldEligible(true, false, false, false));
	TestTrue(TEXT("Stealth mode follower is eligible"), IsLookHoldEligible(true, false, false, false));

	// Outside stealth: solo mode
	TestTrue(TEXT("Solo mode: operative is eligible"), IsLookHoldEligible(false, true, false, false));
	TestTrue(TEXT("Solo mode leader is eligible"), IsLookHoldEligible(false, true, true, false));

	// Outside stealth: formation mode
	TestTrue(TEXT("Formation leader is eligible"), IsLookHoldEligible(false, false, true, false));
	TestFalse(TEXT("Formation follower is not eligible"), IsLookHoldEligible(false, false, false, false));
	TestTrue(TEXT("Formation follower guarding outside formation is eligible"), IsLookHoldEligible(false, false, false, true));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLookHoldTwistLimitsTest, "CodexTactics.Tactics.LookHold.TwistLimits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLookHoldTwistLimitsTest::RunTest(const FString&)
{
	using namespace LookHoldRules;

	// Standing twist clamped to StandingMaxTwistDeg (45 deg)
	TestEqual(TEXT("Standing twist 20 clamped to 20"), ClampResidualTwist(20.f, StandingMaxTwistDeg), 20.f);
	TestEqual(TEXT("Standing twist -20 clamped to -20"), ClampResidualTwist(-20.f, StandingMaxTwistDeg), -20.f);
	TestEqual(TEXT("Standing twist 60 clamped to 45"), ClampResidualTwist(60.f, StandingMaxTwistDeg), 45.f);
	TestEqual(TEXT("Standing twist -60 clamped to -45"), ClampResidualTwist(-60.f, StandingMaxTwistDeg), -45.f);

	// Prone twist clamped to ProneMaxTwistDeg (0 deg, body turns completely)
	TestEqual(TEXT("Prone twist 30 clamped to 0"), ClampResidualTwist(30.f, ProneMaxTwistDeg), 0.f);
	TestEqual(TEXT("Prone twist -30 clamped to 0"), ClampResidualTwist(-30.f, ProneMaxTwistDeg), 0.f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
