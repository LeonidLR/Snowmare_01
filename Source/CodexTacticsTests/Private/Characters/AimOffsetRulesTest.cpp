// CodexTactics.Anim.AimOffset.* - the pitch fed to the ABP's Aim Offset AO1D_Rifle_Idle (user request 2026-10-07; UE-only,
// no Godot reference): the pitch from the muzzle / eye height to the target, and when the offset is on.

#include "Characters/AimOffsetRules.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAimOffsetYawToTargetTest, "CodexTactics.Anim.AimOffset.YawToTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAimOffsetYawToTargetTest::RunTest(const FString&)
{
	// 2D aim offset AO_Rifle_Aim (user request 2026-10-07): + yaw = his right (RightCenter sample at +90).
	using namespace AimOffsetRules;
	const FVector Here(0.f, 0.f, 140.f);
	const FVector Forward(1.f, 0.f, 0.f); // facing +X: his right is +Y
	TestTrue(TEXT("straight ahead -> 0"), FMath::IsNearlyZero(YawToTarget(Here, Forward, FVector(800.f, 0.f, 0.f), 180.f), 0.01f));
	TestTrue(TEXT("45 deg to his right -> +45"), FMath::IsNearlyEqual(YawToTarget(Here, Forward, FVector(500.f, 500.f, 0.f), 180.f), 45.f, 0.01f));
	TestTrue(TEXT("45 deg to his left -> -45"), FMath::IsNearlyEqual(YawToTarget(Here, Forward, FVector(500.f, -500.f, 0.f), 180.f), -45.f, 0.01f));
	TestTrue(TEXT("height does not matter"), FMath::IsNearlyEqual(YawToTarget(Here, Forward, FVector(500.f, 500.f, 900.f), 180.f), 45.f, 0.01f));
	TestTrue(TEXT("clamped to the limit"), FMath::IsNearlyEqual(YawToTarget(Here, Forward, FVector(0.f, 500.f, 0.f), 60.f), 60.f, 0.01f));
	TestTrue(TEXT("clamped on the left too"), FMath::IsNearlyEqual(YawToTarget(Here, Forward, FVector(-100.f, -500.f, 0.f), 60.f), -60.f, 0.01f));
	// Wrap-around: base pointing back-left (yaw 170), target back-right (yaw -170) is 20 deg to his right... of the base.
	const FVector BackLeft = FRotator(0.f, 170.f, 0.f).Vector();
	TestTrue(TEXT("wraps across 180"), FMath::IsNearlyEqual(YawToTarget(Here, BackLeft, Here + FRotator(0.f, -170.f, 0.f).Vector() * 500.f, 180.f), 20.f, 0.05f));
	TestTrue(TEXT("degenerate base -> 0"), FMath::IsNearlyZero(YawToTarget(Here, FVector::UpVector, FVector(500.f, 500.f, 0.f), 180.f)));
	// The residual after the twist: 50 deg error, 40 deg twisted -> 10 left; a twist the wrong way adds up.
	TestTrue(TEXT("residual after the twist"), FMath::IsNearlyEqual(ResidualAimError(50.f, 40.f), 10.f, 0.01f));
	TestTrue(TEXT("residual of the opposite twist"), FMath::IsNearlyEqual(ResidualAimError(-30.f, 20.f), 50.f, 0.01f));
	TestTrue(TEXT("no twist: the full error"), FMath::IsNearlyEqual(ResidualAimError(-25.f, 0.f), 25.f, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAimOffsetPitchToTargetTest, "CodexTactics.Anim.AimOffset.PitchToTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAimOffsetPitchToTargetTest::RunTest(const FString&)
{
	using namespace AimOffsetRules;
	const FVector Eye(0.f, 0.f, 140.f);
	// A target 2 m lower at 6 m: atan(200 / 600) = -18.43 deg.
	const float Down = PitchToTarget(Eye, FVector(600.f, 0.f, -60.f), 90.f);
	TestTrue(FString::Printf(TEXT("2 m lower at 6 m -> %.2f ~ -18.43"), Down), FMath::IsNearlyEqual(Down, -18.43f, 0.05f));
	// Higher: positive; level: 0; the horizontal direction does not matter.
	TestTrue(TEXT("2 m higher at 6 m -> +18.43"), FMath::IsNearlyEqual(PitchToTarget(Eye, FVector(0.f, 600.f, 340.f), 90.f), 18.43f, 0.05f));
	TestTrue(TEXT("level -> 0"), FMath::IsNearlyZero(PitchToTarget(Eye, FVector(500.f, 300.f, 140.f), 90.f), 0.001f));
	// Clamp to the AO range; a target at his feet does not flip (horizontal distance floored at 50 cm).
	TestTrue(TEXT("clamped to the range"), FMath::IsNearlyEqual(PitchToTarget(Eye, FVector(100.f, 0.f, -60.f), 30.f), -30.f, 0.001f));
	TestTrue(TEXT("straight down is finite and -90 at most"), PitchToTarget(Eye, FVector(0.f, 0.f, -60.f), 90.f) >= -90.f);
	TestTrue(TEXT("90-degree range is never exceeded"), FMath::Abs(PitchToTarget(Eye, FVector(1.f, 0.f, 99999.f), 90.f)) <= 90.f);
	// A hound close by (1 m, 70 cm lower): atan(70 / 100) = -35.0 deg.
	TestTrue(TEXT("close hound"), FMath::IsNearlyEqual(PitchToTarget(Eye, FVector(100.f, 0.f, 70.f), 90.f), -35.0f, 0.05f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAimOffsetWantsTest, "CodexTactics.Anim.AimOffset.AlphaRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAimOffsetWantsTest::RunTest(const FString&)
{
	using namespace AimOffsetRules;
	FAimOffsetState State;
	TestTrue(TEXT("default: on"), WantsAimOffset(State));
	auto Off = [this, &State](const TCHAR* Name, TFunctionRef<void(FAimOffsetState&)> Set)
	{
		FAimOffsetState Copy = State;
		Set(Copy);
		TestFalse(Name, WantsAimOffset(Copy));
	};
	Off(TEXT("disabled"), [](FAimOffsetState& S) { S.bEnabled = false; });
	Off(TEXT("melee"), [](FAimOffsetState& S) { S.bRangedWeapon = false; });
	Off(TEXT("hidden weapon"), [](FAimOffsetState& S) { S.bWeaponVisible = false; });
	Off(TEXT("reload"), [](FAimOffsetState& S) { S.bReloading = true; });
	Off(TEXT("grenade / hit window"), [](FAimOffsetState& S) { S.bUpperBodyAction = true; });
	Off(TEXT("vault"), [](FAimOffsetState& S) { S.bVaulting = true; });
	Off(TEXT("sprint"), [](FAimOffsetState& S) { S.bSprinting = true; });
	Off(TEXT("prone"), [](FAimOffsetState& S) { S.bProne = true; });
	Off(TEXT("dead"), [](FAimOffsetState& S) { S.bDead = true; });
	// The step: 0.2 s blend, 0.1 s -> 0.5; reaches 1; instant at 0 blend time.
	TestTrue(TEXT("half way"), FMath::IsNearlyEqual(StepAlpha(0.f, true, 0.1f, 0.2f), 0.5f, 0.001f));
	TestTrue(TEXT("never above 1"), FMath::IsNearlyEqual(StepAlpha(0.9f, true, 1.f, 0.2f), 1.f, 0.001f));
	TestTrue(TEXT("falls to 0"), FMath::IsNearlyZero(StepAlpha(0.3f, false, 1.f, 0.2f), 0.001f));
	TestTrue(TEXT("instant"), FMath::IsNearlyEqual(StepAlpha(0.f, true, 0.016f, 0.f), 1.f, 0.001f));
	return true;
}

#endif
