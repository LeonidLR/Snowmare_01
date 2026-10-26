#pragma once

#include "CoreMinimal.h"
#include "Environment/WeatherSnowFxRules.h"
#include "WeatherRainFxRules.generated.h"

/**
 * Tuning of the RAIN visuals of a temperate (narrative) level for the tactical camera (docs/systems/environment-weather.md section 18).
 * Visual only. Values in UE units (cm, s). Edited in the data asset DA_RainFX (URainFxSettings, /Game/Environment/Weather/Rain/DA_RainFX);
 * these C++ defaults are its initial values and the fallback when the asset is missing. How hard it rains in GAMEPLAY terms (wetness,
 * puddles, MPC RainIntensity) is environment.json's temperate block; this struct says what the rain LOOKS like on each rung.
 * No Godot reference (UE-only feature).
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FWeatherRainFxTuning
{
	GENERATED_BODY()

	// --- Per weather ---

	/** The look's intensity (0..1) on the Rain rung (the temperate Snowfall rung) and on the Storm rung (Blizzard); blended through a transition. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float RainLookIntensity = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float StormLookIntensity = 1.f;

	// --- Drops (the falling streaks) ---

	/** Drops per second at look intensity 1 over the whole spawn volume; scaled by intensity^SpawnRateExponent. GPU cost: see the docs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drops", meta = (ClampMin = "0", UIMax = "80000"))
	float SpawnRateAtFull = 26000.f;

	/** >1 keeps a light rain sparse and the storm dense (Rain 0.55 -> 0.41 of the storm rate at 1.5). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drops", meta = (ClampMin = "0.1", UIMax = "4"))
	float SpawnRateExponent = 1.5f;

	/**
	 * Streak length (cm), each drop random between min and max (a velocity-aligned sprite). The tactical camera is 15-45 m away and looks down
	 * 35 deg: a real drop's motion blur is 10-20 cm; 35-65 cm streaks read as rain from there without turning into white bars (stills
	 * 2026-10-26: 70-130 cm filled the view like a curtain).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drops", meta = (ClampMin = "1", UIMax = "400", Units = "cm"))
	float DropLengthMin = 35.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drops", meta = (ClampMin = "1", UIMax = "400", Units = "cm"))
	float DropLengthMax = 65.f;

	/**
	 * Streak width (cm): ~1.3 cm per pixel at 26 m and 1080p; the soft core makes the visible line about half of it, so 3-4.5 cm reads as a
	 * 1-2 px line (the material is responsive-AA, so the temporal AA keeps it).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drops", meta = (ClampMin = "0.1", UIMax = "20", Units = "cm"))
	float DropWidthMin = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drops", meta = (ClampMin = "0.1", UIMax = "20", Units = "cm"))
	float DropWidthMax = 4.5f;

	/** The terminal fall speed (cm/s; real rain 600-900): the drops reach it within ~0.3 s (DropAcceleration). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drops", meta = (ClampMin = "10", UIMax = "5000"))
	float FallSpeed = 1100.f;

	/** Downward acceleration until the fall speed (cm/s^2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drops", meta = (ClampMin = "0", UIMax = "20000"))
	float DropAcceleration = 4000.f;

	/** Drop opacity at a trace of rain and at look intensity 1 (each drop random between DropOpacityRandomMin x this and this). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drops", meta = (ClampMin = "0", ClampMax = "4"))
	float OpacityMin = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drops", meta = (ClampMin = "0", ClampMax = "4"))
	float OpacityMax = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drops", meta = (ClampMin = "0", ClampMax = "1"))
	float DropOpacityRandomMin = 0.5f;

	/** Drop colour x brightness (unlit, translucent; a cool grey-blue reads on both the grass and the sky). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drops")
	FLinearColor DropColor = FLinearColor(0.72f, 0.8f, 0.9f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drops", meta = (ClampMin = "0", UIMax = "8"))
	float DropBrightness = 1.1f;

	/** The longest drop life (s): drops die where they hit the depth buffer (ground, roofs) or after this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drops", meta = (ClampMin = "0.1", UIMax = "10", Units = "s"))
	float DropLifetime = 3.f;

	/** Spawn area: a cylinder standing on the ground point the view looks at (moved upwind in a strong wind), radius and height (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drops", meta = (ClampMin = "100", UIMax = "8000", Units = "cm"))
	float SpawnRadius = 2600.f;

	/** From the ground up; keep it above the highest camera (40 m x sin 35 deg = 23 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drops", meta = (ClampMin = "100", UIMax = "6000", Units = "cm"))
	float SpawnHeight = 2600.f;

	/** Near-camera fade (MI_Weather_Raindrop): drops closer than NearFadeStart are invisible, fully visible NearFadeLength further. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drops", meta = (ClampMin = "0", UIMax = "3000", Units = "cm"))
	float NearFadeStart = 700.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drops", meta = (ClampMin = "1", UIMax = "3000", Units = "cm"))
	float NearFadeLength = 1100.f;

	// --- Wind ---

	/** Wind influence: the drops' sideways speed (cm/s) at WindStrength 1 (scaled by wind^2): the storm's slant. 0 = straight down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind", meta = (ClampMin = "0", UIMax = "4000"))
	float DropWindSpeed = 750.f;

	/** The system stands this far UPWIND of the view focus at wind 1 (scaled by wind^2), so slanted rain covers the whole view. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind", meta = (ClampMin = "0", UIMax = "8000", Units = "cm"))
	float UpwindOffsetAtFullWind = 1500.f;

	/** The wind the system receives never drops below this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind", meta = (ClampMin = "0", ClampMax = "1"))
	float WindStrengthFloor = 0.05f;

	// --- Splashes (the drops hitting the ground) ---

	/** Small splash sprites on the ground around the view focus (a ground-plane emitter, not per-drop collision: no CPU traces). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Splashes")
	bool bSplashes = true;

	/** Splashes per second at look intensity 1 (linear in intensity). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Splashes", meta = (ClampMin = "0", UIMax = "20000"))
	float SplashRateAtFull = 3000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Splashes", meta = (ClampMin = "1", UIMax = "300", Units = "cm"))
	float SplashSizeMin = 14.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Splashes", meta = (ClampMin = "1", UIMax = "300", Units = "cm"))
	float SplashSizeMax = 28.f;

	/** The ground disc the splashes appear on (radius around the view focus, cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Splashes", meta = (ClampMin = "100", UIMax = "8000", Units = "cm"))
	float SplashRadius = 2200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Splashes", meta = (ClampMin = "0", ClampMax = "4"))
	float SplashOpacity = 0.45f;

	// --- Puddle splashes (rings / crowns on standing water) ---

	/** Splash rings on the puddles: density = PuddleSplashRateAtFull x look intensity x MPC PuddleAmount (NS_Weather_PuddleSplashes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puddle splashes")
	bool bPuddleSplashes = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puddle splashes", meta = (ClampMin = "0", UIMax = "5000"))
	float PuddleSplashRateAtFull = 700.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puddle splashes", meta = (ClampMin = "1", UIMax = "400", Units = "cm"))
	float PuddleSplashSizeMin = 22.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puddle splashes", meta = (ClampMin = "1", UIMax = "400", Units = "cm"))
	float PuddleSplashSizeMax = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puddle splashes", meta = (ClampMin = "100", UIMax = "8000", Units = "cm"))
	float PuddleSplashRadius = 2000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puddle splashes", meta = (ClampMin = "0", ClampMax = "4"))
	float PuddleSplashOpacity = 0.35f;

	/** Below this MPC PuddleAmount no puddle splash spawns (the ground is wet, the water does not stand yet). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puddle splashes", meta = (ClampMin = "0", ClampMax = "1"))
	float PuddleSplashMinPuddles = 0.05f;

	// --- Screen drops (rain on the lens, post process) ---

	/** Drops on the lens while it rains (user decision 2026-10-26: ON for the tactical camera); environment.json temperate.screen_drops gates it too. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Screen drops")
	bool bScreenDrops = true;

	/** The post-process strength at RainIntensity 1 (x MPC ScreenDrops x RainIntensity): subtle at the tactical camera (0 = off, 1 = the old look). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Screen drops", meta = (ClampMin = "0", ClampMax = "2"))
	float ScreenDropsStrength = 0.3f;

	/** Drop texture tiling across the screen (higher = smaller drops) and the drops' animation speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Screen drops", meta = (ClampMin = "0.1", UIMax = "8"))
	float ScreenDropsTiling = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Screen drops", meta = (ClampMin = "0", UIMax = "4"))
	float ScreenDropsSpeed = 0.3f;

	// --- Wet look ---

	/**
	 * Exposure bias (EV) at Wetness 1, scaled by the MPC Wetness (0 = none): the auto exposure would brighten a soaked, darker world back to
	 * the dry look, so the wet darkening of the ground / objects stays readable (stills 2026-10-26). Temperate levels only.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet look", meta = (ClampMin = "-2", ClampMax = "0", Units = "EV"))
	float WetExposureBias = -0.4f;

	// --- Lightning (Storm only) ---

	/** Lightning flashes at random (deterministic) intervals while the Storm amount is at least LightningMinStorm; off on automated runs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lightning")
	bool bLightning = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lightning", meta = (ClampMin = "0", ClampMax = "1"))
	float LightningMinStorm = 0.5f;

	/** Seconds between two strikes, random between min and max (from the environment's seed; the env RNG itself is never advanced). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lightning", meta = (ClampMin = "0.5", UIMax = "120", Units = "s"))
	float LightningIntervalMin = 7.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lightning", meta = (ClampMin = "0.5", UIMax = "120", Units = "s"))
	float LightningIntervalMax = 20.f;

	/** The flash: a shadowless directional light from high above, peak intensity (lux) and colour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lightning", meta = (ClampMin = "0", UIMax = "100"))
	float LightningFlashLux = 9.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lightning")
	FLinearColor LightningColor = FLinearColor(0.78f, 0.86f, 1.f, 1.f);

	/** One strike's flicker: 2..LightningMaxPulses pulses within LightningFlashSeconds, each weaker than the one before. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lightning", meta = (ClampMin = "0.05", UIMax = "3", Units = "s"))
	float LightningFlashSeconds = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lightning", meta = (ClampMin = "1", ClampMax = "6"))
	int32 LightningMaxPulses = 3;

	// --- Indoors (the snow's rules: docs §12.b) ---

	/** No rain inside Closed / Shelter camera zones and under a roof over the view focus / squad leader (Niagara kill boxes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Indoors")
	bool bNoRainIndoors = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Indoors", meta = (ClampMin = "0", UIMax = "20000", Units = "cm"))
	float IndoorSearchRadius = 6000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Indoors", meta = (ClampMin = "0", UIMax = "200", Units = "cm"))
	float IndoorMarginCm = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Indoors", meta = (ClampMin = "0", UIMax = "1000", Units = "cm"))
	float IndoorBelowCm = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Indoors")
	bool bKillUnderRoofs = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Indoors", meta = (ClampMin = "100", UIMax = "10000", Units = "cm"))
	float RoofTraceHeight = 2500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Indoors", meta = (ClampMin = "100", UIMax = "20000", Units = "cm"))
	float RoofMaxSizeCm = 6000.f;

	// --- Smoothing / start ---

	/** The look follows the weather at most this fast (per second) up / down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoothing", meta = (ClampMin = "0.01"))
	float IntensityRisePerSecond = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoothing", meta = (ClampMin = "0.01"))
	float IntensityFallPerSecond = 0.3f;

	/** Below this smoothed intensity nothing spawns and the systems deactivate softly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoothing", meta = (ClampMin = "0"))
	float OffThreshold = 0.02f;

	/** Rain already falling at the level start / a load is simulated this long before the first frame (0 = DropLifetime). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Start", meta = (ClampMin = "0", ClampMax = "10", Units = "s"))
	float StartWarmupSeconds = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Start", meta = (ClampMin = "0.01", ClampMax = "0.5", Units = "s"))
	float StartWarmupTickSeconds = 0.1f;
};

/** The values written to NS_Weather_Rain / NS_Weather_PuddleSplashes User parameters (docs §18). */
struct CODEXTACTICS_API FWeatherRainFxParams
{
	float Intensity = 0.f;
	float SpawnRate = 0.f;
	float Opacity = 0.f;
	float OpacityRandomMin = 0.f;
	float DropLengthMin = 0.f;
	float DropLengthMax = 0.f;
	float DropWidthMin = 0.f;
	float DropWidthMax = 0.f;
	FVector Gravity = FVector(0., 0., -4000.);
	float FallSpeed = 0.f;
	float Lifetime = 0.f;
	float WindStrength = 0.f;
	FVector WindDirection = FVector::ForwardVector;
	float DropWindSpeed = 0.f;
	float SpawnRadius = 0.f;
	float SpawnHeight = 0.f;
	float SplashRate = 0.f;
	float SplashSizeMin = 0.f;
	float SplashSizeMax = 0.f;
	float SplashRadius = 0.f;
	float SplashOpacity = 0.f;
};

