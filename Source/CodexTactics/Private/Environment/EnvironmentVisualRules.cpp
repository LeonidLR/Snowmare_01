#include "Environment/EnvironmentVisualRules.h"

#include "Curves/CurveFloat.h"

namespace EnvironmentVisualRulesLocal
{
	float WrapHour(float Hour)
	{
		Hour = FMath::Fmod(Hour, 24.f);
		return Hour < 0.f ? Hour + 24.f : Hour;
	}

	/** Linear hour curve from (hour, value) pairs. */
	void FillCurve(FRuntimeFloatCurve& Curve, std::initializer_list<TPair<float, float>> Keys)
	{
		FRichCurve& Rich = *Curve.GetRichCurve();
		Rich.Reset();
		for (const TPair<float, float>& Key : Keys)
		{
			const FKeyHandle Handle = Rich.AddKey(Key.Key, Key.Value);
			Rich.SetKeyInterpMode(Handle, RCIM_Linear);
		}
	}

	/** Fraction through the day [0, 1] (bDay) or through the night [0, 1] (otherwise). */
	float DayOrNightFraction(const FEnvironmentDayCurves& Curves, float Hour, bool& bOutDay)
	{
		const float Dawn = WrapHour(Curves.DawnHour);
		const float Dusk = WrapHour(Curves.DuskHour);
		float DayLength = Dusk - Dawn;
		DayLength = DayLength <= 0.f ? DayLength + 24.f : DayLength;
		const float SinceDawn = WrapHour(WrapHour(Hour) - Dawn);
		bOutDay = SinceDawn <= DayLength;
		if (bOutDay)
		{
			return DayLength > 0.f ? SinceDawn / DayLength : 0.f;
		}
		const float NightLength = FMath::Max(24.f - DayLength, KINDA_SMALL_NUMBER);
		return FMath::Clamp((SinceDawn - DayLength) / NightLength, 0.f, 1.f);
	}

	FLinearColor LerpColor(const FLinearColor& A, const FLinearColor& B, float Alpha)
	{
		return A + (B - A) * Alpha;
	}
}

FEnvironmentDayCurves::FEnvironmentDayCurves()
{
	// Relative to the authored sun: 0 below the horizon, a soft ramp after dawn / before dusk, full at noon.
	EnvironmentVisualRulesLocal::FillCurve(SunIntensityByHour, { {0.f, 0.f}, {6.f, 0.f}, {6.5f, 0.2f}, {7.5f, 0.55f}, {9.f, 0.85f}, {12.f, 1.f}, {15.f, 0.85f},
		{16.5f, 0.55f}, {17.5f, 0.2f}, {18.f, 0.f}, {24.f, 0.f} });
	// Arctic: only a slightly warm low sun (the atmosphere already reddens it), neutral midday (the presets add their own offset: Clear is cold).
	EnvironmentVisualRulesLocal::FillCurve(SunTemperatureByHour, { {0.f, 5200.f}, {6.f, 5000.f}, {7.f, 6000.f}, {9.f, 6200.f}, {12.f, 6400.f}, {15.f, 6200.f},
		{17.f, 6000.f}, {18.f, 5000.f}, {24.f, 5200.f} });
	// Relative to the authored SUN: a strong (game) moon so the squad stays readable; 0 exactly from dawn to dusk.
	EnvironmentVisualRulesLocal::FillCurve(MoonIntensityByHour, { {0.f, 0.22f}, {4.5f, 0.2f}, {5.75f, 0.04f}, {6.f, 0.f}, {18.f, 0.f}, {18.25f, 0.04f}, {19.5f, 0.2f},
		{24.f, 0.22f} });
	// The real-time captured night sky is dark: the sky light is raised at night (readable ambient).
	EnvironmentVisualRulesLocal::FillCurve(SkyLightIntensityByHour, { {0.f, 2.2f}, {5.f, 2.2f}, {6.f, 1.6f}, {7.f, 1.1f}, {8.f, 1.f}, {17.f, 1.f}, {18.f, 1.4f},
		{19.f, 2.2f}, {24.f, 2.2f} });
	// The fog colour is constant light: it must fade at night or the fog glows.
	EnvironmentVisualRulesLocal::FillCurve(FogBrightnessByHour, { {0.f, 0.1f}, {5.f, 0.1f}, {6.f, 0.3f}, {7.5f, 0.85f}, {9.f, 1.f}, {16.f, 1.f}, {17.5f, 0.6f},
		{18.5f, 0.2f}, {19.5f, 0.1f}, {24.f, 0.1f} });
	EnvironmentVisualRulesLocal::FillCurve(ExposureBiasByHour, { {0.f, -1.2f}, {5.f, -1.2f}, {7.f, 0.f}, {17.f, 0.f}, {19.f, -1.2f}, {24.f, -1.2f} });
}

