#include "Misc/AutomationTest.h"
#include "Tactics/TorsoResetRules.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTorsoResetRulesTest, "CodexTactics.Tactics.Torso.Reset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTorsoResetRulesTest::RunTest(const FString&)
{
	using namespace TorsoResetRules;
	const FRelease Cleared = AfterExit(false);
	TestEqual(TEXT("exit: mesh yaw is the move"), Cleared.MeshYawDeg, 0.f);
	TestEqual(TEXT("exit: barrel yaw is 0"), Cleared.BarrelYawDeg, 0.f);
	TestFalse(TEXT("exit: the controller does not hold the rotation"), Cleared.bControllerDesiredRotation);
	TestTrue(TEXT("exit with no formation look: aim pitch clears"), Cleared.bClearAimPitch);

	const FRelease Kept = AfterExit(true);
	TestFalse(TEXT("a formation look keeps the scan pitch"), Kept.bClearAimPitch);
	TestEqual(TEXT("the mesh still returns"), Kept.MeshYawDeg, 0.f);

	TestTrue(TEXT("a snap is settled at once"), Settled(0.f, 12.f, 0.5f, 10.f));
	TestTrue(TEXT("aim interp 12 brings a half-turn inside 10 deg in 0.5 s"), Settled(180.f, 12.f, 0.5f, 10.f));
	TestTrue(TEXT("barrel ease 6 brings 45 deg inside 10 deg in 0.5 s"), Settled(45.f, 6.f, 0.5f, 10.f));
	TestFalse(TEXT("a slow ease of a half-turn is still past 10 deg at 0.5 s"), Settled(180.f, 2.f, 0.5f, 10.f));
	return true;
}

#endif
