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

CAMERA_TEST(FCameraMinZoomTest, "Camera.MinZoom")
bool FCameraMinZoomTest::RunTest(const FString&)
{
	using namespace TacticalCameraRules;
	FTacticalCameraConfig Config;
	TestEqual(TEXT("Default minimum is the wheel limit of 8 m"), GetMinZoomDistance(Config), 800.f);
	Config.DistanceMin = 100.f; // a Blueprint that lowered the wheel limit (bug #52)
	TestEqual(TEXT("The hard floor wins over a low DistanceMin"), GetMinZoomDistance(Config), Config.MinSafeDistance);
	TestEqual(TEXT("A tiny glide distance is raised"), ClampMinDistance(Config, 100.f), Config.MinSafeDistance);
	TestEqual(TEXT("A far distance is not touched"), ClampMinDistance(Config, 5000.f), 5000.f);
	TestEqual(TEXT("The wheel never goes below the floor"), StepZoom(Config, 800.f, -50.f), Config.MinSafeDistance);

	// Capsule push-out: radius 35, half height 90, clearance 60 -> reach 95 cm from the axis.
	const FVector Centre(0.f, 0.f, 90.f);
	const FVector Outside(300.f, 0.f, 90.f);
	TestEqual(TEXT("Outside: unchanged"), PushOutOfCapsule(Outside, Centre, 90.f, 35.f, 60.f), Outside);
	const FVector Pushed = PushOutOfCapsule(FVector(10.f, 0.f, 120.f), Centre, 90.f, 35.f, 60.f);
	TestTrue(TEXT("Inside: pushed out sideways to the reach"), FMath::IsNearlyEqual(Pushed.X, 95.f, 0.1f) && FMath::IsNearlyEqual(Pushed.Z, 120.f, 0.1f));
	const FVector AboveHead = PushOutOfCapsule(FVector(0.f, 0.f, 175.f), Centre, 90.f, 35.f, 0.f);
	TestTrue(TEXT("Over the head, inside the cap: pushed up"), AboveHead.Z >= 90.f + 55.f + 35.f - 0.1f);
	TestTrue(TEXT("On the axis: pushed straight up"), PushOutOfCapsule(Centre, Centre, 90.f, 35.f, 0.f).Z > Centre.Z);
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

CAMERA_TEST(FCameraFreePanNoLeashTest, "Camera.FreePanNoLeash")
bool FCameraFreePanNoLeashTest::RunTest(const FString&)
{
	using namespace TacticalCameraRules;
	// Bug_04 B (game designer 2026-11-01): the free pan stopped MaxPanCombat (85 m) from the focus frozen at the selected character. The
	// only limit now: the level's area (120 x 120 m, the formation lab's) grown by 30 m.
	const FBox2D Lab(FVector2D(-6000.0, -6000.0), FVector2D(6000.0, 6000.0));
	const FVector Focus(-5500.f, -4500.f, 100.f);
	const FVector Far(10000.f, 0.f, 0.f); // 100 m along X: past the old 85 m leash, still inside the lab
	TestEqual(TEXT("100 m along X inside the level: no leash"), ClampFreePan(Focus, Far, Lab, 3000.f), Far);
	TestEqual(TEXT("... the same pan along Y"), ClampFreePan(Focus, FVector(0.f, 9000.f, 0.f), Lab, 3000.f), FVector(0.f, 9000.f, 0.f));
	TestTrue(TEXT("the old radius leash would have stopped it at 85 m"), ClampPan(Far, 8500.f).X < 8501.f);
	// Past the level: the view stops at the area's edge + the margin, by axis (the other axis keeps moving).
	const FVector Out = ClampFreePan(Focus, FVector(20000.f, 3000.f, 0.f), Lab, 3000.f);
	TestEqual(TEXT("past the east edge: held at 6000 + 3000"), Focus.X + Out.X, 9000.0, 0.01);
	TestEqual(TEXT("... while Y still moves"), Focus.Y + Out.Y, -1500.0, 0.01);
	TestEqual(TEXT("... Z kept"), Out.Z, 0.0, 0.01);
	// Not relative to the character: the same view point is allowed whoever is followed and wherever he stands.
	const FVector OtherFocus(4000.f, 4000.f, 0.f);
	const FVector ViewPoint(8500.f, -8500.f, 0.f);
	const FVector ViaA = Focus + ClampFreePan(Focus, ViewPoint - Focus, Lab, 3000.f);
	const FVector ViaB = OtherFocus + ClampFreePan(OtherFocus, ViewPoint - OtherFocus, Lab, 3000.f);
	TestTrue(TEXT("the limit is the level's, not the focus's (focus A)"), FVector2D(ViaA).Equals(FVector2D(8500.0, -8500.0), 0.01));
	TestTrue(TEXT("... (focus B)"), FVector2D(ViaB).Equals(FVector2D(8500.0, -8500.0), 0.01));
	// A character outside the area keeps his own view (the grown area holds the focus).
	const FVector Outside(15000.f, 0.f, 0.f);
	TestEqual(TEXT("a focus outside the level: his own view stays"), ClampFreePan(Outside, FVector::ZeroVector, Lab, 3000.f), FVector::ZeroVector);
	// No area known yet (before the map's setup): no limit at all.
	TestEqual(TEXT("no level area: unchanged"), ClampFreePan(Focus, FVector(50000.f, -50000.f, 0.f), FBox2D(ForceInit), 3000.f), FVector(50000.f, -50000.f, 0.f));
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
	TestTrue(TEXT("Leader inside"), ShouldBeActive(true, true));
	TestFalse(TEXT("Leader outside"), ShouldBeActive(true, false));
	TestFalse(TEXT("Switch disabled"), ShouldBeActive(false, true));
	// User request 2026-10-08: per-volume modes, defaults = real time on, turn-based / tactical pause off.
	const FCameraZoneModes Defaults;
	TestTrue(TEXT("Exploration shows the zone camera"), IsCameraAllowed(Defaults, ECodexCombatMode::None));
	TestTrue(TEXT("Real-time combat shows it"), IsCameraAllowed(Defaults, ECodexCombatMode::RealTime));
	TestFalse(TEXT("Tactical pause hides it"), IsCameraAllowed(Defaults, ECodexCombatMode::TacticalPause));
	TestFalse(TEXT("Turn-based hides it"), IsCameraAllowed(Defaults, ECodexCombatMode::TurnBased));
	FCameraZoneModes Custom;
	Custom.bRealTime = false;
	Custom.bTurnBased = true;
	Custom.bTacticalPause = true;
	TestFalse(TEXT("Custom: real time off"), IsCameraAllowed(Custom, ECodexCombatMode::RealTime));
	TestTrue(TEXT("Custom: turn-based on"), IsCameraAllowed(Custom, ECodexCombatMode::TurnBased));
	TestTrue(TEXT("Custom: pause on"), IsCameraAllowed(Custom, ECodexCombatMode::TacticalPause));
	TestTrue(TEXT("Custom: exploration always"), IsCameraAllowed(Custom, ECodexCombatMode::None));
	TestEqual(TEXT("Bunker cold x0"), GetColdMultiplier(ECameraZoneEnvironment::Closed), 0.f);
	TestEqual(TEXT("Shelter cold x0.5"), GetColdMultiplier(ECameraZoneEnvironment::Shelter), 0.5f);
	TestEqual(TEXT("Open cold x1"), GetColdMultiplier(ECameraZoneEnvironment::Standard), 1.f);
	TestEqual(TEXT("Blizzard cold x2.5"), GetColdMultiplier(ECameraZoneEnvironment::Blizzard), 2.5f);
	return true;
}

CAMERA_TEST(FGameFlowWaveActiveTest, "GameFlow.Phases.WaveActiveMatchesGodot")
bool FGameFlowWaveActiveTest::RunTest(const FString&)
{
	FGameFlowConfig Prepared;
	Prepared.bSuddenWaveStart = false; // Godot's is_wave_active through the classic prepared start
	Prepared.bVictoryPanelBetweenWaves = true; // ... and its victory panel between waves (off by default since 2026-10-25)
	FGameFlowStateMachine Machine(Prepared);
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

CAMERA_TEST(FCameraLadderSpringTest, "Camera.LadderZSpring")
bool FCameraLadderSpringTest::RunTest(const FString&)
{
	// docs/design/LADDERS.md 7.3: the focus height follows a climber through a critically damped spring (half-life 0.35 s).
	float Z = 0.f;
	float Velocity = 0.f;
	float Previous = 0.f;
	bool bMonotone = true;
	float AtHalfLife = 0.f;
	for (int32 Frame = 1; Frame <= 120; ++Frame)
	{
		Z = TacticalCameraRules::SpringDamp(Z, Velocity, 100.f, 0.35f, 1.f / 60.f);
		bMonotone &= Z >= Previous - 0.001f && Z <= 100.001f;
		Previous = Z;
		if (Frame == 21)
		{
			AtHalfLife = Z;
		}
	}
	TestTrue(TEXT("no overshoot, never back down (no pumping)"), bMonotone);
	TestTrue(TEXT("after one half-life the gap is ~60 % (critically damped from rest)"), AtHalfLife > 30.f && AtHalfLife < 50.f);
	TestTrue(TEXT("settled after 2 s"), FMath::Abs(Z - 100.f) < 1.f);
	float Big = 0.f;
	float BigVelocity = 0.f;
	Big = TacticalCameraRules::SpringDamp(Big, BigVelocity, 100.f, 0.35f, 5.f);
	TestTrue(TEXT("stable for a huge step"), Big > 90.f && Big <= 100.001f);
	float Steady = 50.f;
	float SteadyVelocity = 0.f;
	TestTrue(TEXT("at the target it stays"), FMath::IsNearlyEqual(TacticalCameraRules::SpringDamp(Steady, SteadyVelocity, 50.f, 0.35f, 0.016f), 50.f));
	return true;
}

#undef CAMERA_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
