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
	TestEqual(TEXT("Root launch speed fraction is 0.75"), Config.RootLaunchSpeedFraction, 0.75f);
	TestEqual(TEXT("Launch clamp window is 1.5 s"), Config.LaunchClampWindowSeconds, 1.5f);
	TestTrue(TEXT("Launch clamp window ends before the settle timeout (a toppling body must land before it freezes)"), Config.LaunchClampWindowSeconds < Config.MaxSettleTimeoutSeconds);
	TestEqual(TEXT("Max angular speed is 10 rad/s"), Config.MaxAngularSpeedRadS, 10.0f);
	TestEqual(TEXT("Reference body mass is 80 kg"), Config.ReferenceBodyMassKg, 80.0f);
	TestEqual(TEXT("Max pelvis sink is 20 cm"), Config.MaxPelvisSinkCm, 20.0f);
	TestEqual(TEXT("Max floor lift is 100 cm"), Config.MaxFloorLiftCm, 100.0f);
	TestEqual(TEXT("Floor trace starts 100 cm above the pelvis"), Config.FloorTraceUpCm, 100.0f);
	TestEqual(TEXT("Floor trace ends 300 cm below the pelvis"), Config.FloorTraceDownCm, 300.0f);
	TestEqual(TEXT("Floor min normal Z is 0.7"), Config.FloorMinNormalZ, 0.7f);
	TestEqual(TEXT("Floor max above pelvis is 40 cm"), Config.FloorMaxAbovePelvisCm, 40.0f);
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

	// 4. Timeout fallback: forced settle even if moving slowly (50 cm/s = timeout_freeze_max_speed_cm_s)
	TestTrue(TEXT("Exceeding MaxSettleTimeout forces settle"), FDeathRagdollRules::HasBodySettled(MovingLin, SpinningAng, 4.0f, Config));

	// 5. ...but never for a body still falling (the Brute's topple: 136 cm/s at 3.5 s)
	const FVector FallingLin(0.0f, 60.0f, -120.0f);
	TestFalse(TEXT("A falling body is not force-settled at the timeout"), FDeathRagdollRules::HasBodySettled(FallingLin, SpinningAng, 4.0f, Config));
	TestEqual(TEXT("Timeout freeze speed limit is 50 cm/s"), Config.TimeoutFreezeMaxSpeedCmS, 50.0f);
	TestTrue(TEXT("Timeout freeze at 3.5 s and 12 cm/s"), FDeathRagdollRules::IsRagdollTimeoutFreeze(3.5f, 12.0f, Config));
	TestFalse(TEXT("No timeout freeze at 3.5 s and 136 cm/s"), FDeathRagdollRules::IsRagdollTimeoutFreeze(3.5f, 136.0f, Config));
	TestFalse(TEXT("No timeout freeze before 3.5 s"), FDeathRagdollRules::IsRagdollTimeoutFreeze(3.4f, 0.0f, Config));

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
	const float MaxSink = 20.0f;
	const float MaxLift = 100.0f;

	// The pelvis BODY bottom 30 cm below the floor -> lifted 30 cm.
	TestEqual(TEXT("Body bottom 30 cm below floor corrected by +30 cm"), FDeathRagdollRules::CalculatePelvisFloorCorrection(70.0f, FloorZ, MaxSink, MaxLift), 30.0f);
	// Within the sink tolerance / exactly on it -> nothing.
	TestEqual(TEXT("Body bottom 15 cm below floor needs no correction"), FDeathRagdollRules::CalculatePelvisFloorCorrection(85.0f, FloorZ, MaxSink, MaxLift), 0.0f);
	TestEqual(TEXT("Body bottom exactly 20 cm below floor needs no correction"), FDeathRagdollRules::CalculatePelvisFloorCorrection(80.0f, FloorZ, MaxSink, MaxLift), 0.0f);
	// Resting on / above the floor -> nothing.
	TestEqual(TEXT("Body bottom above floor needs no correction"), FDeathRagdollRules::CalculatePelvisFloorCorrection(115.0f, FloorZ, MaxSink, MaxLift), 0.0f);
	// The main-branch Brute case: the capsule snap pushed the 99 cm thick pelvis body 134 cm under the floor -> lift capped at MaxLift.
	TestEqual(TEXT("Brute body bottom 134 cm below floor lifted by the 100 cm cap"), FDeathRagdollRules::CalculatePelvisFloorCorrection(FloorZ - 134.0f, FloorZ, MaxSink, MaxLift), 100.0f);
	TestEqual(TEXT("Zero lift cap never lifts"), FDeathRagdollRules::CalculatePelvisFloorCorrection(0.0f, FloorZ, MaxSink, 0.0f), 0.0f);
	return true;
}

