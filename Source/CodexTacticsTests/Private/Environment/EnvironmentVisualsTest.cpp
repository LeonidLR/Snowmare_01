#include "Misc/AutomationTest.h"
#include "Data/LevelJsonRules.h"
#include "Dom/JsonObject.h"
#include "Environment/EnvironmentVisualRules.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

// Environment phase 3, the visual presenter's pure logic (docs/systems/environment-weather.md §11): the sun / moon arc by hour, the
// preset blend, the built-in day curves (monotonic dawn / dusk, a readable night) and the level JSON "visuals" switch.

namespace
{
	const EWeatherState VisualsWeathers[] = { EWeatherState::Clear, EWeatherState::Overcast, EWeatherState::Snowfall, EWeatherState::Blizzard };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnvironmentVisualsSunArcTest, "CodexTactics.Environment.Visuals.SunArc",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnvironmentVisualsSunArcTest::RunTest(const FString&)
{
	const FEnvironmentDayCurves Curves;
	TestEqual(TEXT("sun on the horizon at dawn"), EnvironmentVisualRules::GetSunElevation(Curves, Curves.DawnHour), 0.f, 0.01f);
	TestEqual(TEXT("sun on the horizon at dusk"), EnvironmentVisualRules::GetSunElevation(Curves, Curves.DuskHour), 0.f, 0.01f);
	TestEqual(TEXT("sun at its highest at midday"), EnvironmentVisualRules::GetSunElevation(Curves, 12.f), Curves.SunMaxElevation, 0.01f);
	for (float Hour = 7.f; Hour <= 17.f; Hour += 1.f)
	{
		TestTrue(*FString::Printf(TEXT("sun above the horizon at %.0f"), Hour), EnvironmentVisualRules::GetSunElevation(Curves, Hour) > 0.f);
	}
	for (const float Hour : { 19.f, 21.f, 23.f, 0.f, 2.f, 4.f, 5.5f })
	{
		TestTrue(*FString::Printf(TEXT("sun below the horizon at %.1f"), Hour), EnvironmentVisualRules::GetSunElevation(Curves, Hour) < 0.f);
		TestTrue(*FString::Printf(TEXT("moon above the horizon at %.1f"), Hour),
			EnvironmentVisualRules::GetMoonElevation(Curves, Hour) >= Curves.MoonMinElevation - 0.01f);
	}
	TestTrue(TEXT("the sun is deepest in the middle of the night"),
		EnvironmentVisualRules::GetSunElevation(Curves, 23.5f) <= EnvironmentVisualRules::GetSunElevation(Curves, 20.f));
	TestTrue(TEXT("moon below the horizon at midday"), EnvironmentVisualRules::GetMoonElevation(Curves, 12.f) < 0.f);
	// Morning rise / evening fall, wrap-safe hours.
	TestTrue(TEXT("the sun rises through the morning"),
		EnvironmentVisualRules::GetSunElevation(Curves, 8.f) < EnvironmentVisualRules::GetSunElevation(Curves, 10.f));
	TestTrue(TEXT("the sun sinks through the afternoon"),
		EnvironmentVisualRules::GetSunElevation(Curves, 14.f) > EnvironmentVisualRules::GetSunElevation(Curves, 16.f));
	TestEqual(TEXT("hour 24 == hour 0"), EnvironmentVisualRules::GetSunElevation(Curves, 24.f), EnvironmentVisualRules::GetSunElevation(Curves, 0.f), 0.001f);
	TestEqual(TEXT("hour -1 == hour 23"), EnvironmentVisualRules::GetSunElevation(Curves, -1.f), EnvironmentVisualRules::GetSunElevation(Curves, 23.f), 0.001f);
	// The yaw turns from dawn to dusk by the sweep.
	TestEqual(TEXT("yaw at dawn"), EnvironmentVisualRules::GetSunYaw(Curves, Curves.DawnHour), static_cast<float>(FRotator::NormalizeAxis(Curves.SunYawAtDawn)), 0.01f);
	TestEqual(TEXT("yaw at dusk"), EnvironmentVisualRules::GetSunYaw(Curves, Curves.DuskHour),
		static_cast<float>(FRotator::NormalizeAxis(Curves.SunYawAtDawn + Curves.SunYawSweep)), 0.01f);

	// The frame: day -> sun visible, moon hidden; night -> the reverse.
	const FWeatherVisualPreset Clear = EnvironmentVisualRules::GetDefaultPreset(EWeatherState::Clear);
	const FWeatherVisualFrame Noon = EnvironmentVisualRules::ComputeFrame(Curves, Clear, Clear, EWeatherState::Clear, EWeatherState::Overcast, 0.f, 12.f);
	TestTrue(TEXT("noon: sun visible"), Noon.bSunVisible && Noon.SunIntensityScale > 0.5f);
	TestFalse(TEXT("noon: moon hidden"), Noon.bMoonVisible);
	TestEqual(TEXT("noon: no night grade"), Noon.NightAmount, 0.f);
	const FWeatherVisualFrame Night = EnvironmentVisualRules::ComputeFrame(Curves, Clear, Clear, EWeatherState::Clear, EWeatherState::Overcast, 0.f, 23.f);
	TestFalse(TEXT("23:00: sun hidden"), Night.bSunVisible);
	TestTrue(TEXT("23:00: moon visible"), Night.bMoonVisible && Night.MoonIntensityScale > 0.f);
	TestEqual(TEXT("23:00: full night grade"), Night.NightAmount, 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnvironmentVisualsPresetLerpTest, "CodexTactics.Environment.Visuals.PresetLerp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnvironmentVisualsPresetLerpTest::RunTest(const FString&)
{
	const FWeatherVisualPreset A = EnvironmentVisualRules::GetDefaultPreset(EWeatherState::Overcast);
	const FWeatherVisualPreset B = EnvironmentVisualRules::GetDefaultPreset(EWeatherState::Snowfall);
	const FWeatherVisualPreset Start = EnvironmentVisualRules::LerpPresets(A, B, 0.f);
	const FWeatherVisualPreset End = EnvironmentVisualRules::LerpPresets(A, B, 1.f);
	const FWeatherVisualPreset Mid = EnvironmentVisualRules::LerpPresets(A, B, 0.5f);
	TestEqual(TEXT("alpha 0 = A fog"), Start.FogDensity, A.FogDensity);
	TestEqual(TEXT("alpha 1 = B fog"), End.FogDensity, B.FogDensity);
	TestEqual(TEXT("alpha 0.5 = half fog"), Mid.FogDensity, (A.FogDensity + B.FogDensity) * 0.5f, 1.e-5f);
	TestEqual(TEXT("alpha 0.5 = half sun"), Mid.SunIntensityScale, (A.SunIntensityScale + B.SunIntensityScale) * 0.5f, 1.e-5f);
	TestTrue(TEXT("alpha 0.5 = half fog colour"), Mid.FogColor.Equals((A.FogColor + B.FogColor) * 0.5f, 1.e-5f));
	TestEqual(TEXT("alpha 0.5 = half exposure"), Mid.ExposureBias, (A.ExposureBias + B.ExposureBias) * 0.5f, 1.e-5f);
	TestEqual(TEXT("alpha clamps below 0"), EnvironmentVisualRules::LerpPresets(A, B, -3.f).FogDensity, A.FogDensity);
	TestEqual(TEXT("alpha clamps above 1"), EnvironmentVisualRules::LerpPresets(A, B, 7.f).FogDensity, B.FogDensity);
	TestFalse(TEXT("volumetric fog switches at half: off below"), EnvironmentVisualRules::LerpPresets(A, B, 0.49f).bVolumetricFog);
	TestTrue(TEXT("volumetric fog switches at half: on from 0.5"), EnvironmentVisualRules::LerpPresets(A, B, 0.5f).bVolumetricFog);

	// The default presets climb the ladder: denser fog, weaker sun, greyer picture.
	for (int32 Index = 1; Index < 4; ++Index)
	{
		const FWeatherVisualPreset Lower = EnvironmentVisualRules::GetDefaultPreset(VisualsWeathers[Index - 1]);
		const FWeatherVisualPreset Upper = EnvironmentVisualRules::GetDefaultPreset(VisualsWeathers[Index]);
		TestTrue(*FString::Printf(TEXT("fog denser on rung %d"), Index), Upper.FogDensity > Lower.FogDensity);
		TestTrue(*FString::Printf(TEXT("sun weaker on rung %d"), Index), Upper.SunIntensityScale < Lower.SunIntensityScale);
		TestTrue(*FString::Printf(TEXT("saturation lower on rung %d"), Index), Upper.Saturation < Lower.Saturation);
	}
	TestTrue(TEXT("Overcast: sky light up vs Clear"), EnvironmentVisualRules::GetDefaultPreset(EWeatherState::Overcast).SkyLightIntensityScale
		> EnvironmentVisualRules::GetDefaultPreset(EWeatherState::Clear).SkyLightIntensityScale);

	// The frame blends by the subsystem's transition blend.
	const FEnvironmentDayCurves Curves;
	const FWeatherVisualFrame Frame = EnvironmentVisualRules::ComputeFrame(Curves, A, B, EWeatherState::Overcast, EWeatherState::Snowfall, 0.25f, 12.f);
	TestEqual(TEXT("frame preset = lerp by the blend"), Frame.Preset.FogDensity, FMath::Lerp(A.FogDensity, B.FogDensity, 0.25f), 1.e-5f);
	TestEqual(TEXT("frame sun = noon curve x blended preset"), Frame.SunIntensityScale, 1.f * FMath::Lerp(A.SunIntensityScale, B.SunIntensityScale, 0.25f), 1.e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnvironmentVisualsCurveDefaultsTest, "CodexTactics.Environment.Visuals.CurveDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnvironmentVisualsCurveDefaultsTest::RunTest(const FString&)
{
	const FEnvironmentDayCurves Curves;
	auto Sun = [&Curves](float Hour) { return EnvironmentVisualRules::EvalHourCurve(Curves.SunIntensityByHour, Hour, -1.f); };
	auto Moon = [&Curves](float Hour) { return EnvironmentVisualRules::EvalHourCurve(Curves.MoonIntensityByHour, Hour, -1.f); };
	TestTrue(TEXT("curves have keys"), Sun(12.f) >= 0.f && Moon(23.f) >= 0.f);

	// Monotonic dawn (dawn -> noon rises) and dusk (noon -> dusk falls), sampled every 6 game minutes.
	bool bDawnMonotonic = true;
	bool bDuskMonotonic = true;
	for (float Hour = Curves.DawnHour; Hour < 12.f; Hour += 0.1f)
	{
		bDawnMonotonic &= Sun(Hour + 0.1f) >= Sun(Hour) - 1.e-5f;
		bDawnMonotonic &= EnvironmentVisualRules::GetNightAmount(Curves, Hour + 0.1f) <= EnvironmentVisualRules::GetNightAmount(Curves, Hour) + 1.e-5f;
	}
	for (float Hour = 12.f; Hour < Curves.DuskHour; Hour += 0.1f)
	{
		bDuskMonotonic &= Sun(Hour + 0.1f) <= Sun(Hour) + 1.e-5f;
		bDuskMonotonic &= EnvironmentVisualRules::GetNightAmount(Curves, Hour + 0.1f) >= EnvironmentVisualRules::GetNightAmount(Curves, Hour) - 1.e-5f;
	}
	TestTrue(TEXT("sun intensity / night amount monotonic through the dawn"), bDawnMonotonic);
	TestTrue(TEXT("sun intensity / night amount monotonic through the dusk"), bDuskMonotonic);
	TestEqual(TEXT("sun curve full at noon"), Sun(12.f), 1.f, 1.e-4f);
	TestEqual(TEXT("no sun at 23:00"), Sun(23.f), 0.f, 1.e-4f);
	TestEqual(TEXT("no moon at noon"), Moon(12.f), 0.f, 1.e-4f);

	// Readable night: the Demo 01 tutorial is locked at 23:00 Snowfall - moonlight, raised sky light, a fog that does not glow.
	const FWeatherVisualPreset Snowfall = EnvironmentVisualRules::GetDefaultPreset(EWeatherState::Snowfall);
	const FWeatherVisualFrame Night = EnvironmentVisualRules::ComputeFrame(Curves, Snowfall, Snowfall, EWeatherState::Snowfall, EWeatherState::Snowfall, 0.f, 23.f);
	const FWeatherVisualFrame Noon = EnvironmentVisualRules::ComputeFrame(Curves, Snowfall, Snowfall, EWeatherState::Snowfall, EWeatherState::Snowfall, 0.f, 12.f);
	TestTrue(TEXT("night floor: moon at 23:00 >= 0.2 x sun (curve)"), Moon(23.f) >= 0.2f);
	TestTrue(TEXT("night floor: Snowfall moon x sun > 0.05"), Night.MoonIntensityScale > 0.05f && Night.bMoonVisible);
	TestTrue(TEXT("night floor: sky light raised at night"), Night.SkyLightIntensityScale >= Noon.SkyLightIntensityScale);
	TestTrue(TEXT("night: fog darker than by day but not black"), Night.FogBrightness < Noon.FogBrightness * 0.5f && Night.FogBrightness > 0.f);
	TestTrue(TEXT("night: exposure not crushed (>= -1.5 EV vs day)"), Night.ExposureBias >= Noon.ExposureBias - 1.5f);
	TestTrue(TEXT("night: colour grade keeps some saturation"), Night.Saturation > 0.3f);
	for (const EWeatherState Weather : VisualsWeathers)
	{
		const FWeatherVisualPreset P = EnvironmentVisualRules::GetDefaultPreset(Weather);
		const FWeatherVisualFrame F = EnvironmentVisualRules::ComputeFrame(Curves, P, P, Weather, Weather, 0.f, 23.f);
		TestTrue(*FString::Printf(TEXT("moonlit at 23:00 in every weather (%d)"), static_cast<int32>(Weather)), F.bMoonVisible && F.MoonIntensityScale > 0.05f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnvironmentVisualsLevelJsonTest, "CodexTactics.Environment.Visuals.LevelJson",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnvironmentVisualsLevelJsonTest::RunTest(const FString&)
{
	auto Parse = [](const TCHAR* Text)
	{
		TSharedPtr<FJsonObject> Root;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
		return Root.IsValid() ? LevelJsonRules::ParseEnvironmentSettings(*Root) : FEnvironmentLevelSettings();
	};
	TestTrue(TEXT("absent -> visuals on"), Parse(TEXT("{}")).bVisuals);
	TestFalse(TEXT("\"visuals\": false"), Parse(TEXT(R"({"visuals": false})")).bVisuals);
	TestTrue(TEXT("\"visuals\": true"), Parse(TEXT(R"({"visuals": true, "locked": true})")).bVisuals);

	FLevelCombatConfig Config;
	FString Error;
	TestTrue(TEXT("level parses"), LevelJsonRules::ParseLevel(TEXT(R"({"level_id": "v", "environment": {"start_hour": 12, "visuals": false}})"),
		TEXT("v"), Config, Error));
	TestFalse(TEXT("level: visuals off"), Config.Environment.bVisuals);
	TestTrue(TEXT("level: other keys still read"), Config.Environment.bHasStartHour);
	FLevelCombatConfig Plain;
	TestTrue(TEXT("plain level parses"), LevelJsonRules::ParseLevel(TEXT(R"({"level_id": "p"})"), TEXT("p"), Plain, Error));
	TestTrue(TEXT("plain level: visuals on"), Plain.Environment.bVisuals);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
