#include "Misc/AutomationTest.h"
#include "Environment/RainFxSettings.h"
#include "Environment/SnowOverlayRules.h"
#include "Environment/WeatherRainFxRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Environment section 18, the rain visuals' pure logic (docs/systems/environment-weather.md section 18): the look per rung, intensity ->
// spawn / opacity / slant / splashes, puddle-splash density, screen-drop strength, the indoor kill-box rules shared with the falling snow,
// the deterministic lightning schedule and the wet overlay gate. UE-only (no Godot reference).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnvironmentRainFxLookTest, "CodexTactics.Environment.RainFx.LookPerWeather",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnvironmentRainFxLookTest::RunTest(const FString&)
{
	const FWeatherRainFxTuning Tuning;
	TestEqual(TEXT("SunnyDay / Overcast: no rain"), WeatherRainFxRules::GetLookIntensity(0.f, 0.f, Tuning), 0.f);
	TestEqual(TEXT("Rain rung = RainLookIntensity"), WeatherRainFxRules::GetLookIntensity(1.f, 0.f, Tuning), Tuning.RainLookIntensity, 1e-5f);
	TestEqual(TEXT("Storm rung = StormLookIntensity"), WeatherRainFxRules::GetLookIntensity(0.f, 1.f, Tuning), Tuning.StormLookIntensity, 1e-5f);
	TestTrue(TEXT("the storm looks heavier than the rain"), Tuning.StormLookIntensity > Tuning.RainLookIntensity);
	TestEqual(TEXT("a Rain -> Storm transition blends"), WeatherRainFxRules::GetLookIntensity(0.5f, 0.5f, Tuning),
		0.5f * (Tuning.RainLookIntensity + Tuning.StormLookIntensity), 1e-5f);
	TestEqual(TEXT("inputs clamped"), WeatherRainFxRules::GetLookIntensity(3.f, 3.f, Tuning), 1.f, 1e-5f);

	// Smoothing.
	float I = WeatherRainFxRules::StepIntensity(0.f, 1.f, 1.f, Tuning);
	TestEqual(TEXT("rises by the rise rate"), I, Tuning.IntensityRisePerSecond, 1e-5f);
	TestEqual(TEXT("falls by the fall rate"), WeatherRainFxRules::StepIntensity(1.f, 0.f, 1.f, Tuning), 1.f - Tuning.IntensityFallPerSecond, 1e-5f);
	TestEqual(TEXT("a jump delta reaches the target"), WeatherRainFxRules::StepIntensity(0.f, 0.55f, 1000.f, Tuning), 0.55f, 1e-5f);
	TestEqual(TEXT("a negative delta changes nothing"), WeatherRainFxRules::StepIntensity(0.3f, 1.f, -1.f, Tuning), 0.3f, 1e-5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnvironmentRainFxParamsTest, "CodexTactics.Environment.RainFx.IntensityToSpawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnvironmentRainFxParamsTest::RunTest(const FString&)
{
	const FWeatherRainFxTuning Tuning;
	const FVector Wind(3.f, 0.f, 5.f);
	const FWeatherRainFxParams None = WeatherRainFxRules::ComputeParams(0.f, 0.3f, Wind, Tuning);
	TestEqual(TEXT("0: nothing spawns"), None.SpawnRate, 0.f);
	TestEqual(TEXT("0: no splashes"), None.SplashRate, 0.f);
	TestEqual(TEXT("0: invisible"), None.Opacity, 0.f);
	TestEqual(TEXT("under the off threshold: nothing"), WeatherRainFxRules::ComputeParams(Tuning.OffThreshold * 0.5f, 0.f, Wind, Tuning).SpawnRate, 0.f);
	TestTrue(TEXT("wind direction horizontal unit"), None.WindDirection.Equals(FVector::ForwardVector, 1e-4f));
	TestTrue(TEXT("zero wind direction -> +X"), WeatherRainFxRules::ComputeParams(0.5f, 0.5f, FVector::ZeroVector, Tuning).WindDirection.Equals(FVector::ForwardVector));

	// The default presets: Rain wind 0.45, Storm wind 0.9 (EnvironmentVisualRules temperate presets).
	const FWeatherRainFxParams Rain = WeatherRainFxRules::ComputeParams(Tuning.RainLookIntensity, 0.45f, Wind, Tuning);
	const FWeatherRainFxParams Storm = WeatherRainFxRules::ComputeParams(Tuning.StormLookIntensity, 0.9f, Wind, Tuning);
	TestTrue(TEXT("rain spawns"), Rain.SpawnRate > 0.f);
	TestEqual(TEXT("storm = the full rate"), Storm.SpawnRate, Tuning.SpawnRateAtFull, 1.f);
	TestTrue(TEXT("storm at least 2x denser than rain"), Storm.SpawnRate >= 2.f * Rain.SpawnRate);
	TestEqual(TEXT("rate exponent"), Rain.SpawnRate, Tuning.SpawnRateAtFull * FMath::Pow(Tuning.RainLookIntensity, Tuning.SpawnRateExponent), 1.f);
	TestTrue(TEXT("storm more opaque"), Storm.Opacity > Rain.Opacity);
	TestTrue(TEXT("storm more splashes"), Storm.SplashRate > Rain.SplashRate && Rain.SplashRate > 0.f);
	TestTrue(TEXT("storm slants more (wind^2)"), Storm.DropWindSpeed > 3.f * Rain.DropWindSpeed);
	TestEqual(TEXT("slant = DropWindSpeed x wind^2"), Storm.DropWindSpeed, Tuning.DropWindSpeed * 0.81f, 0.5f);
	TestTrue(TEXT("falls down"), Storm.Gravity.Z < 0.f && Storm.FallSpeed > 0.f);
	TestTrue(TEXT("the slant stays under 45 deg in the storm"), Storm.DropWindSpeed < Storm.FallSpeed);

	// Hand-edited nonsense is sanitised.
	FWeatherRainFxTuning Bad;
	Bad.DropLengthMin = 200.f;
	Bad.DropLengthMax = 50.f;
	Bad.DropWidthMin = -1.f;
	Bad.SpawnRadius = 0.f;
	Bad.bSplashes = false;
	const FWeatherRainFxParams Fixed = WeatherRainFxRules::ComputeParams(1.f, 1.f, Wind, Bad);
	TestTrue(TEXT("length max >= min"), Fixed.DropLengthMax >= Fixed.DropLengthMin);
	TestTrue(TEXT("width positive"), Fixed.DropWidthMin > 0.f);
	TestTrue(TEXT("radius floor"), Fixed.SpawnRadius >= 100.f);
	TestEqual(TEXT("splashes switched off"), Fixed.SplashRate, 0.f);

	// The spawn offset: upwind by UpwindOffsetAtFullWind x wind^2.
	const FVector Offset = WeatherRainFxRules::ComputeSpawnOffset(FVector(0.f, 1.f, 0.f), 1.f, Tuning);
	TestTrue(TEXT("upwind at full wind"), Offset.Equals(FVector(0.f, -Tuning.UpwindOffsetAtFullWind, 0.f), 0.1f));
	TestTrue(TEXT("no offset without wind"), WeatherRainFxRules::ComputeSpawnOffset(FVector(0.f, 1.f, 0.f), 0.f, Tuning).IsNearlyZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnvironmentRainFxPuddleTest, "CodexTactics.Environment.RainFx.PuddleSplashDensity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnvironmentRainFxPuddleTest::RunTest(const FString&)
{
	const FWeatherRainFxTuning Tuning;
	TestEqual(TEXT("dry ground: none"), WeatherRainFxRules::ComputePuddleSplashRate(1.f, 0.f, Tuning), 0.f);
	TestEqual(TEXT("below the puddle minimum: none"), WeatherRainFxRules::ComputePuddleSplashRate(1.f, Tuning.PuddleSplashMinPuddles * 0.5f, Tuning), 0.f);
	TestEqual(TEXT("no rain: none (puddles alone do not splash)"), WeatherRainFxRules::ComputePuddleSplashRate(0.f, 1.f, Tuning), 0.f);
	TestEqual(TEXT("full = the rate"), WeatherRainFxRules::ComputePuddleSplashRate(1.f, 1.f, Tuning), Tuning.PuddleSplashRateAtFull, 0.01f);
	TestEqual(TEXT("density = rate x intensity x puddles"), WeatherRainFxRules::ComputePuddleSplashRate(0.55f, 0.6f, Tuning),
		Tuning.PuddleSplashRateAtFull * 0.55f * 0.6f, 0.01f);
	TestTrue(TEXT("more puddles -> more splashes"),
		WeatherRainFxRules::ComputePuddleSplashRate(0.55f, 0.9f, Tuning) > WeatherRainFxRules::ComputePuddleSplashRate(0.55f, 0.3f, Tuning));
	TestTrue(TEXT("heavier rain -> more splashes"),
		WeatherRainFxRules::ComputePuddleSplashRate(1.f, 0.5f, Tuning) > WeatherRainFxRules::ComputePuddleSplashRate(0.55f, 0.5f, Tuning));
	FWeatherRainFxTuning Off;
	Off.bPuddleSplashes = false;
	TestEqual(TEXT("switched off"), WeatherRainFxRules::ComputePuddleSplashRate(1.f, 1.f, Off), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnvironmentRainFxScreenDropsTest, "CodexTactics.Environment.RainFx.ScreenDrops",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnvironmentRainFxScreenDropsTest::RunTest(const FString&)
{
	const FWeatherRainFxTuning Tuning;
	TestEqual(TEXT("MPC ScreenDrops 0: off"), WeatherRainFxRules::ComputeScreenDropsStrength(0.f, 1.f, Tuning), 0.f);
	TestEqual(TEXT("no rain: off"), WeatherRainFxRules::ComputeScreenDropsStrength(1.f, 0.f, Tuning), 0.f);
	TestEqual(TEXT("storm = DA strength"), WeatherRainFxRules::ComputeScreenDropsStrength(1.f, 1.f, Tuning), Tuning.ScreenDropsStrength, 1e-6f);
	TestEqual(TEXT("rain 0.6 = 0.6 x strength"), WeatherRainFxRules::ComputeScreenDropsStrength(1.f, 0.6f, Tuning), 0.6f * Tuning.ScreenDropsStrength, 1e-6f);
	TestTrue(TEXT("subtle at the tactical camera (<= 0.5)"), Tuning.ScreenDropsStrength <= 0.5f && Tuning.bScreenDrops);
	FWeatherRainFxTuning Off;
	Off.bScreenDrops = false;
	TestEqual(TEXT("DA switch off"), WeatherRainFxRules::ComputeScreenDropsStrength(1.f, 1.f, Off), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnvironmentRainFxKillBoxTest, "CodexTactics.Environment.RainFx.KillBoxReuse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnvironmentRainFxKillBoxTest::RunTest(const FString&)
{
	FWeatherRainFxTuning Tuning;
	Tuning.IndoorMarginCm = 33.f;
	Tuning.IndoorBelowCm = 120.f;
	Tuning.RoofTraceHeight = 1800.f;
	Tuning.RoofMaxSizeCm = 900.f;
	Tuning.IndoorSearchRadius = 4500.f;
	Tuning.bKillUnderRoofs = false;
	const FWeatherSnowFxTuning Indoor = WeatherRainFxRules::ToIndoorTuning(Tuning);
	TestEqual(TEXT("margin carried"), Indoor.IndoorMarginCm, 33.f);
	TestEqual(TEXT("below carried"), Indoor.IndoorBelowCm, 120.f);
	TestEqual(TEXT("roof trace carried"), Indoor.RoofTraceHeight, 1800.f);
	TestEqual(TEXT("roof size carried"), Indoor.RoofMaxSizeCm, 900.f);
	TestEqual(TEXT("search radius carried"), Indoor.IndoorSearchRadius, 4500.f);
	TestFalse(TEXT("roof switch carried"), Indoor.bKillUnderRoofs);
	TestEqual(TEXT("the same box count as the snow"), WeatherRainFxRules::KillBoxCount, WeatherSnowFxRules::KillBoxCount);

	// The hut of L_WeatherRainTest: a 6.6 x 5.6 m roof slab at z 300..320 over the leader standing at z 0 -> a kill box around him.
	const FBox Roof(FVector(570., 20., 300.), FVector(1230., 580., 320.));
	const FWeatherSnowFxTuning Defaults = WeatherRainFxRules::ToIndoorTuning(FWeatherRainFxTuning());
	TestTrue(TEXT("the hut roof is a roof candidate"), WeatherSnowFxRules::IsRoofCandidate(Roof, 300., 30., Defaults));
	const FBox Box = WeatherSnowFxRules::MakeRoofKillBox(Roof, 0., Defaults.IndoorMarginCm, Defaults.IndoorBelowCm);
	TestTrue(TEXT("the leader in the hut is inside the kill box"), WeatherSnowFxRules::IsInsideKillBox(FVector(900., 300., 95.), Box));
	TestFalse(TEXT("a point outside the hut is not"), WeatherSnowFxRules::IsInsideKillBox(FVector(1500., 300., 95.), Box));
	const FBox Huge(FVector(-4000., -4000., 300.), FVector(4000., 4000., 320.));
	TestFalse(TEXT("an 80 m slab is no roof (rain default)"), WeatherSnowFxRules::IsRoofCandidate(Huge, 300., 30., Defaults));
	const TArray<FBox> Selected = WeatherSnowFxRules::SelectKillBoxes({ Box }, FVector(900., 300., 0.), Defaults.IndoorSearchRadius, WeatherRainFxRules::KillBoxCount);
	TestEqual(TEXT("selected"), Selected.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnvironmentRainFxLightningTest, "CodexTactics.Environment.RainFx.Lightning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnvironmentRainFxLightningTest::RunTest(const FString&)
{
	const FWeatherRainFxTuning Tuning;
	// Suppression: -1 auto (off on automated runs), 0 off, 1 on.
	TestTrue(TEXT("auto + automated run = suppressed"), WeatherRainFxRules::IsLightningSuppressed(-1, true));
	TestFalse(TEXT("auto + a normal run = on"), WeatherRainFxRules::IsLightningSuppressed(-1, false));
	TestTrue(TEXT("0 = off"), WeatherRainFxRules::IsLightningSuppressed(0, false));
	TestFalse(TEXT("1 = on, also in a smoke"), WeatherRainFxRules::IsLightningSuppressed(1, true));
	TestFalse(TEXT("no storm: no lightning"), WeatherRainFxRules::IsLightningActive(0.f, false, Tuning));
	TestFalse(TEXT("rain only: no lightning"), WeatherRainFxRules::IsLightningActive(Tuning.LightningMinStorm * 0.9f, false, Tuning));
	TestTrue(TEXT("storm: lightning"), WeatherRainFxRules::IsLightningActive(1.f, false, Tuning));
	TestFalse(TEXT("storm but suppressed"), WeatherRainFxRules::IsLightningActive(1.f, true, Tuning));

	// Deterministic schedule within the interval, varying by seed and strike.
	bool bVaries = false;
	float First = -1.f;
	for (int32 Index = 0; Index < 20; ++Index)
	{
		const float Delay = WeatherRainFxRules::ComputeStrikeDelay(1234, Index, Tuning);
		TestTrue(FString::Printf(TEXT("delay %d in range (%.2f)"), Index, Delay), Delay >= Tuning.LightningIntervalMin && Delay <= Tuning.LightningIntervalMax);
		TestEqual(FString::Printf(TEXT("delay %d deterministic"), Index), Delay, WeatherRainFxRules::ComputeStrikeDelay(1234, Index, Tuning));
		bVaries |= First >= 0.f && !FMath::IsNearlyEqual(First, Delay, 0.01f);
		First = First < 0.f ? Delay : First;
	}
	TestTrue(TEXT("the delays vary"), bVaries);
	TestNotEqual(TEXT("another seed, another schedule"), WeatherRainFxRules::ComputeStrikeDelay(1234, 0, Tuning), WeatherRainFxRules::ComputeStrikeDelay(99, 0, Tuning));

	// The flash: full at the strike, 0 before / after, 2..max pulses, never above 1.
	TestEqual(TEXT("full at the strike"), WeatherRainFxRules::ComputeFlashScale(0.f, 5, 2, Tuning), 1.f, 1e-5f);
	TestEqual(TEXT("dark before"), WeatherRainFxRules::ComputeFlashScale(-0.1f, 5, 2, Tuning), 0.f);
	TestEqual(TEXT("dark after"), WeatherRainFxRules::ComputeFlashScale(Tuning.LightningFlashSeconds + 0.01f, 5, 2, Tuning), 0.f);
	for (int32 Index = 0; Index < 10; ++Index)
	{
		const int32 Pulses = WeatherRainFxRules::GetPulseCount(77, Index, Tuning);
		TestTrue(FString::Printf(TEXT("pulses %d in 2..max"), Pulses), Pulses >= 2 && Pulses <= Tuning.LightningMaxPulses);
		const float Slot = Tuning.LightningFlashSeconds / Pulses;
		const float Second = WeatherRainFxRules::ComputeFlashScale(Slot + 1e-4f, 77, Index, Tuning);
		TestTrue(FString::Printf(TEXT("the second pulse flashes, weaker (%.2f)"), Second), Second > 0.3f && Second < 1.f);
		for (float T = 0.f; T < Tuning.LightningFlashSeconds; T += 0.01f)
		{
			const float Scale = WeatherRainFxRules::ComputeFlashScale(T, 77, Index, Tuning);
			if (Scale < 0.f || Scale > 1.f)
			{
				AddError(FString::Printf(TEXT("flash scale %.3f out of 0..1 at %.2f s"), Scale, T));
				break;
			}
		}
	}
	FWeatherRainFxTuning Off;
	Off.bLightning = false;
	TestFalse(TEXT("DA switch off"), WeatherRainFxRules::IsLightningActive(1.f, false, Off));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnvironmentRainFxWetOverlayTest, "CodexTactics.Environment.RainFx.WetOverlayGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnvironmentRainFxWetOverlayTest::RunTest(const FString&)
{
	TestFalse(TEXT("dry: no wet overlay pass"), SnowOverlayRules::ShouldShowWetOverlay(0.f, 0.f));
	TestFalse(TEXT("a trace: none"), SnowOverlayRules::ShouldShowWetOverlay(0.004f, 0.004f));
	TestTrue(TEXT("wet ground"), SnowOverlayRules::ShouldShowWetOverlay(0.2f, 0.f));
	TestTrue(TEXT("puddles left on a drying ground"), SnowOverlayRules::ShouldShowWetOverlay(0.f, 0.3f));
	TestFalse(TEXT("negative input"), SnowOverlayRules::ShouldShowWetOverlay(-1.f, -1.f));
	TestEqual(TEXT("DA_RainFX default path"), FString(URainFxSettings::DefaultAssetPath), FString(TEXT("/Game/Environment/Weather/Rain/DA_RainFX.DA_RainFX")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