FWeatherVisualPreset EnvironmentVisualRules::GetDefaultPreset(EWeatherState Weather)
{
	FWeatherVisualPreset P;
	switch (Weather)
	{
	case EWeatherState::Clear:
		// Hard frost: crisp cold-blue light, sharp shadows, a deep blue sky, low fog.
		P.SunIntensityScale = 1.1f;
		P.SunTint = FLinearColor(0.93f, 0.97f, 1.f);
		P.SunTemperatureOffset = 900.f;
		P.SunSourceAngle = 0.5f;
		P.MoonIntensityScale = 1.15f;
		P.SkyLightIntensityScale = 0.85f;
		P.RayleighScatteringScale = 1.f;
		P.MieScatteringScale = 0.6f;
		P.FogDensity = 0.01f;
		P.FogHeightFalloff = 0.25f;
		P.FogColor = FLinearColor(0.45f, 0.6f, 0.9f);
		P.FogBrightnessScale = 0.8f;
		P.FogStartDistance = 2500.f;
		P.FogMaxOpacity = 0.85f;
		P.bVolumetricFog = false;
		P.VolumetricFogExtinctionScale = 1.f;
		P.ExposureBias = 0.5f;
		P.Saturation = 1.05f;
		P.ColorTint = FLinearColor(0.94f, 0.98f, 1.06f);
		P.SnowIntensity = 0.f;
		P.FrostAmount = 1.f;
		P.WindStrength = 0.15f;
		break;
	case EWeatherState::Overcast:
		// Flat grey: the sun dim and soft, the sky light up, a grey hazy sky.
		P.SunIntensityScale = 0.35f;
		P.SunTint = FLinearColor(0.86f, 0.89f, 0.93f);
		P.SunTemperatureOffset = 1500.f;
		P.SunSourceAngle = 6.f;
		P.MoonIntensityScale = 0.55f;
		P.SkyLightIntensityScale = 1.4f;
		P.RayleighScatteringScale = 0.6f;
		P.MieScatteringScale = 2.5f;
		P.FogDensity = 0.03f;
		P.FogHeightFalloff = 0.2f;
		P.FogColor = FLinearColor(0.56f, 0.59f, 0.63f);
		P.FogBrightnessScale = 1.f;
		P.FogStartDistance = 1500.f;
		P.FogMaxOpacity = 0.95f;
		P.bVolumetricFog = false;
		P.VolumetricFogExtinctionScale = 1.f;
		P.ExposureBias = 0.4f;
		P.Saturation = 0.72f;
		P.ColorTint = FLinearColor(0.95f, 0.96f, 0.98f);
		P.SnowIntensity = 0.f;
		P.FrostAmount = 0.4f;
		P.WindStrength = 0.35f;
		break;
	case EWeatherState::Snowfall:
		// Dense fog, short visibility, soft light.
		P.SunIntensityScale = 0.22f;
		P.SunTint = FLinearColor(0.84f, 0.88f, 0.94f);
		P.SunTemperatureOffset = 1300.f;
		P.SunSourceAngle = 10.f;
		P.MoonIntensityScale = 0.45f;
		P.SkyLightIntensityScale = 1.35f;
		P.RayleighScatteringScale = 0.5f;
		P.MieScatteringScale = 4.f;
		P.FogDensity = 0.06f;
		P.FogHeightFalloff = 0.2f;
		P.FogColor = FLinearColor(0.63f, 0.67f, 0.73f);
		P.FogBrightnessScale = 1.1f;
		P.FogStartDistance = 1800.f;
		P.FogMaxOpacity = 0.95f;
		P.bVolumetricFog = true;
		P.VolumetricFogExtinctionScale = 0.8f;
		P.ExposureBias = 0.35f;
		P.Saturation = 0.62f;
		P.ColorTint = FLinearColor(0.94f, 0.96f, 1.f);
		P.SnowIntensity = 0.6f;
		P.FrostAmount = 0.3f;
		P.WindStrength = 0.45f;
		break;
	case EWeatherState::Blizzard:
	default:
		// Very dense white-grey, low light.
		P.SunIntensityScale = 0.1f;
		P.SunTint = FLinearColor(0.86f, 0.89f, 0.93f);
		P.SunTemperatureOffset = 1800.f;
		P.SunSourceAngle = 15.f;
		P.MoonIntensityScale = 0.3f;
		P.SkyLightIntensityScale = 1.15f;
		P.RayleighScatteringScale = 0.4f;
		P.MieScatteringScale = 8.f;
		P.FogDensity = 0.1f;
		P.FogHeightFalloff = 0.18f;
		P.FogColor = FLinearColor(0.76f, 0.79f, 0.83f);
		P.FogBrightnessScale = 1.2f;
		P.FogStartDistance = 1800.f;
		P.FogMaxOpacity = 0.86f;
		P.bVolumetricFog = false;
		P.VolumetricFogExtinctionScale = 1.f;
		P.ExposureBias = -0.15f;
		P.Saturation = 0.45f;
		P.ColorTint = FLinearColor(0.95f, 0.97f, 1.f);
		P.SnowIntensity = 1.f;
		P.FrostAmount = 0.2f;
		P.WindStrength = 1.f;
		break;
	}
	return P;
}

