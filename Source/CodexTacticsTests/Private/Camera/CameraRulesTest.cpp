#include "Misc/AutomationTest.h"
#include "Camera/CameraZoneVolume.h"
#include "Camera/TacticalCameraRules.h"
#include "GameFlow/GameFlowStateMachine.h"

#if WITH_DEV_AUTOMATION_TESTS

// Expected values mirror Godot camera.gd / camera_zone_trigger.gd (converted to cm and degrees).

#define CAMERA_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

CAMERA_TEST(FCameraIsometricOffsetTest, "Camera.IsometricViewOffset")
bool FCameraIsometricOffsetTest::RunTest(const FString&)
{
	const FTacticalCameraConfig Config;
	// Godot scene camera looks along (-1,-1,-1)/sqrt(3): equal parts behind, to the side and above.
	const FVector Offset = TacticalCameraRules::ComputeViewOffset(Config.BaseYaw, Config.Pitch, 1000.f);
	TestEqual(TEXT("Length"), Offset.Size(), 1000.0, 0.5);
	TestEqual(TEXT("|X| == |Y|"), FMath::Abs(Offset.X), FMath::Abs(Offset.Y), 1.0);
	TestEqual(TEXT("|X| == Z"), FMath::Abs(Offset.X), Offset.Z, 2.0);
	TestTrue(TEXT("Camera is above the focus"), Offset.Z > 0.f);
	return true;
}

CAMERA_TEST(FCameraZoomClampTest, "Camera.ZoomStepsAndClamps")
bool FCameraZoomClampTest::RunTest(const FString&)
{
	const FTacticalCameraConfig Config;
	TestEqual(TEXT("One notch out"), TacticalCameraRules::StepZoom(Config, 1600.f, 1.f), 1913.f);
	TestEqual(TEXT("One notch in"), TacticalCameraRules::StepZoom(Config, 1600.f, -1.f), 1287.f);
	TestEqual(TEXT("Clamped at min"), TacticalCameraRules::StepZoom(Config, 900.f, -3.f), Config.DistanceMin);
	TestEqual(TEXT("Clamped at max"), TacticalCameraRules::StepZoom(Config, 3900.f, 2.f), Config.DistanceMax);
	return true;
}

CAMERA_TEST(FCameraEdgeScrollTest, "Camera.EdgeScroll")
bool FCameraEdgeScrollTest::RunTest(const FString&)
{
	const FVector2D Viewport(1920.f, 1080.f);
	using namespace TacticalCameraRules;
	TestEqual(TEXT("Centre: none"), ComputeEdgeScroll(FVector2D(960.f, 540.f), Viewport, 30.f), FVector2D::ZeroVector);
	TestEqual(TEXT("Left edge"), ComputeEdgeScroll(FVector2D(10.f, 540.f), Viewport, 30.f), FVector2D(-1.f, 0.f));
	TestEqual(TEXT("Top-right corner"), ComputeEdgeScroll(FVector2D(1915.f, 5.f), Viewport, 30.f), FVector2D(1.f, 1.f));
	TestEqual(TEXT("Bottom edge"), ComputeEdgeScroll(FVector2D(960.f, 1070.f), Viewport, 30.f), FVector2D(0.f, -1.f));
	TestEqual(TEXT("Outside viewport: none"), ComputeEdgeScroll(FVector2D(-5.f, 540.f), Viewport, 30.f), FVector2D::ZeroVector);
	return true;
}

CAMERA_TEST(FCameraPanDirectionTest, "Camera.PanFollowsViewYaw")
bool FCameraPanDirectionTest::RunTest(const FString&)
{
	using namespace TacticalCameraRules;
	TestEqual(TEXT("W at yaw 0 is +X"), ComputePanDirection(FVector2D(0.f, 1.f), 0.f), FVector(1.f, 0.f, 0.f), 0.001f);
	TestEqual(TEXT("D at yaw 0 is +Y"), ComputePanDirection(FVector2D(1.f, 0.f), 0.f), FVector(0.f, 1.f, 0.f), 0.001f);
	TestEqual(TEXT("W at yaw 90 is +Y"), ComputePanDirection(FVector2D(0.f, 1.f), 90.f), FVector(0.f, 1.f, 0.f), 0.001f);
	TestEqual(TEXT("Diagonal is normalised"), ComputePanDirection(FVector2D(1.f, 1.f), 0.f).Size(), 1.0, 0.001);
	TestEqual(TEXT("Pan clamp"), ClampPan(FVector(5000.f, 0.f, 0.f), 3500.f), FVector(3500.f, 0.f, 0.f));
	return true;
}

