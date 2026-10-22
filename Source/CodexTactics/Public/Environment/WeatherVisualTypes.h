#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Environment/EnvironmentTypes.h"
#include "WeatherVisualTypes.generated.h"

// Environment phase 3: the VISUAL side of the day cycle + weather (UE-only; design docs/systems/environment-weather.md §5 / §10).
// Reference (design only, no code port): the user's WeatherSystem_01 BP_DynamivSky (sun rotation by hour between DawnTime / DuskTime via
// MapRangeUnclamped, moon light at night, per-weather light / sky / fog presets). Pure data; EnvironmentVisualRules computes with it,
// AWeatherPresenterActor applies it. Nothing here changes gameplay.

/**
 * The visual targets of one weather state (UWeatherPresetAsset DA_Weather_*, C++ fallbacks in EnvironmentVisualRules::GetDefaultPreset).
 * Intensities are MULTIPLIERS of the values authored in the map (so every map keeps its exposure balance); the fog density / falloff /
 * start distance and the post-process values are absolute. Volumetric clouds are not part of a preset: the tactical camera never sees
 * them, the presenter switches the map's AVolumetricCloud off (user decision 2026-10-22).
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FWeatherVisualPreset
{
	GENERATED_BODY()

	// --- Sun / moon ---

	/** x the map's authored sun intensity x the hour curve. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun", meta = (ClampMin = "0"))
	float SunIntensityScale = 1.f;

	/** x the map's authored sun colour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun")
	FLinearColor SunTint = FLinearColor::White;

	/** Kelvin added to the hour curve (FEnvironmentDayCurves::SunTemperatureByHour); colder look = higher. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun")
	float SunTemperatureOffset = 0.f;

	/** Sun disc angle in degrees (soft shadows under cloud). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun", meta = (ClampMin = "0", ClampMax = "30"))
	float SunSourceAngle = 0.5357f;

	/** x the moon curve (FEnvironmentDayCurves::MoonIntensityByHour). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun", meta = (ClampMin = "0"))
	float MoonIntensityScale = 1.f;

	// --- Sky ---

	/** x the map's authored sky light intensity x the hour curve. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky", meta = (ClampMin = "0"))
	float SkyLightIntensityScale = 1.f;

	/** x the authored sky atmosphere Rayleigh scattering scale (blue sky; lower = greyer). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky", meta = (ClampMin = "0"))
	float RayleighScatteringScale = 1.f;

	/** x the authored sky atmosphere Mie scattering scale (haze; higher = whiter, flatter). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky", meta = (ClampMin = "0"))
	float MieScatteringScale = 1.f;

	// --- Height fog ---

	/** ExponentialHeightFog density (absolute). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog", meta = (ClampMin = "0"))
	float FogDensity = 0.02f;

	/** ExponentialHeightFog height falloff (absolute; lower = the fog reaches higher). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog", meta = (ClampMin = "0.001"))
	float FogHeightFalloff = 0.2f;

	/** Inscattering colour (hue; its brightness comes from FogBrightnessScale x the authored fog luminance x the hour curve). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog")
	FLinearColor FogColor = FLinearColor(0.45f, 0.6f, 0.85f);

	/** x the brightest channel of the map's authored fog inscattering luminance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog", meta = (ClampMin = "0"))
	float FogBrightnessScale = 1.f;

	/** Fog start distance, cm (absolute). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog", meta = (ClampMin = "0"))
	float FogStartDistance = 0.f;

	/** Fog max opacity (absolute, 0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog", meta = (ClampMin = "0", ClampMax = "1"))
	float FogMaxOpacity = 1.f;

	/** Volumetric fog on (lit by the sun / moon; snow in the air). A blend switches it at the half-way point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog")
	bool bVolumetricFog = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog", meta = (ClampMin = "0"))
	float VolumetricFogExtinctionScale = 1.f;

	// --- Post process (the presenter's own unbound post-process, or added to an adopted unbound volume) ---

	/** EV added to the auto-exposure bias. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PostProcess")
	float ExposureBias = 0.f;

	/** Overall colour saturation (1 = as authored). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PostProcess", meta = (ClampMin = "0"))
	float Saturation = 1.f;

	/** Scene colour tint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PostProcess")
	FLinearColor ColorTint = FLinearColor::White;

	// --- Phase 4 data (Niagara User parameters / MPC_Environment; not written yet) ---

	/** 0..1 falling snow strength (User.WeatherIntensity / MPC IsSnowing). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phase4", meta = (ClampMin = "0", ClampMax = "1"))
	float SnowIntensity = 0.f;

	/** 0..1 frost on surfaces (MPC FrostAmount). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phase4", meta = (ClampMin = "0", ClampMax = "1"))
	float FrostAmount = 0.f;

	/** 0..1 wind (MPC WindStrength / User.WindStrength). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phase4", meta = (ClampMin = "0", ClampMax = "1"))
	float WindStrength = 0.f;
};

/**
 * The day curves of the presenter (the old HandleSunMoonRotation / HandleNightSettings moved to data). Hour keys 0..24, linear, the
 * defaults are built in the constructor (night stays READABLE for a tactical game: moonlit blue, not black). Relative values multiply
 * the map's authored values; FEnvironmentDayCurves() is what the tests check (EnvironmentVisualRules).
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FEnvironmentDayCurves
{
	GENERATED_BODY()

	FEnvironmentDayCurves();

	/** Sunrise: the sun crosses the horizon going up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun", meta = (ClampMin = "0", ClampMax = "24"))
	float DawnHour = 6.f;

	/** Sunset: the sun crosses the horizon going down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun", meta = (ClampMin = "0", ClampMax = "24"))
	float DuskHour = 18.f;

	/** Sun elevation at the middle of the day, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun", meta = (ClampMin = "1", ClampMax = "90"))
	float SunMaxElevation = 52.f;

	/** How deep the sun sinks at the middle of the night, degrees below the horizon. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun", meta = (ClampMin = "1", ClampMax = "90"))
	float SunNightDepth = 35.f;

	/** Sun yaw at dawn (world degrees) and how far it turns until dusk. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun")
	float SunYawAtDawn = -70.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun")
	float SunYawSweep = 160.f;

	/** Moon elevation at moonrise / moonset (dusk / dawn) and in the middle of the night, degrees (kept high: readable shadows). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moon", meta = (ClampMin = "1", ClampMax = "90"))
	float MoonMinElevation = 28.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moon", meta = (ClampMin = "1", ClampMax = "90"))
	float MoonMaxElevation = 55.f;

	/** Moon yaw relative to the sun's yaw at the same hour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moon")
	float MoonYawOffset = 150.f;

	/** Moonlight colour (cold blue). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moon")
	FLinearColor MoonColor = FLinearColor(0.6f, 0.7f, 1.f);

	/** Sun intensity by hour, x the authored sun intensity (0 at night). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curves")
	FRuntimeFloatCurve SunIntensityByHour;

	/** Sun colour temperature by hour, Kelvin (warm low sun, neutral midday). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curves")
	FRuntimeFloatCurve SunTemperatureByHour;

	/** Moon intensity by hour, x the authored SUN intensity (0 by day). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curves")
	FRuntimeFloatCurve MoonIntensityByHour;

	/** Sky light intensity by hour, x the authored sky light (raised at night: the captured night sky is dark). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curves")
	FRuntimeFloatCurve SkyLightIntensityByHour;

	/** Fog inscattering brightness by hour (the fog colour is not lit by the sun: it must darken at night). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curves")
	FRuntimeFloatCurve FogBrightnessByHour;

	/** EV added to the exposure by hour (night a little darker than auto exposure makes it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curves")
	FRuntimeFloatCurve ExposureBiasByHour;

	/** Colour grade at full night (blended by the night amount): cold tint and lower saturation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Night")
	FLinearColor NightTint = FLinearColor(0.84f, 0.9f, 1.04f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Night", meta = (ClampMin = "0"))
	float NightSaturation = 0.65f;

	/** Sun curve value below which it counts as night (night amount 1 at 0, 0 at this value and above). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Night", meta = (ClampMin = "0.01"))
	float NightSunThreshold = 0.25f;
};

/** Everything the presenter applies at one moment (EnvironmentVisualRules::ComputeFrame): hour curves x the blended preset. */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FWeatherVisualFrame
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Environment")
	float Hour = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Environment")
	EWeatherState Weather = EWeatherState::Clear;

	UPROPERTY(BlueprintReadOnly, Category = "Environment")
	EWeatherState NextWeather = EWeatherState::Clear;

	/** 0 = fully Weather, 1 = fully NextWeather. */
	UPROPERTY(BlueprintReadOnly, Category = "Environment")
	float Blend = 0.f;

	/** 0 = day, 1 = full night. */
	UPROPERTY(BlueprintReadOnly, Category = "Environment")
	float NightAmount = 0.f;

	/** The blended preset in force. */
	UPROPERTY(BlueprintReadOnly, Category = "Environment")
	FWeatherVisualPreset Preset;

	/** Degrees above the horizon (negative = below). The light's pitch is -SunElevation. */
	UPROPERTY(BlueprintReadOnly, Category = "Sun")
	float SunElevation = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Sun")
	float SunYaw = 0.f;

	/** x the authored sun intensity (hour curve x preset). */
	UPROPERTY(BlueprintReadOnly, Category = "Sun")
	float SunIntensityScale = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Sun")
	float SunTemperature = 6500.f;

	UPROPERTY(BlueprintReadOnly, Category = "Sun")
	bool bSunVisible = false;

	UPROPERTY(BlueprintReadOnly, Category = "Moon")
	float MoonElevation = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Moon")
	float MoonYaw = 0.f;

	/** x the authored SUN intensity. */
	UPROPERTY(BlueprintReadOnly, Category = "Moon")
	float MoonIntensityScale = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Moon")
	bool bMoonVisible = false;

	/** x the authored sky light intensity. */
	UPROPERTY(BlueprintReadOnly, Category = "Sky")
	float SkyLightIntensityScale = 1.f;

	/** x the authored fog luminance (brightest channel), applied to Preset.FogColor. */
	UPROPERTY(BlueprintReadOnly, Category = "Fog")
	float FogBrightness = 1.f;

	/** EV added to the base exposure bias (preset + hour). */
	UPROPERTY(BlueprintReadOnly, Category = "PostProcess")
	float ExposureBias = 0.f;

	/** Final saturation / tint (preset x night grade). */
	UPROPERTY(BlueprintReadOnly, Category = "PostProcess")
	float Saturation = 1.f;

	UPROPERTY(BlueprintReadOnly, Category = "PostProcess")
	FLinearColor ColorTint = FLinearColor::White;
};
