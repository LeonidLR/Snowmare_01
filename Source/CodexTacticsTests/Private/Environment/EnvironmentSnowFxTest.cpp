#include "Misc/AutomationTest.h"
#include "Environment/WeatherSnowFxRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Environment phase 4b, the falling snow's pure logic (docs/systems/environment-weather.md section 12): intensity smoothing, the
// contract User parameter values by intensity / wind, and the ground point the view looks at.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnvironmentSnowFxIntensityTest, "CodexTactics.Environment.SnowFx.IntensitySmoothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnvironmentSnowFxIntensityTest::RunTest(const FString&)
{
	const FWeatherSnowFxTuning Tuning;
	// An instant weather change fades in at the rise rate, never overshoots.
	float I = WeatherSnowFxRules::StepIntensity(0.f, 1.f, 1.f, Tuning);
	TestEqual(TEXT("rises by the rate per second"), I, Tuning.IntensityRisePerSecond, 1e-4f);
	for (int32 Step = 0; Step < 100; ++Step)
	{
		I = WeatherSnowFxRules::StepIntensity(I, 1.f, 0.1f, Tuning);
	}
	TestEqual(TEXT("reaches the target"), I, 1.f, 1e-4f);
	TestEqual(TEXT("falls by the fall rate"), WeatherSnowFxRules::StepIntensity(1.f, 0.f, 1.f, Tuning), 1.f - Tuning.IntensityFallPerSecond, 1e-4f);
	TestEqual(TEXT("no overshoot downward"), WeatherSnowFxRules::StepIntensity(0.1f, 0.f, 10.f, Tuning), 0.f, 1e-4f);
	TestEqual(TEXT("a negative delta changes nothing"), WeatherSnowFxRules::StepIntensity(0.4f, 1.f, -1.f, Tuning), 0.4f, 1e-4f);
	TestEqual(TEXT("inputs are clamped"), WeatherSnowFxRules::StepIntensity(3.f, 2.f, 0.f, Tuning), 1.f, 1e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnvironmentSnowFxParamsTest, "CodexTactics.Environment.SnowFx.Params",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnvironmentSnowFxParamsTest::RunTest(const FString&)
{
	const FWeatherSnowFxTuning Tuning;
	const FVector Wind(0.f, 3.f, 2.f);

	const FWeatherSnowFxParams None = WeatherSnowFxRules::ComputeParams(0.f, 0.f, Wind, Tuning);
	TestEqual(TEXT("intensity 0: nothing spawns"), None.SpawnRate, 0.f);
	TestEqual(TEXT("intensity 0: invisible"), None.Opacity, 0.f);
	TestEqual(TEXT("intensity 0: no drift"), None.DriftOpacity, 0.f);
	TestEqual(TEXT("wind keeps its floor"), None.WindStrength, Tuning.WindStrengthFloor);
	TestTrue(TEXT("wind direction horizontal unit"), None.WindDirection.Equals(FVector(0.f, 1.f, 0.f), 1e-4f));
	TestTrue(TEXT("zero wind direction falls back to +X"),
		WeatherSnowFxRules::ComputeParams(0.5f, 0.5f, FVector::ZeroVector, Tuning).WindDirection.Equals(FVector::ForwardVector));

	// Presets: Snowfall 0.6 / wind 0.45, Blizzard 1 / wind 1 (EnvironmentVisualRules::GetDefaultPreset).
	const FWeatherSnowFxParams Snowfall = WeatherSnowFxRules::ComputeParams(0.6f, 0.45f, Wind, Tuning);
	const FWeatherSnowFxParams Blizzard = WeatherSnowFxRules::ComputeParams(1.f, 1.f, Wind, Tuning);
	TestTrue(TEXT("snowfall spawns"), Snowfall.SpawnRate > 0.f);
	TestEqual(TEXT("blizzard = full rate"), Blizzard.SpawnRate, Tuning.SpawnRateAtFull, 1.f);
	TestTrue(TEXT("blizzard at least 2.5x denser than snowfall"), Blizzard.SpawnRate >= 2.5f * Snowfall.SpawnRate);
	TestTrue(TEXT("blizzard more opaque"), Blizzard.Opacity > Snowfall.Opacity);
	TestTrue(TEXT("blizzard drifts more"), Blizzard.DriftOpacity > Snowfall.DriftOpacity);
	TestTrue(TEXT("blizzard drift puffs bigger"), Blizzard.DriftSpriteSize > Snowfall.DriftSpriteSize);
	TestEqual(TEXT("blizzard drift at its max"), Blizzard.DriftOpacity, Tuning.DriftOpacityMax, 1e-4f);
	TestEqual(TEXT("wind passed through"), Blizzard.WindStrength, 1.f);
	TestEqual(TEXT("intensity passed through"), Snowfall.WeatherIntensity, 0.6f, 1e-4f);
	TestEqual(TEXT("no drift below its start"), WeatherSnowFxRules::ComputeParams(Tuning.DriftStartIntensity - 0.05f, 0.f, Wind, Tuning).DriftOpacity, 0.f);

	// Monotonic in intensity.
	float LastRate = -1.f;
	for (float Intensity = Tuning.OffThreshold; Intensity <= 1.f; Intensity += 0.05f)
	{
		const float Rate = WeatherSnowFxRules::ComputeParams(Intensity, 0.5f, Wind, Tuning).SpawnRate;
		TestTrue(*FString::Printf(TEXT("rate rises at %.2f"), Intensity), Rate > LastRate);
		LastRate = Rate;
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnvironmentSnowFxFocusTest, "CodexTactics.Environment.SnowFx.ViewFocus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnvironmentSnowFxFocusTest::RunTest(const FString&)
{
	// The tactical camera: pitch -35.26 deg, 26 m from the focus.
	const FRotator View(-35.26f, 45.f, 0.f);
	const FVector Focus(1200.f, -300.f, 50.f);
	const FVector Camera = Focus - View.Vector() * 2600.f;
	TestTrue(TEXT("the view ray meets the ground at the focus"),
		WeatherSnowFxRules::ComputeViewFocus(Camera, View.Vector(), Focus.Z, 2500.f).Equals(Focus, 0.5f));
	// Looking flat or up: a point ahead on the ground plane.
	const FVector Flat = WeatherSnowFxRules::ComputeViewFocus(FVector(0.f, 0.f, 1000.f), FVector(1.f, 0.f, 0.2f), 0.f, 2500.f);
	TestEqual(TEXT("fallback lies on the ground plane"), Flat.Z, 0.);
	TestTrue(TEXT("fallback ahead"), Flat.X > 2000.);
	// The spawn volume moves upwind with the wind squared (the asset's wind speed curve); none without a direction.
	const FWeatherSnowFxTuning Tuning;
	const FVector Calm = WeatherSnowFxRules::ComputeSpawnOffset(FVector(1.f, 0.f, 0.5f), 0.f, Tuning);
	const FVector Snowfall = WeatherSnowFxRules::ComputeSpawnOffset(FVector(1.f, 0.f, 0.5f), 0.45f, Tuning);
	const FVector Blizzard = WeatherSnowFxRules::ComputeSpawnOffset(FVector(2.f, 0.f, 0.f), 1.f, Tuning);
	TestTrue(TEXT("no wind: on the focus"), Calm.IsNearlyZero());
	TestTrue(TEXT("blizzard: full offset upwind"), Blizzard.Equals(FVector(-Tuning.UpwindOffsetAtFullWind, 0.f, 0.f), 0.01f));
	TestEqual(TEXT("snowfall: wind^2 of it"), Snowfall.X, -Tuning.UpwindOffsetAtFullWind * 0.45 * 0.45, 0.5);
	TestEqual(TEXT("horizontal only"), Snowfall.Z, 0.);
	TestTrue(TEXT("zero direction: no offset"), WeatherSnowFxRules::ComputeSpawnOffset(FVector::ZeroVector, 1.f, Tuning).IsNearlyZero());
	// Camera below the plane: fallback too.
	TestEqual(TEXT("below the plane: on the plane"), WeatherSnowFxRules::ComputeViewFocus(FVector(0.f, 0.f, -100.f), FVector(0.f, 0.f, -1.f), 0.f, 100.f).Z, 0.);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
