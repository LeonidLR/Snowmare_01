#pragma once

#include "CoreMinimal.h"
#include "Environment/WeatherVisualTypes.h"

struct FRuntimeFloatCurve;

/**
 * Pure logic of the environment VISUALS (phase 3, UE-only; docs/systems/environment-weather.md §10). No world access:
 * AWeatherPresenterActor calls these with the UEnvironmentSubsystem state and applies the result to the map's lights.
 * Hours are game hours in [0, 24); angles are degrees.
 */
namespace EnvironmentVisualRules
{
	/** The C++ fallback preset of Weather (used when /Game/Environment/Weather/Presets/DA_Weather_* is missing; the assets start from these). */
	CODEXTACTICS_API FWeatherVisualPreset GetDefaultPreset(EWeatherState Weather);

	/** A -> B by Alpha (clamped 0..1): numbers and colours lerp, bVolumetricFog switches at Alpha 0.5. */
	CODEXTACTICS_API FWeatherVisualPreset LerpPresets(const FWeatherVisualPreset& A, const FWeatherVisualPreset& B, float Alpha);

	/** Wind 0..1 in force: From.WindStrength lerped toward To.WindStrength by Blend (clamped; the same blend LerpPresets uses). */
	CODEXTACTICS_API float GetBlendedWindStrength(const FWeatherVisualPreset& From, const FWeatherVisualPreset& To, float Blend);

	/** Curve value at Hour (wrapped into [0, 24)); Default when the curve has no keys. */
	CODEXTACTICS_API float EvalHourCurve(const FRuntimeFloatCurve& Curve, float Hour, float Default = 0.f);

	/**
	 * Sun elevation above the horizon: SunMaxElevation x sin(pi t) over the day (t = 0 at DawnHour, 1 at DuskHour - the old
	 * BP_DynamivSky MapRangeUnclamped arc), -SunNightDepth x sin(pi n) through the night (n = 0 at dusk, 1 at the next dawn).
	 */
	CODEXTACTICS_API float GetSunElevation(const FEnvironmentDayCurves& Curves, float Hour);

	/** Sun yaw: SunYawAtDawn turning SunYawSweep degrees until dusk, the rest of the circle through the night. */
	CODEXTACTICS_API float GetSunYaw(const FEnvironmentDayCurves& Curves, float Hour);

	/** Moon elevation: MoonMin -> MoonMax -> MoonMin over the night (kept high so it lights the squad), below the horizon by day. */
	CODEXTACTICS_API float GetMoonElevation(const FEnvironmentDayCurves& Curves, float Hour);

	CODEXTACTICS_API float GetMoonYaw(const FEnvironmentDayCurves& Curves, float Hour);

	/** 0 = day .. 1 = full night: 1 - SunIntensityByHour / NightSunThreshold, clamped. */
	CODEXTACTICS_API float GetNightAmount(const FEnvironmentDayCurves& Curves, float Hour);

	/** Hour curves x the preset blended From -> To by Blend: everything the presenter applies now. */
	CODEXTACTICS_API FWeatherVisualFrame ComputeFrame(const FEnvironmentDayCurves& Curves, const FWeatherVisualPreset& From, const FWeatherVisualPreset& To,
		EWeatherState Weather, EWeatherState NextWeather, float Blend, float Hour);
}