/** Pure logic of UWeatherRainFxComponent (tests: CodexTactics.Environment.RainFx.*). */
namespace WeatherRainFxRules
{
	/** Kill boxes NS_Weather_Rain has (User.KillBox0Origin / Size .. KillBox3*), the snow's count. */
	constexpr int32 KillBoxCount = WeatherSnowFxRules::KillBoxCount;

	/** The look intensity 0..1 from the weather amounts of the Rain (Snowfall) and Storm (Blizzard) rungs (GetWeatherAmount; sum <= 1). */
	CODEXTACTICS_API float GetLookIntensity(float RainAmount, float StormAmount, const FWeatherRainFxTuning& Tuning);

	/** Moves Current toward Target (both clamped 0..1) by the rise / fall rate of Tuning over DeltaSeconds. */
	CODEXTACTICS_API float StepIntensity(float Current, float Target, float DeltaSeconds, const FWeatherRainFxTuning& Tuning);

	/** The User parameter values for a smoothed look Intensity (0..1) and Wind (0..1, horizontal direction; zero -> +X). Under OffThreshold nothing spawns. */
	CODEXTACTICS_API FWeatherRainFxParams ComputeParams(float Intensity, float Wind, const FVector& WindDirection, const FWeatherRainFxTuning& Tuning);

