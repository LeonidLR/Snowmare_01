// CodexTactics.Combat.DeathRagdoll.* - Tests for Death Ragdoll pure rules and validation.
// Governed by docs/port/gemini/outbox/049-ragdoll-plan.md (Task 005 / Mechanics #16 Step 1).

#include "Misc/AutomationTest.h"
#include "Combat/DeathRagdollRules.h"
#include "Combat/DeathRagdollTypes.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

#define RAGDOLL_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Combat.DeathRagdoll." TestPath, EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

RAGDOLL_TEST(FDeathRagdollConfigLoadsTest, "ConfigLoads")
bool FDeathRagdollConfigLoadsTest::RunTest(const FString&)
{
	const FString ConfigPath = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/Combat/death_ragdoll.json"));
	FString JsonContent;
	const bool bFileRead = FFileHelper::LoadFileToString(JsonContent, *ConfigPath);
	TestTrue(TEXT("Loaded Content/Data/Combat/death_ragdoll.json"), bFileRead);

	FDeathRagdollConfig Config;
	const bool bParsed = FDeathRagdollRules::LoadConfigFromJson(JsonContent, Config);
	TestTrue(TEXT("Parsed config successfully"), bParsed);

	TestTrue(TEXT("Death ragdoll enabled"), Config.bEnabled);
	TestEqual(TEXT("Max simultaneous ragdolls is 4"), Config.MaxSimultaneousRagdolls, 4);
	TestEqual(TEXT("Max settle timeout is 3.5s"), Config.MaxSettleTimeoutSeconds, 3.5f);
	TestEqual(TEXT("Linear settle threshold is 15 cm/s"), Config.LinearSettleThresholdCmS, 15.0f);
	TestEqual(TEXT("Angular settle threshold is 0.25 rad/s"), Config.AngularSettleThresholdRadS, 0.25f);
	TestEqual(TEXT("Max impulse magnitude is 60,000"), Config.MaxImpulseMagnitude, 60000.0f);
	TestEqual(TEXT("Max launch speed is 600 cm/s"), Config.MaxLaunchSpeedCmS, 600.0f);
	TestEqual(TEXT("Max launch speed explosive is 900 cm/s"), Config.MaxLaunchSpeedExplosiveCmS, 900.0f);
	TestEqual(TEXT("Upward impulse bias is 0.25"), Config.UpwardImpulseBias, 0.25f);
	TestEqual(TEXT("Min damage factor is 0.5"), Config.MinDamageFactor, 0.5f);
	TestEqual(TEXT("Max damage factor is 3.0"), Config.MaxDamageFactor, 3.0f);

	TestTrue(TEXT("Contains Kinetic impulse scale"), Config.ImpulseScales.Contains(EDamageType::Kinetic));
	TestTrue(TEXT("Contains Explosive impulse scale"), Config.ImpulseScales.Contains(EDamageType::Explosive));
	TestTrue(TEXT("Spitter is in fallback archetypes"), Config.FallbackArchetypes.Contains(TEXT("Spitter")));
	TestTrue(TEXT("CRYO_DRONE is in fallback archetypes"), Config.FallbackArchetypes.Contains(TEXT("CRYO_DRONE")));

	return true;
}

RAGDOLL_TEST(FDeathRagdollImpulseCalculationTest, "ImpulseCalculation")
bool FDeathRagdollImpulseCalculationTest::RunTest(const FString&)
{
	const FDeathRagdollConfig Config = FDeathRagdollRules::GetDefaultConfig();

	// Kinetic rifle shot forward
	const FVector HitDir(1.0f, 0.0f, 0.0f);
	const FVector KineticImpulse = FDeathRagdollRules::CalculateImpulse(HitDir, 25.0f, EDamageType::Kinetic, Config);
	TestTrue(TEXT("Kinetic impulse has positive X"), KineticImpulse.X > 0.0f);
	TestTrue(TEXT("Kinetic impulse has upward Z bias"), KineticImpulse.Z > 0.0f);

	// Explosive blast
	const FVector ExplosiveImpulse = FDeathRagdollRules::CalculateImpulse(HitDir, 100.0f, EDamageType::Explosive, Config);
	TestTrue(TEXT("Explosive impulse stronger than kinetic"), ExplosiveImpulse.Size() > KineticImpulse.Size());

	// Clamp verification: huge damage
	const FVector MassiveImpulse = FDeathRagdollRules::CalculateImpulse(HitDir, 50000.0f, EDamageType::Explosive, Config);
	TestTrue(
		TEXT("Impulse clamped to MaxImpulseMagnitude"),
		MassiveImpulse.Size() <= Config.MaxImpulseMagnitude + 1.0f
	);

	return true;
}

