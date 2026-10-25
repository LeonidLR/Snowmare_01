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

// DA_SnowFX (user request 2026-10-23): the look the component writes to NS_Weather_Snow's User parameters.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnvironmentSnowFxLookTest, "CodexTactics.Environment.SnowFx.Look",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnvironmentSnowFxLookTest::RunTest(const FString&)
{
	FWeatherSnowFxTuning Tuning;
	// The approved default: clearly smaller than the first version's 8-18 cm flakes.
	TestTrue(TEXT("default flakes smaller than the old 8-18 cm"), Tuning.FlakeSizeMin < 8.f && Tuning.FlakeSizeMax <= 12.f);
	TestTrue(TEXT("default flakes min < max"), Tuning.FlakeSizeMin < Tuning.FlakeSizeMax);

	Tuning.FlakeSizeMin = 3.f;
	Tuning.FlakeSizeMax = 7.f;
	Tuning.LifetimeMin = 5.f;
	Tuning.LifetimeMax = 8.f;
	Tuning.FallAcceleration = 1500.f;
	Tuning.SpeedLimit = 1800.f;
	Tuning.FlakeWindSpeed = 700.f;
	Tuning.SpawnRadius = 2500.f;
	Tuning.SpawnHeight = 2400.f;
	Tuning.DriftRateAtFull = 30.f;
	const FWeatherSnowFxParams Full = WeatherSnowFxRules::ComputeParams(1.f, 1.f, FVector::ForwardVector, Tuning);
	TestEqual(TEXT("size min"), Full.FlakeSizeMin, 3.f);
	TestEqual(TEXT("size max"), Full.FlakeSizeMax, 7.f);
	TestEqual(TEXT("life min"), Full.LifetimeMin, 5.f);
	TestEqual(TEXT("life max"), Full.LifetimeMax, 8.f);
	TestTrue(TEXT("fall acceleration = gravity down"), Full.Gravity.Equals(FVector(0., 0., -1500.)));
	TestEqual(TEXT("speed limit"), Full.SpeedLimit, 1800.f);
	TestEqual(TEXT("wind influence"), Full.FlakeWindSpeed, 700.f);
	TestEqual(TEXT("spawn radius"), Full.SpawnRadius, 2500.f);
	TestEqual(TEXT("spawn height"), Full.SpawnHeight, 2400.f);
	TestEqual(TEXT("drift rate at full"), Full.DriftRate, 30.f);

	// The look is written even with no snow (the asset reads right at the next snowfall); nothing spawns.
	const FWeatherSnowFxParams Off = WeatherSnowFxRules::ComputeParams(0.f, 0.f, FVector::ForwardVector, Tuning);
	TestEqual(TEXT("off: look kept"), Off.FlakeSizeMax, 7.f);
	TestEqual(TEXT("off: no drift puffs"), Off.DriftRate, 0.f);

	// Drift scales from its start intensity; switched off = no puffs at all.
	const float Half = Tuning.DriftStartIntensity + (1.f - Tuning.DriftStartIntensity) * 0.5f;
	TestEqual(TEXT("drift rate halfway"), WeatherSnowFxRules::ComputeParams(Half, 0.5f, FVector::ForwardVector, Tuning).DriftRate, 15.f, 1e-3f);
	Tuning.bDriftEnabled = false;
	const FWeatherSnowFxParams NoDrift = WeatherSnowFxRules::ComputeParams(1.f, 1.f, FVector::ForwardVector, Tuning);
	TestEqual(TEXT("drift off: rate 0"), NoDrift.DriftRate, 0.f);
	TestEqual(TEXT("drift off: opacity 0"), NoDrift.DriftOpacity, 0.f);
	TestTrue(TEXT("drift off: flakes still fall"), NoDrift.SpawnRate > 0.f);

	// Bad input from the editor: a min above its max is one value, nothing negative reaches Niagara.
	Tuning.FlakeSizeMin = 12.f;
	Tuning.FlakeSizeMax = 6.f;
	Tuning.LifetimeMin = -1.f;
	Tuning.FallAcceleration = -50.f;
	const FWeatherSnowFxParams Bad = WeatherSnowFxRules::ComputeParams(1.f, 1.f, FVector::ForwardVector, Tuning);
	TestEqual(TEXT("swapped sizes: max = min"), Bad.FlakeSizeMax, 12.f);
	TestTrue(TEXT("lifetime positive"), Bad.LifetimeMin > 0.f && Bad.LifetimeMax >= Bad.LifetimeMin);
	TestTrue(TEXT("no upward gravity"), Bad.Gravity.Z <= 0.);
	return true;
}