	/** Offset of the system from the view focus: upwind by UpwindOffsetAtFullWind x Wind^2. */
	CODEXTACTICS_API FVector ComputeSpawnOffset(const FVector& WindDirection, float Wind, const FWeatherRainFxTuning& Tuning);

	/** Puddle splashes per second: PuddleSplashRateAtFull x Intensity x PuddleAmount (0 under the off threshold / PuddleSplashMinPuddles / switched off). */
	CODEXTACTICS_API float ComputePuddleSplashRate(float Intensity, float PuddleAmount, const FWeatherRainFxTuning& Tuning);

	/** The screen-drops post-process strength: ScreenDropsStrength x ScreenDrops (MPC 0 / 1) x RainIntensity (0 when switched off). */
	CODEXTACTICS_API float ComputeScreenDropsStrength(float ScreenDrops, float RainIntensity, const FWeatherRainFxTuning& Tuning);

	/**
	 * The snow tuning carrying the rain's indoor rules (search radius, margins, roof trace / size), so the kill boxes are made exactly like
	 * the falling snow's (WeatherSnowFxRules::MakeZoneKillBox / MakeRoofKillBox / IsRoofCandidate / SelectKillBoxes).
	 */
	CODEXTACTICS_API FWeatherSnowFxTuning ToIndoorTuning(const FWeatherRainFxTuning& Tuning);