float EnvironmentVisualRules::GetBlendedWindStrength(const FWeatherVisualPreset& From, const FWeatherVisualPreset& To, float Blend)
{
	return FMath::Clamp(FMath::Lerp(From.WindStrength, To.WindStrength, FMath::Clamp(Blend, 0.f, 1.f)), 0.f, 1.f);
}

FWeatherVisualPreset EnvironmentVisualRules::LerpPresets(const FWeatherVisualPreset& A, const FWeatherVisualPreset& B, float Alpha)
{
	const float T = FMath::Clamp(Alpha, 0.f, 1.f);
	FWeatherVisualPreset P;
	P.SunIntensityScale = FMath::Lerp(A.SunIntensityScale, B.SunIntensityScale, T);
	P.SunTint = EnvironmentVisualRulesLocal::LerpColor(A.SunTint, B.SunTint, T);
	P.SunTemperatureOffset = FMath::Lerp(A.SunTemperatureOffset, B.SunTemperatureOffset, T);
	P.SunSourceAngle = FMath::Lerp(A.SunSourceAngle, B.SunSourceAngle, T);
	P.MoonIntensityScale = FMath::Lerp(A.MoonIntensityScale, B.MoonIntensityScale, T);
	P.SkyLightIntensityScale = FMath::Lerp(A.SkyLightIntensityScale, B.SkyLightIntensityScale, T);
	P.RayleighScatteringScale = FMath::Lerp(A.RayleighScatteringScale, B.RayleighScatteringScale, T);
	P.MieScatteringScale = FMath::Lerp(A.MieScatteringScale, B.MieScatteringScale, T);
	P.FogDensity = FMath::Lerp(A.FogDensity, B.FogDensity, T);
	P.FogHeightFalloff = FMath::Lerp(A.FogHeightFalloff, B.FogHeightFalloff, T);
	P.FogColor = EnvironmentVisualRulesLocal::LerpColor(A.FogColor, B.FogColor, T);
	P.FogBrightnessScale = FMath::Lerp(A.FogBrightnessScale, B.FogBrightnessScale, T);
	P.FogStartDistance = FMath::Lerp(A.FogStartDistance, B.FogStartDistance, T);
	P.FogMaxOpacity = FMath::Lerp(A.FogMaxOpacity, B.FogMaxOpacity, T);
	P.bVolumetricFog = T < 0.5f ? A.bVolumetricFog : B.bVolumetricFog;
	P.VolumetricFogExtinctionScale = FMath::Lerp(A.VolumetricFogExtinctionScale, B.VolumetricFogExtinctionScale, T);
	P.ExposureBias = FMath::Lerp(A.ExposureBias, B.ExposureBias, T);
	P.Saturation = FMath::Lerp(A.Saturation, B.Saturation, T);
	P.ColorTint = EnvironmentVisualRulesLocal::LerpColor(A.ColorTint, B.ColorTint, T);
	P.SnowIntensity = FMath::Lerp(A.SnowIntensity, B.SnowIntensity, T);
	P.FrostAmount = FMath::Lerp(A.FrostAmount, B.FrostAmount, T);
	P.WindStrength = FMath::Lerp(A.WindStrength, B.WindStrength, T);
	return P;
}

float EnvironmentVisualRules::EvalHourCurve(const FRuntimeFloatCurve& Curve, float Hour, float Default)
{
	const FRichCurve* Rich = Curve.GetRichCurveConst();
	if (!Rich || Rich->GetNumKeys() == 0)
	{
		return Default;
	}
	return Rich->Eval(EnvironmentVisualRulesLocal::WrapHour(Hour), Default);
}

float EnvironmentVisualRules::GetSunElevation(const FEnvironmentDayCurves& Curves, float Hour)
{
	bool bDay = false;
	const float Fraction = EnvironmentVisualRulesLocal::DayOrNightFraction(Curves, Hour, bDay);
	return bDay ? Curves.SunMaxElevation * FMath::Sin(PI * Fraction) : -Curves.SunNightDepth * FMath::Sin(PI * Fraction);
}