// No snow indoors: the kill boxes (Niagara KillParticlesInVolume, box mode) the component builds from interior zones and roofs.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnvironmentSnowFxKillBoxTest, "CodexTactics.Environment.SnowFx.KillBoxes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnvironmentSnowFxKillBoxTest::RunTest(const FString&)
{
	// The lake's fishermen's hut zone (Trigger_BoatsCabin): centre (-2180, -7090), half extent 606 x 400; 3 m tall standing on z 0.
	const FTransform Hut(FRotator::ZeroRotator, FVector(-2180., -7090., 150.));
	const FVector HutHalf(606., 400., 150.);
	const FBox Box = WeatherSnowFxRules::MakeZoneKillBox(Hut, HutHalf, 20.f, 150.f);
	TestTrue(TEXT("hut box valid"), Box.IsValid != 0);
	TestTrue(TEXT("sides grown by the margin"), Box.Min.Equals(FVector(-2180. - 626., -7090. - 420., -150.), 0.01)
		&& Box.Max.Equals(FVector(-2180. + 626., -7090. + 420., 300.), 0.01));
	TestTrue(TEXT("a flake by the crate is killed"), WeatherSnowFxRules::IsInsideKillBox(FVector(-2000., -7000., 100.), Box));
	TestTrue(TEXT("a flake on the floor is killed"), WeatherSnowFxRules::IsInsideKillBox(FVector(-2180., -7090., 1.), Box));
	TestFalse(TEXT("a flake above the roof lives"), WeatherSnowFxRules::IsInsideKillBox(FVector(-2180., -7090., 320.), Box));
	TestFalse(TEXT("a flake 1 m outside the wall lives"), WeatherSnowFxRules::IsInsideKillBox(FVector(-2180. + 606. + 100., -7090., 100.), Box));

	// A turned zone: the axis-aligned bounds of the rotated box (90 deg swaps the extents).
	const FBox Turned = WeatherSnowFxRules::MakeZoneKillBox(FTransform(FRotator(0., 90., 0.), FVector::ZeroVector), FVector(600., 200., 100.), 0.f, 0.f);
	TestTrue(TEXT("90 deg: extents swapped"), Turned.GetExtent().Equals(FVector(200., 600., 100.), 0.01));
	const FBox Diagonal = WeatherSnowFxRules::MakeZoneKillBox(FTransform(FRotator(0., 45., 0.), FVector::ZeroVector), FVector(100., 100., 100.), 0.f, 0.f);
	TestEqual(TEXT("45 deg: the bounds hold the corners"), Diagonal.Max.X, 100. * FMath::Sqrt(2.), 0.01);
	TestFalse(TEXT("flat zone: no box"), WeatherSnowFxRules::MakeZoneKillBox(Hut, FVector(600., 400., 0.), 0.f, 0.f).IsValid != 0);

	// The Niagara slot of an unused box: zero size far below the world (nothing inside); a used one = centre + full size.
	FVector Origin, Size;
	WeatherSnowFxRules::PackKillBox(FBox(ForceInit), Origin, Size);
	TestTrue(TEXT("unused slot: zero size"), Size.IsZero());
	TestTrue(TEXT("unused slot: far below"), Origin.Z < -1.0e6);
	WeatherSnowFxRules::PackKillBox(Box, Origin, Size);
	TestTrue(TEXT("slot origin = centre"), Origin.Equals(Box.GetCenter()));
	TestTrue(TEXT("slot size = full size"), Size.Equals(Box.GetSize()));

	// Roofs without a zone: a hit within the trace height on a mesh no wider than RoofMaxSizeCm.
	const FWeatherSnowFxTuning Tuning;
	const FBox Shed(FVector(0., 0., 280.), FVector(800., 500., 320.));
	TestTrue(TEXT("a shed roof 3 m up is a roof"), WeatherSnowFxRules::IsRoofCandidate(Shed, 290., 0., Tuning));
	TestFalse(TEXT("too high above"), WeatherSnowFxRules::IsRoofCandidate(Shed, Tuning.RoofTraceHeight + 10., 0., Tuning));
	TestFalse(TEXT("a sky dome / foliage cluster is not"), WeatherSnowFxRules::IsRoofCandidate(FBox(FVector(-50000.), FVector(50000.)), 2000., 0., Tuning));
	const FBox UnderShed = WeatherSnowFxRules::MakeRoofKillBox(Shed, 0., 20.f, 150.f);
	TestTrue(TEXT("under the shed: footprint + margin, floor - below .. roof top"),
		UnderShed.Min.Equals(FVector(-20., -20., -150.), 0.01) && UnderShed.Max.Equals(FVector(820., 520., 320.), 0.01));
	TestFalse(TEXT("roof below the floor: no box"), WeatherSnowFxRules::MakeRoofKillBox(Shed, 400., 0.f, 0.f).IsValid != 0);

	// Selection: nearest first within the radius (to the edge), a box inside a chosen one dropped, at most KillBoxCount.
	TArray<FBox> Candidates;
	for (int32 Index = 0; Index < 6; ++Index)
	{
		Candidates.Add(FBox(FVector(1000. * Index, 0., 0.), FVector(1000. * Index + 500., 500., 300.)));
	}
	Candidates.Add(FBox(FVector(100., 100., 0.), FVector(200., 200., 100.))); // inside the first one
	Candidates.Add(FBox(ForceInit));
	const TArray<FBox> Chosen = WeatherSnowFxRules::SelectKillBoxes(Candidates, FVector(5200., 250., 0.), 100000., WeatherSnowFxRules::KillBoxCount);
	TestEqual(TEXT("capped"), Chosen.Num(), WeatherSnowFxRules::KillBoxCount);
	TestTrue(TEXT("the one under the focus first"), Chosen.Num() > 0 && Chosen[0].Min.X == 5000.);
	TestTrue(TEXT("then the next nearest"), Chosen.Num() > 1 && Chosen[1].Min.X == 4000.);
	const TArray<FBox> Near = WeatherSnowFxRules::SelectKillBoxes(Candidates, FVector(250., 250., 0.), 800., WeatherSnowFxRules::KillBoxCount);
	TestEqual(TEXT("radius: two boxes in reach, the inner one dropped"), Near.Num(), 2);
	TestEqual(TEXT("edge distance 0 inside"), WeatherSnowFxRules::DistanceSq2D(FVector(250., 250., 999.), Candidates[0]), 0.);
	TestTrue(TEXT("nothing in reach"), WeatherSnowFxRules::SelectKillBoxes(Candidates, FVector(-90000., 0., 0.), 1000., 4).IsEmpty());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