	/** Lightning runs: the switch, the storm amount at least LightningMinStorm, and not suppressed (automated run / console). */
	CODEXTACTICS_API bool IsLightningActive(float StormAmount, bool bSuppressed, const FWeatherRainFxTuning& Tuning);

	/** Lightning console switch Codex.Env.Lightning: -1 auto (off on automated runs: smokes, bots, captures), 0 off, 1 on. True = suppressed. */
	CODEXTACTICS_API bool IsLightningSuppressed(int32 ConsoleValue, bool bAutomatedRun);

	/** Seconds until strike Index + 1 after strike Index (Index 0 = the first after the storm started): Interval min..max from (Seed, Index). */
	CODEXTACTICS_API float ComputeStrikeDelay(int32 Seed, int32 Index, const FWeatherRainFxTuning& Tuning);

	/** The flash scale 0..1 of strike (Seed, Index) at SecondsSinceStrike: 2..LightningMaxPulses decaying pulses within LightningFlashSeconds. */
	CODEXTACTICS_API float ComputeFlashScale(float SecondsSinceStrike, int32 Seed, int32 Index, const FWeatherRainFxTuning& Tuning);

	/** The pulse count of strike (Seed, Index) (2..LightningMaxPulses, 1 when the max is 1). */
	CODEXTACTICS_API int32 GetPulseCount(int32 Seed, int32 Index, const FWeatherRainFxTuning& Tuning);
}