CAMERA_TEST(FCameraEaseAndDeadzoneTest, "Camera.PanReturnEaseAndTurnBasedDeadzone")
bool FCameraEaseAndDeadzoneTest::RunTest(const FString&)
{
	using namespace TacticalCameraRules;
	TestEqual(TEXT("Ease start"), CubicEaseOut(0.f), 0.f);
	TestEqual(TEXT("Ease half"), CubicEaseOut(0.5f), 0.875f);
	TestEqual(TEXT("Ease end clamps"), CubicEaseOut(2.f), 1.f);

	const FVector Focus(0.f, 0.f, 0.f);
	TestEqual(TEXT("Inside deadzone: focus stays"), FollowWithDeadzone(Focus, FVector(200.f, 0.f, 50.f), 300.f, 2.5f, 0.1f), FVector(0.f, 0.f, 50.f));
	TestEqual(TEXT("Outside deadzone: moves 25%"), FollowWithDeadzone(Focus, FVector(1000.f, 0.f, 0.f), 300.f, 2.5f, 0.1f), FVector(250.f, 0.f, 0.f), 0.01f);
	return true;
}

CAMERA_TEST(FCameraZoneRulesTest, "Camera.ZoneActivationRules")
bool FCameraZoneRulesTest::RunTest(const FString&)
{
	using namespace CameraZoneRules;
	TestTrue(TEXT("Leader inside in exploration"), ShouldBeActive(true, false, ECodexCombatMode::None, false, true));
	TestFalse(TEXT("Leader outside"), ShouldBeActive(true, false, ECodexCombatMode::None, false, false));
	TestFalse(TEXT("Switch disabled"), ShouldBeActive(false, false, ECodexCombatMode::None, false, true));
	TestFalse(TEXT("Tactical pause"), ShouldBeActive(true, true, ECodexCombatMode::TacticalPause, true, true));
	TestFalse(TEXT("Active wave"), ShouldBeActive(true, false, ECodexCombatMode::RealTime, true, true));
	TestTrue(TEXT("Active wave, zone allows combat"), ShouldBeActive(true, true, ECodexCombatMode::RealTime, true, true));
	TestEqual(TEXT("Bunker cold x0"), GetColdMultiplier(ECameraZoneEnvironment::Closed), 0.f);
	TestEqual(TEXT("Shelter cold x0.5"), GetColdMultiplier(ECameraZoneEnvironment::Shelter), 0.5f);
	TestEqual(TEXT("Open cold x1"), GetColdMultiplier(ECameraZoneEnvironment::Standard), 1.f);
	TestEqual(TEXT("Blizzard cold x2.5"), GetColdMultiplier(ECameraZoneEnvironment::Blizzard), 2.5f);
	return true;
}

CAMERA_TEST(FGameFlowWaveActiveTest, "GameFlow.Phases.WaveActiveMatchesGodot")
bool FGameFlowWaveActiveTest::RunTest(const FString&)
{
	FGameFlowStateMachine Machine;
	TestFalse(TEXT("Exploration"), Machine.IsWaveActive());
	Machine.TriggerCombatZone();
	Machine.FinishCutscene();
	TestFalse(TEXT("First preparation"), Machine.IsWaveActive());
	Machine.FinishPreparation();
	TestTrue(TEXT("Wave"), Machine.IsWaveActive());
	Machine.ToggleTacticalPause();
	TestTrue(TEXT("Wave during pause"), Machine.IsWaveActive());
	Machine.NotifyWaveCleared();
	TestFalse(TEXT("Wave cleared"), Machine.IsWaveActive());
	Machine.AdvanceAfterWave();
	TestTrue(TEXT("Rest before wave 2"), Machine.IsWaveActive());
	return true;
}

#undef CAMERA_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