RAGDOLL_TEST(FDeathRagdollSettleDetectionTest, "SettleDetection")
bool FDeathRagdollSettleDetectionTest::RunTest(const FString&)
{
	const FDeathRagdollConfig Config = FDeathRagdollRules::GetDefaultConfig();

	// 1. Moving body: not settled
	const FVector MovingLin(50.0f, 0.0f, 0.0f);
	const FVector StillAng(0.05f, 0.0f, 0.0f);
	TestFalse(TEXT("Fast linear velocity is not settled"), FDeathRagdollRules::HasBodySettled(MovingLin, StillAng, 1.0f, Config));

	// 2. Spinning body: not settled
	const FVector StillLin(5.0f, 0.0f, 0.0f);
	const FVector SpinningAng(1.5f, 0.0f, 0.0f);
	TestFalse(TEXT("High angular velocity is not settled"), FDeathRagdollRules::HasBodySettled(StillLin, SpinningAng, 1.0f, Config));

	// 3. Resting body: settled
	TestTrue(TEXT("Low linear and angular velocities are settled"), FDeathRagdollRules::HasBodySettled(StillLin, StillAng, 1.0f, Config));

	// 4. Timeout fallback: forced settle even if moving
	TestTrue(TEXT("Exceeding MaxSettleTimeout forces settle"), FDeathRagdollRules::HasBodySettled(MovingLin, SpinningAng, 4.0f, Config));

	return true;
}

RAGDOLL_TEST(FDeathRagdollBudgetCapTest, "BudgetCap")
bool FDeathRagdollBudgetCapTest::RunTest(const FString&)
{
	const FDeathRagdollConfig Config = FDeathRagdollRules::GetDefaultConfig();

	// Under budget: allowed
	TestTrue(TEXT("0 active: can simulate"), FDeathRagdollRules::CanSimulateRagdoll(0, Config));
	TestTrue(TEXT("3 active: can simulate"), FDeathRagdollRules::CanSimulateRagdoll(3, Config));

	// At or over budget (cap is 4): rejected
	TestFalse(TEXT("4 active: budget reached, cannot simulate"), FDeathRagdollRules::CanSimulateRagdoll(4, Config));
	TestFalse(TEXT("5 active: over budget, cannot simulate"), FDeathRagdollRules::CanSimulateRagdoll(5, Config));

	return true;
}

RAGDOLL_TEST(FDeathRagdollFallbackArchetypesTest, "FallbackArchetypes")
bool FDeathRagdollFallbackArchetypesTest::RunTest(const FString&)
{
	const FDeathRagdollConfig Config = FDeathRagdollRules::GetDefaultConfig();

	TestTrue(TEXT("Spitter falls back to pose"), FDeathRagdollRules::ShouldFallbackToPose(TEXT("Spitter"), Config));
	TestTrue(TEXT("CRYO_DRONE falls back to pose"), FDeathRagdollRules::ShouldFallbackToPose(TEXT("CRYO_DRONE"), Config));
	TestTrue(TEXT("CryoDrone alias falls back to pose"), FDeathRagdollRules::ShouldFallbackToPose(TEXT("CryoDrone"), Config));

	TestFalse(TEXT("FrostHound does not fallback"), FDeathRagdollRules::ShouldFallbackToPose(TEXT("FrostHound"), Config));
	TestFalse(TEXT("Brute does not fallback"), FDeathRagdollRules::ShouldFallbackToPose(TEXT("Brute"), Config));
	TestFalse(TEXT("MilitaryZombie does not fallback"), FDeathRagdollRules::ShouldFallbackToPose(TEXT("MilitaryZombie"), Config));

	return true;
}

RAGDOLL_TEST(FDeathRagdollPhysicsAssetValidationTest, "PhysicsAssetValidation")
bool FDeathRagdollPhysicsAssetValidationTest::RunTest(const FString&)
{
	FString ErrorMsg;
	const bool bNullResult = FDeathRagdollRules::ValidatePhysicsAssetSetup(nullptr, ErrorMsg);
	TestFalse(TEXT("Null mesh fails validation"), bNullResult);
	TestTrue(TEXT("Produces descriptive error"), ErrorMsg.Contains(TEXT("null")));

	TestEqual(TEXT("Default pelvis bone name"), FDeathRagdollRules::GetDefaultPelvisBoneName(), FName(TEXT("pelvis")));

	return true;
}

