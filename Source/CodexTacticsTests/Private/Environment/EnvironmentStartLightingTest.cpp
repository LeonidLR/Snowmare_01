#include "Misc/AutomationTest.h"
#include "Environment/EnvironmentStartRules.h"
#include "Environment/WeatherSnowFxRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Section 16 of docs/systems/environment-weather.md (game designer 2026-10-25): the level opens in its final look on frame 1 (no authored
// sunset, no snow ramp) and the weather presenter stands alone (its own rig when a level has no lighting actors). Pure rules only; the
// in-game checks are the smokes StartLightingSmoke (nullrhi) and StartSnowSmoke (rendered).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnvironmentStartRigPlanTest, "CodexTactics.Environment.StartLighting.RigPlan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnvironmentStartRigPlanTest::RunTest(const FString&)
{
	// The Demo 01 levels today: sun + sky atmosphere + sky light + fog, no moon, no unbound post-process volume.
	FLevelLightingInventory Template;
	Template.DirectionalLights = 1;
	Template.bSkyAtmosphere = Template.bSkyLight = Template.bHeightFog = true;
	const FWeatherRigPlan Adopted = EnvironmentStartRules::PlanRig(Template, true);
	TestTrue(TEXT("a level with its lights: adopted"), Adopted.Mode == EWeatherRigMode::Adopted);
	TestFalse(TEXT("adopted: no own sun"), Adopted.bOwnSun);
	TestFalse(TEXT("adopted: no own sky atmosphere"), Adopted.bOwnSkyAtmosphere);
	TestFalse(TEXT("adopted: no own sky light"), Adopted.bOwnSkyLight);
	TestFalse(TEXT("adopted: no own fog"), Adopted.bOwnHeightFog);
	TestTrue(TEXT("one directional light: the moon is the presenter's (as before)"), Adopted.bOwnMoon);
	TestTrue(TEXT("no unbound volume: the post-process is the presenter's (as before)"), Adopted.bOwnPostProcess);

	// After remove_level_lighting.py: nothing left.
	const FWeatherRigPlan Own = EnvironmentStartRules::PlanRig(FLevelLightingInventory(), true);
	TestTrue(TEXT("a level without lights: own rig"), Own.Mode == EWeatherRigMode::Own);
	TestTrue(TEXT("own: sun"), Own.bOwnSun && Own.bOwnMoon);
	TestTrue(TEXT("own: sky atmosphere + sky light"), Own.bOwnSkyAtmosphere && Own.bOwnSkyLight);
	TestTrue(TEXT("own: height fog + post-process"), Own.bOwnHeightFog && Own.bOwnPostProcess);

	// A partly cleaned level: the level's sun stays, the rest is own.
	FLevelLightingInventory SunOnly;
	SunOnly.DirectionalLights = 1;
	const FWeatherRigPlan Mixed = EnvironmentStartRules::PlanRig(SunOnly, true);
	TestTrue(TEXT("sun only: mixed"), Mixed.Mode == EWeatherRigMode::Mixed);
	TestFalse(TEXT("mixed: the level's sun is kept"), Mixed.bOwnSun);
	TestTrue(TEXT("mixed: the missing parts are own"), Mixed.bOwnSkyAtmosphere && Mixed.bOwnSkyLight && Mixed.bOwnHeightFog);

	// bBuildMissingLighting off = the old behaviour: a missing part stays missing.
	const FWeatherRigPlan Old = EnvironmentStartRules::PlanRig(FLevelLightingInventory(), false);
	TestTrue(TEXT("build missing off: nothing own (reported as adopted)"), Old.Mode == EWeatherRigMode::Adopted);
	TestFalse(TEXT("build missing off: no own sun / sky / fog"), Old.bOwnSun || Old.bOwnSkyAtmosphere || Old.bOwnSkyLight || Old.bOwnHeightFog);

	// A level with a moon and a global grade keeps both.
	FLevelLightingInventory Full = Template;
	Full.DirectionalLights = 2;
	Full.bUnboundPostProcess = true;
	const FWeatherRigPlan WithMoon = EnvironmentStartRules::PlanRig(Full, true);
	TestFalse(TEXT("a second directional light is the moon (adopted)"), WithMoon.bOwnMoon);
	TestFalse(TEXT("an unbound post-process volume is adopted"), WithMoon.bOwnPostProcess);

	TestEqual(TEXT("mode names"), EnvironmentStartRules::RigModeToString(EWeatherRigMode::Adopted) + TEXT("/")
		+ EnvironmentStartRules::RigModeToString(EWeatherRigMode::Own) + TEXT("/") + EnvironmentStartRules::RigModeToString(EWeatherRigMode::Mixed),
		FString(TEXT("adopted/own/mixed")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnvironmentStartSnowWarmupTest, "CodexTactics.Environment.StartLighting.SnowWarmup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnvironmentStartSnowWarmupTest::RunTest(const FString&)
{
	FWeatherSnowFxTuning Tuning;
	// 0 = the longest flake life: a flake born at the top of the spawn cylinder has reached the ground, the air is full.
	TestEqual(TEXT("default warm-up = the longest flake life"), EnvironmentStartRules::GetSnowWarmupSeconds(Tuning), Tuning.LifetimeMax, 1.e-4f);
	Tuning.LifetimeMin = 12.f;
	TestEqual(TEXT("min > max: the longer one"), EnvironmentStartRules::GetSnowWarmupSeconds(Tuning), 12.f, 1.e-4f);
	Tuning.StartWarmupSeconds = 5.f;
	TestEqual(TEXT("an explicit warm-up wins"), EnvironmentStartRules::GetSnowWarmupSeconds(Tuning), 5.f, 1.e-4f);
	Tuning.StartWarmupSeconds = 100.f;
	TestEqual(TEXT("clamped to the load-time budget"), EnvironmentStartRules::GetSnowWarmupSeconds(Tuning), EnvironmentStartRules::MaxWarmupSeconds, 1.e-4f);

	TestEqual(TEXT("9 s at 0.1 s = 90 ticks"), EnvironmentStartRules::GetSnowWarmupTicks(9.f, 0.1f), 90);
	TestEqual(TEXT("a partial tick rounds up"), EnvironmentStartRules::GetSnowWarmupTicks(9.05f, 0.1f), 91);
	TestEqual(TEXT("less than a tick = 1"), EnvironmentStartRules::GetSnowWarmupTicks(0.05f, 0.1f), 1);
	TestEqual(TEXT("nothing to warm"), EnvironmentStartRules::GetSnowWarmupTicks(0.f, 0.1f), 0);
	TestTrue(TEXT("a zero tick is clamped (no endless loop)"), EnvironmentStartRules::GetSnowWarmupTicks(1.f, 0.f) <= 240);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnvironmentStartSnapTest, "CodexTactics.Environment.StartLighting.Snap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnvironmentStartSnapTest::RunTest(const FString&)
{
	TestTrue(TEXT("the first application snaps"), EnvironmentStartRules::ShouldSnap(true, 1, 1));
	TestFalse(TEXT("a weather change in play does not snap (same serial)"), EnvironmentStartRules::ShouldSnap(false, 3, 3));
	TestTrue(TEXT("a state jump (save load) snaps"), EnvironmentStartRules::ShouldSnap(false, 3, 4));

	TestFalse(TEXT("no snap yet"), EnvironmentStartRules::IsExposureSnapActive(-1.f, 1.f));
	TestTrue(TEXT("snapping on the snap frame"), EnvironmentStartRules::IsExposureSnapActive(0.f, 1.f));
	TestTrue(TEXT("snapping inside the window"), EnvironmentStartRules::IsExposureSnapActive(0.99f, 1.f));
	TestFalse(TEXT("authored speeds after the window"), EnvironmentStartRules::IsExposureSnapActive(1.f, 1.f));

	// Section 19 (start flicker 2026-10-27): no fast adaptation window by default - the camera-cut frames settle the exposure.
	TestFalse(TEXT("the default window is off (the level start is never at a fast speed)"),
		EnvironmentStartRules::IsExposureSnapActive(0.f, EnvironmentStartRules::ExposureSnapSeconds));
	TestTrue(TEXT("the snap has camera-cut frames (the engine's eye adaptation ForceTarget)"), EnvironmentStartRules::SnapCameraCutFrames >= 2);
	// The eye adaptation closes 1 - 2^(-speed x dt) of the gap per frame.
	TestTrue(TEXT("the old 1000 f-stops / s closed the whole gap in one 60 fps frame (the oscillation)"),
		EnvironmentStartRules::GetExposureFrameClosure(1000.f, 1.f / 60.f) > 0.9999f);
	const float SnapClosure = EnvironmentStartRules::GetExposureFrameClosure(EnvironmentStartRules::ExposureSnapSpeed, 1.f / 60.f);
	TestTrue(FString::Printf(TEXT("the optional snap speed is stable at 60 fps (%.3f of the gap per frame)"), SnapClosure),
		SnapClosure <= EnvironmentStartRules::MaxStableExposureClosure);
	TestEqual(TEXT("3 f-stops / s closes 0.034 per 60 fps frame"), EnvironmentStartRules::GetExposureFrameClosure(3.f, 1.f / 60.f), 1.f - FMath::Exp2(-0.05f), 1.e-5f);
	// A long frame (a PIE hitch) caps the speed so that frame stays stable too.
	for (const float FrameSeconds : { 1.f / 120.f, 1.f / 60.f, 0.1f, 0.5f })
	{
		const float Capped = EnvironmentStartRules::ClampStableExposureSpeed(1000.f, FrameSeconds);
		TestTrue(FString::Printf(TEXT("1000 capped to %.1f at %.3f s per frame: closure %.3f"), Capped, FrameSeconds,
					 EnvironmentStartRules::GetExposureFrameClosure(Capped, FrameSeconds)),
			EnvironmentStartRules::GetExposureFrameClosure(Capped, FrameSeconds) <= EnvironmentStartRules::MaxStableExposureClosure + 1.e-4f);
	}
	TestEqual(TEXT("a stable speed is kept as it is"), EnvironmentStartRules::ClampStableExposureSpeed(3.f, 1.f / 60.f), 3.f);
	TestEqual(TEXT("a negative speed is 0"), EnvironmentStartRules::ClampStableExposureSpeed(-1.f, 1.f / 60.f), 0.f);
	return true;
}

#endif