float EnvironmentVisualRules::GetSunYaw(const FEnvironmentDayCurves& Curves, float Hour)
{
	bool bDay = false;
	const float Fraction = EnvironmentVisualRulesLocal::DayOrNightFraction(Curves, Hour, bDay);
	const float Yaw = bDay ? Curves.SunYawAtDawn + Curves.SunYawSweep * Fraction
		: Curves.SunYawAtDawn + Curves.SunYawSweep + (360.f - Curves.SunYawSweep) * Fraction;
	return FRotator::NormalizeAxis(Yaw);
}

float EnvironmentVisualRules::GetMoonElevation(const FEnvironmentDayCurves& Curves, float Hour)
{
	bool bDay = false;
	const float Fraction = EnvironmentVisualRulesLocal::DayOrNightFraction(Curves, Hour, bDay);
	const float Arc = Curves.MoonMinElevation + (Curves.MoonMaxElevation - Curves.MoonMinElevation) * FMath::Sin(PI * Fraction);
	return bDay ? -Arc : Arc;
}

float EnvironmentVisualRules::GetMoonYaw(const FEnvironmentDayCurves& Curves, float Hour)
{
	return FRotator::NormalizeAxis(GetSunYaw(Curves, Hour) + Curves.MoonYawOffset);
}

float EnvironmentVisualRules::GetNightAmount(const FEnvironmentDayCurves& Curves, float Hour)
{
	const float Sun = EvalHourCurve(Curves.SunIntensityByHour, Hour, 1.f);
	return 1.f - FMath::Clamp(Sun / FMath::Max(Curves.NightSunThreshold, KINDA_SMALL_NUMBER), 0.f, 1.f);
}

FWeatherVisualFrame EnvironmentVisualRules::ComputeFrame(const FEnvironmentDayCurves& Curves, const FWeatherVisualPreset& From,
	const FWeatherVisualPreset& To, EWeatherState Weather, EWeatherState NextWeather, float Blend, float Hour)
{
	FWeatherVisualFrame Frame;
	Frame.Hour = EnvironmentVisualRulesLocal::WrapHour(Hour);
	Frame.Weather = Weather;
	Frame.NextWeather = NextWeather;
	Frame.Blend = FMath::Clamp(Blend, 0.f, 1.f);
	Frame.Preset = LerpPresets(From, To, Frame.Blend);
	const FWeatherVisualPreset& P = Frame.Preset;
	Frame.NightAmount = GetNightAmount(Curves, Frame.Hour);

	Frame.SunElevation = GetSunElevation(Curves, Frame.Hour);
	Frame.SunYaw = GetSunYaw(Curves, Frame.Hour);
	Frame.SunIntensityScale = FMath::Max(0.f, EvalHourCurve(Curves.SunIntensityByHour, Frame.Hour, 1.f)) * P.SunIntensityScale;
	Frame.SunTemperature = FMath::Clamp(EvalHourCurve(Curves.SunTemperatureByHour, Frame.Hour, 6500.f) + P.SunTemperatureOffset, 1700.f, 12000.f);
	Frame.bSunVisible = Frame.SunElevation > 0.f && Frame.SunIntensityScale > 0.001f;

	Frame.MoonElevation = GetMoonElevation(Curves, Frame.Hour);
	Frame.MoonYaw = GetMoonYaw(Curves, Frame.Hour);
	Frame.MoonIntensityScale = FMath::Max(0.f, EvalHourCurve(Curves.MoonIntensityByHour, Frame.Hour, 0.f)) * P.MoonIntensityScale;
	Frame.bMoonVisible = Frame.MoonElevation > 0.f && Frame.MoonIntensityScale > 0.001f;

	Frame.SkyLightIntensityScale = FMath::Max(0.f, EvalHourCurve(Curves.SkyLightIntensityByHour, Frame.Hour, 1.f)) * P.SkyLightIntensityScale;
	Frame.FogBrightness = FMath::Max(0.f, EvalHourCurve(Curves.FogBrightnessByHour, Frame.Hour, 1.f)) * P.FogBrightnessScale;
	Frame.ExposureBias = P.ExposureBias + EvalHourCurve(Curves.ExposureBiasByHour, Frame.Hour, 0.f);
	Frame.Saturation = P.Saturation * FMath::Lerp(1.f, Curves.NightSaturation, Frame.NightAmount);
	Frame.ColorTint = P.ColorTint * EnvironmentVisualRulesLocal::LerpColor(FLinearColor::White, Curves.NightTint, Frame.NightAmount);
	Frame.ColorTint.A = 1.f;
	return Frame;
}