RAGDOLL_TEST(FDeathRagdollLaunchVelocityChangeTest, "LaunchVelocityChange")
bool FDeathRagdollLaunchVelocityChangeTest::RunTest(const FString&)
{
	const FDeathRagdollConfig Config = FDeathRagdollRules::GetDefaultConfig();
	const float RootLimit = FDeathRagdollRules::GetMaxLaunchSpeed(EDamageType::Explosive, Config) * Config.RootLaunchSpeedFraction;
	TestNearlyEqual(TEXT("Explosive root limit is 0.75 x 900 = 675 cm/s"), RootLimit, 675.0f, 0.01f);

	// Standing body, small impulse: unchanged.
	const FVector Small(200.0f, 0.0f, 50.0f);
	TestEqual(TEXT("Small velocity change unchanged"), FDeathRagdollRules::ClampLaunchVelocityChange(FVector::ZeroVector, Small, RootLimit), Small);

	// Standing body, the 60000 impulse on an 80 kg body (750 cm/s): the result reaches exactly the limit.
	const FVector Big(750.0f, 0.0f, 0.0f);
	const FVector BigClamped = FDeathRagdollRules::ClampLaunchVelocityChange(FVector::ZeroVector, Big, RootLimit);
	TestNearlyEqual(TEXT("Big velocity change clamped to 675 cm/s"), static_cast<float>(BigClamped.Size()), RootLimit, 0.1f);

	// Killed mid-sprint (500 cm/s) and hit forward again: the final speed, not the change, is limited.
	const FVector Sprint(500.0f, 0.0f, 0.0f);
	const FVector SprintChange = FDeathRagdollRules::ClampLaunchVelocityChange(Sprint, FVector(400.0f, 0.0f, 0.0f), RootLimit);
	TestNearlyEqual(TEXT("Sprint + hit ends at 675 cm/s"), static_cast<float>((Sprint + SprintChange).Size()), RootLimit, 0.1f);

	// Already faster than the limit, no hit: slowed down to the limit.
	const FVector Fast(900.0f, 0.0f, 0.0f);
	TestNearlyEqual(TEXT("Over-limit body slowed to 675 cm/s"), static_cast<float>((Fast + FDeathRagdollRules::ClampLaunchVelocityChange(Fast, FVector::ZeroVector, RootLimit)).Size()), RootLimit, 0.1f);
	return true;
}

RAGDOLL_TEST(FDeathRagdollFloorHitTest, "FloorHit")
bool FDeathRagdollFloorHitTest::RunTest(const FString&)
{
	const FDeathRagdollConfig Config = FDeathRagdollRules::GetDefaultConfig();
	const float PelvisZ = 99.0f; // the settled Brute's pelvis bone
	TestTrue(TEXT("Flat floor under the pelvis is the floor"), FDeathRagdollRules::IsCorpseFloorHit(0.0f, 1.0f, PelvisZ, Config));
	TestTrue(TEXT("A 40 cm higher ledge is still the floor (boundary)"), FDeathRagdollRules::IsCorpseFloorHit(PelvisZ + 40.0f, 1.0f, PelvisZ, Config));
	TestFalse(TEXT("A table top 60 cm above the pelvis is an overhead obstacle"), FDeathRagdollRules::IsCorpseFloorHit(PelvisZ + 60.0f, 1.0f, PelvisZ, Config));
	TestFalse(TEXT("A wall / steep face (normal Z 0.5) is not the floor"), FDeathRagdollRules::IsCorpseFloorHit(0.0f, 0.5f, PelvisZ, Config));
	TestTrue(TEXT("A 45 degree ramp (normal Z 0.707) is the floor"), FDeathRagdollRules::IsCorpseFloorHit(0.0f, 0.707f, PelvisZ, Config));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