RAGDOLL_TEST(FDeathRagdollSpeedClampTest, "SpeedClamp")
bool FDeathRagdollSpeedClampTest::RunTest(const FString&)
{
	const FDeathRagdollConfig Config = FDeathRagdollRules::GetDefaultConfig();

	// 1. GetMaxLaunchSpeed
	const float KineticMax = FDeathRagdollRules::GetMaxLaunchSpeed(EDamageType::Kinetic, Config);
	TestEqual(TEXT("Kinetic max launch speed is 600 cm/s"), KineticMax, 600.0f);

	const float ExplosiveMax = FDeathRagdollRules::GetMaxLaunchSpeed(EDamageType::Explosive, Config);
	TestEqual(TEXT("Explosive max launch speed is 900 cm/s"), ExplosiveMax, 900.0f);

	// 2. Velocity below threshold: unchanged
	const FVector SubThresholdVel(300.0f, 0.0f, 0.0f);
	const FVector ClampedSub = FDeathRagdollRules::ClampLinearVelocity(SubThresholdVel, 600.0f);
	TestEqual(TEXT("Sub-threshold velocity unchanged"), ClampedSub, SubThresholdVel);

	// 3. Velocity above threshold: clamped to max speed in same direction
	const FVector ExcessiveVel(0.0f, 1200.0f, 0.0f);
	const FVector ClampedExcess = FDeathRagdollRules::ClampLinearVelocity(ExcessiveVel, 600.0f);
	TestNearlyEqual(TEXT("Excessive velocity clamped to 600 cm/s"), static_cast<float>(ClampedExcess.Size()), 600.0f, 0.1f);
	TestTrue(TEXT("Clamped velocity preserves direction"), ClampedExcess.Y > 0.0f);

	// 4. Zero velocity: unchanged
	const FVector ZeroVel = FVector::ZeroVector;
	TestEqual(TEXT("Zero velocity unchanged"), FDeathRagdollRules::ClampLinearVelocity(ZeroVel, 600.0f), ZeroVel);

	return true;
}

RAGDOLL_TEST(FDeathRagdollPelvisFloorCorrectionTest, "PelvisFloorCorrection")
bool FDeathRagdollPelvisFloorCorrectionTest::RunTest(const FString&)
{
	const float FloorZ = 100.0f;

	// 1. Pelvis > 20 cm below floor (e.g. at Z = 70 cm, sink = 30 cm) -> lifts by 30 cm
	const float SunkPelvisZ = 70.0f;
	const float CorrectionSunk = FDeathRagdollRules::CalculatePelvisFloorCorrection(SunkPelvisZ, FloorZ, 20.0f);
	TestEqual(TEXT("Pelvis 30 cm below floor corrected by +30 cm"), CorrectionSunk, 30.0f);

	// 2. Pelvis <= 20 cm below floor (e.g. at Z = 85 cm, sink = 15 cm) -> no correction (returns 0.0f)
	const float MinorSinkPelvisZ = 85.0f;
	const float CorrectionMinor = FDeathRagdollRules::CalculatePelvisFloorCorrection(MinorSinkPelvisZ, FloorZ, 20.0f);
	TestEqual(TEXT("Pelvis 15 cm below floor needs no correction"), CorrectionMinor, 0.0f);

	// 3. Pelvis exactly 20 cm below floor -> boundary check (returns 0.0f)
	const float BoundaryPelvisZ = 80.0f;
	const float CorrectionBoundary = FDeathRagdollRules::CalculatePelvisFloorCorrection(BoundaryPelvisZ, FloorZ, 20.0f);
	TestEqual(TEXT("Pelvis exactly 20 cm below floor needs no correction"), CorrectionBoundary, 0.0f);

	// 4. Pelvis above floor -> no correction
	const float AbovePelvisZ = 115.0f;
	const float CorrectionAbove = FDeathRagdollRules::CalculatePelvisFloorCorrection(AbovePelvisZ, FloorZ, 20.0f);
	TestEqual(TEXT("Pelvis above floor needs no correction"), CorrectionAbove, 0.0f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
