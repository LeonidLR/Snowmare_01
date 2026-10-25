#pragma once

#include "CoreMinimal.h"
#include "WeatherSnowFxRules.generated.h"

/**
 * Tuning of the falling snow for the tactical camera (environment phase 4b, docs/systems/environment-weather.md §12.b). Visual only.
 * Values in UE units (cm, s). The look is edited in the data asset DA_SnowFX (USnowFxSettings, /Game/Environment/Weather/Snow/DA_SnowFX);
 * these C++ defaults are its initial values and the fallback when the asset is missing. How MUCH snow falls per weather is the presets'
 * SnowIntensity (DA_Weather_<State>, 0 Clear .. 1 Blizzard); this struct says what intensity 1 looks like.
 * No Godot reference (UE-only feature).
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FWeatherSnowFxTuning
{
	GENERATED_BODY()

	// --- Flakes ---

	/**
	 * Flake size (cm), each flake random between min and max. The tactical camera is 26-40 m away: 5-11 cm reads as fine snow (the first
	 * version's 8-18 cm looked like big blobs; 4-9 cm was too faint on the light-grey blizzard - stills 2026-10-23).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flakes", meta = (ClampMin = "0.5", UIMax = "40", Units = "cm"))
	float FlakeSizeMin = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flakes", meta = (ClampMin = "0.5", UIMax = "40", Units = "cm"))
	float FlakeSizeMax = 11.f;

	/** Density: flakes per second at intensity 1 over the whole spawn volume; scaled by intensity^SpawnRateExponent. GPU cost: see the docs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flakes", meta = (ClampMin = "0", UIMax = "40000"))
	float SpawnRateAtFull = 16000.f;

	/** >1 keeps calm snowfall sparse and the blizzard dense (Snowfall 0.6 -> 0.36 of the blizzard rate at 2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flakes", meta = (ClampMin = "0.1", UIMax = "4"))
	float SpawnRateExponent = 2.f;

	/** Flake opacity at a trace of snow and at intensity 1 (each flake gets a random opacity between FlakeOpacityRandomMin and this). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flakes", meta = (ClampMin = "0", ClampMax = "4"))
	float OpacityMin = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flakes", meta = (ClampMin = "0", ClampMax = "4"))
	float OpacityMax = 1.f;

	/** Low end of the per-flake random opacity (NS_Weather_Snow User.Min_Snow_Opacity). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flakes", meta = (ClampMin = "0", ClampMax = "4"))
	float FlakeOpacityRandomMin = 0.5f;

	/** Flake lifetime (s), random between min and max: long enough to fall through the whole spawn height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flakes", meta = (ClampMin = "0.1", UIMax = "30", Units = "s"))
	float LifetimeMin = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flakes", meta = (ClampMin = "0.1", UIMax = "30", Units = "s"))
	float LifetimeMax = 9.f;

	/**
	 * Fall speed: the downward acceleration of a flake (cm/s^2, 980 = real gravity). The aerodynamic drag of NS_Weather_Snow caps the speed,
	 * so the fall speed grows roughly with the square root of this (x2 -> ~1.4x faster).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flakes", meta = (ClampMin = "0", UIMax = "5000"))
	float FallAcceleration = 980.f;

	/** Hard cap of a flake's speed (cm/s, wind included). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flakes", meta = (ClampMin = "10", UIMax = "6000"))
	float SpeedLimit = 2500.f;

	/** Wind influence: the flakes' wind speed (cm/s) at WindStrength 1 (the presets' wind; scaled by wind^2). 0 = the wind never moves them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flakes", meta = (ClampMin = "0", UIMax = "4000"))
	float FlakeWindSpeed = 1100.f;

	/** Spawn area: a cylinder standing on the ground point the view looks at (moved upwind in a strong wind), radius and height (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flakes", meta = (ClampMin = "100", UIMax = "8000", Units = "cm"))
	float SpawnRadius = 3000.f;

	/** From the ground up; keep it above the highest camera (40 m x sin 35 deg = 23 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flakes", meta = (ClampMin = "100", UIMax = "6000", Units = "cm"))
	float SpawnHeight = 2800.f;

	/** Near-camera fade (MI_Weather_Snowflake): flakes closer than NearFadeStart are invisible, fully visible NearFadeLength further. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flakes", meta = (ClampMin = "0", UIMax = "3000", Units = "cm"))
	float NearFadeStart = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flakes", meta = (ClampMin = "1", UIMax = "3000", Units = "cm"))
	float NearFadeLength = 900.f;

	/** Soft edge of the round flake (0 = blurry disk, 0.9 = hard dot) and its brightness. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flakes", meta = (ClampMin = "0", ClampMax = "0.99"))
	float FlakeSoftness = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flakes", meta = (ClampMin = "0", UIMax = "4"))
	float FlakeBrightness = 1.f;

	// --- Ground drift ("pozyomka") ---

	/** The ground drift layer (wind-blown snow puffs near the ground) on / off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drift")
	bool bDriftEnabled = true;

	/** Drift starts above this intensity and reaches DriftOpacityMax / DriftRateAtFull at 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drift", meta = (ClampMin = "0", ClampMax = "1"))
	float DriftStartIntensity = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drift", meta = (ClampMin = "0", ClampMax = "4"))
	float DriftOpacityMax = 0.65f;

	/** Low end of the per-puff random opacity (User.Min_SnowSmoke_Opacity). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drift", meta = (ClampMin = "0", ClampMax = "4"))
	float DriftOpacityRandomMin = 0.f;

	/** Drift puff base size (cm; NS_Weather_Snow multiplies it by a random 1.2..2.5) at the start intensity and at 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drift", meta = (ClampMin = "0", UIMax = "3000", Units = "cm"))
	float DriftSpriteSizeMin = 250.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drift", meta = (ClampMin = "0", UIMax = "3000", Units = "cm"))
	float DriftSpriteSizeMax = 500.f;

	/** Puffs per second at intensity 1 (scaled linearly from DriftStartIntensity). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drift", meta = (ClampMin = "0", UIMax = "200"))
	float DriftRateAtFull = 24.f;

	/** The puffs' wind speed (cm/s) at WindStrength 1 (scaled by wind^2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drift", meta = (ClampMin = "0", UIMax = "4000"))
	float DriftWindSpeed = 1200.f;

	/** The drift band: a cylinder radius around the view focus and its height above the ground (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drift", meta = (ClampMin = "100", UIMax = "8000", Units = "cm"))
	float DriftRadius = 2600.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drift", meta = (ClampMin = "10", UIMax = "2000", Units = "cm"))
	float DriftHeight = 250.f;

	// --- Wind ---

	/**
	 * The system stands this far UPWIND of the view focus at wind 1 (scaled by wind^2, like the flakes' wind speed): with a strong wind
	 * the flakes cross the view instead of being born in it, so the blizzard's sideways sheets fill the whole view. Keep it in step with
	 * SpawnRadius / FlakeWindSpeed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind", meta = (ClampMin = "0", UIMax = "8000", Units = "cm"))
	float UpwindOffsetAtFullWind = 4000.f;

	/** The wind the system receives never drops below this (snow always drifts a little). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind", meta = (ClampMin = "0", ClampMax = "1"))
	float WindStrengthFloor = 0.1f;

	// --- Indoors ---

	/**
	 * No snow inside buildings: flakes and drift die inside the nearest Closed / Shelter ACameraZoneVolumes and under a roof above the view
	 * focus / the squad leader (Niagara kill boxes, User.KillBox<i>Origin / Size). Flakes also die where they hit a roof (depth-buffer collision).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Indoors")
	bool bNoSnowIndoors = true;

	/** Interior zones are looked for within this distance of the view focus (cm, to the zone's edge). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Indoors", meta = (ClampMin = "0", UIMax = "20000", Units = "cm"))
	float IndoorSearchRadius = 6000.f;

	/** A kill box reaches this far past the zone's sides (walls are often a bit outside the zone box) and this far below its bottom. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Indoors", meta = (ClampMin = "0", UIMax = "200", Units = "cm"))
	float IndoorMarginCm = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Indoors", meta = (ClampMin = "0", UIMax = "1000", Units = "cm"))
	float IndoorBelowCm = 150.f;

	/** Roofs that are not a camera zone: an upward trace from the view focus and the leader; the roof mesh's footprint becomes a kill box. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Indoors")
	bool bKillUnderRoofs = true;

	/** The upward trace length (cm) and the largest roof footprint (cm, longest side) still treated as a roof (not a sky dome / terrain). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Indoors", meta = (ClampMin = "100", UIMax = "10000", Units = "cm"))
	float RoofTraceHeight = 2500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Indoors", meta = (ClampMin = "100", UIMax = "20000", Units = "cm"))
	float RoofMaxSizeCm = 6000.f;

	// --- Smoothing ---

	/** Intensity follows the weather at most this fast (per second) up / down (an instant ForceWeather fades in). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoothing", meta = (ClampMin = "0.01"))
	float IntensityRisePerSecond = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoothing", meta = (ClampMin = "0.01"))
	float IntensityFallPerSecond = 0.25f;

	/** Below this smoothed intensity nothing spawns and the system deactivates softly (the flakes in the air still land). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoothing", meta = (ClampMin = "0"))
	float OffThreshold = 0.02f;

	// --- Level start (docs/systems/environment-weather.md section 16) ---

	/**
	 * When the snow is already falling at the level start (or after a save load), this much simulation runs before the first frame that
	 * shows it, so the air is full at once (flakes and ground drift); 0 = the longest flake life (LifetimeMax). Not used for weather
	 * changes during play (they fade in by IntensityRisePerSecond).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Start", meta = (ClampMin = "0", ClampMax = "20", Units = "s"))
	float StartWarmupSeconds = 0.f;

	/** The warm-up's simulation step (smaller = smoother but more ticks while the level loads). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Start", meta = (ClampMin = "0.01", ClampMax = "0.5", Units = "s"))
	float StartWarmupTickSeconds = 0.1f;
};

/** The values written to the Niagara User parameters of NS_Weather_Snow (docs §12.b). */
struct CODEXTACTICS_API FWeatherSnowFxParams
{
	// The contract (docs §5).
	float WeatherIntensity = 0.f;
	float SpawnRate = 0.f;
	float Opacity = 0.f;
	float WindStrength = 0.f;
	FVector WindDirection = FVector::ForwardVector;
	float DriftOpacity = 0.f;
	float DriftSpriteSize = 0.f;
	// The look (DA_SnowFX).
	float FlakeSizeMin = 0.f;
	float FlakeSizeMax = 0.f;
	float FlakeOpacityRandomMin = 0.f;
	float LifetimeMin = 0.f;
	float LifetimeMax = 0.f;
	FVector Gravity = FVector(0., 0., -980.);
	float SpeedLimit = 0.f;
	float FlakeWindSpeed = 0.f;
	float SpawnRadius = 0.f;
	float SpawnHeight = 0.f;
	float DriftRate = 0.f;
	float DriftOpacityRandomMin = 0.f;
	float DriftWindSpeed = 0.f;
	float DriftRadius = 0.f;
	float DriftHeight = 0.f;
};

/** Pure logic of UWeatherSnowFxComponent (tests: CodexTactics.Environment.SnowFx.*). */
namespace WeatherSnowFxRules
{
	/** Kill boxes NS_Weather_Snow has (User.KillBox0Origin / KillBox0Size .. KillBox3*), on both emitters. */
	constexpr int32 KillBoxCount = 4;

	/** Moves Current toward Target (both clamped 0..1) by the rise / fall rate of Tuning over DeltaSeconds. */
	CODEXTACTICS_API float StepIntensity(float Current, float Target, float DeltaSeconds, const FWeatherSnowFxTuning& Tuning);

	/**
	 * The User parameter values for a smoothed Intensity (0..1) and Wind (0..1, horizontal direction; a zero direction falls back to +X).
	 * Intensity under OffThreshold -> nothing spawns (flakes nor drift); drift off -> no drift puffs.
	 */
	CODEXTACTICS_API FWeatherSnowFxParams ComputeParams(float Intensity, float Wind, const FVector& WindDirection, const FWeatherSnowFxTuning& Tuning);

	/** Offset of the system from the view focus: upwind (against the horizontal WindDirection) by UpwindOffsetAtFullWind x Wind^2. */
	CODEXTACTICS_API FVector ComputeSpawnOffset(const FVector& WindDirection, float Wind, const FWeatherSnowFxTuning& Tuning);

	/**
	 * Where the view looks at the ground: the camera ray hits the horizontal plane at GroundZ (cm); a ray that never meets it (looking up or
	 * flat) -> the point FallbackDistance ahead projected to GroundZ.
	 */
	CODEXTACTICS_API FVector ComputeViewFocus(const FVector& CameraLocation, const FVector& CameraForward, float GroundZ, float FallbackDistance);

	/**
	 * The world axis-aligned kill box of an interior zone (ZoneTransform without scale, scaled ZoneHalfExtent): the rotated box's bounds grown
	 * by MarginCm on the sides, the bottom lowered by BelowCm (the floor and a sunken interior), the top kept.
	 */
	CODEXTACTICS_API FBox MakeZoneKillBox(const FTransform& ZoneTransform, const FVector& ZoneHalfExtent, float MarginCm, float BelowCm);

	/** The kill box under a roof mesh (its world bounds RoofBounds) standing over FloorZ: the footprint + MarginCm, from FloorZ - BelowCm to the roof top. */
	CODEXTACTICS_API FBox MakeRoofKillBox(const FBox& RoofBounds, double FloorZ, float MarginCm, float BelowCm);

	/** A trace hit above the focus counts as a roof: within RoofTraceHeight above FromZ and a footprint no longer than RoofMaxSizeCm. */
	CODEXTACTICS_API bool IsRoofCandidate(const FBox& Bounds, double HitZ, double FromZ, const FWeatherSnowFxTuning& Tuning);

	/** Squared horizontal distance from Point to Box (0 inside the footprint). */
	CODEXTACTICS_API double DistanceSq2D(const FVector& Point, const FBox& Box);

	/**
	 * The kill boxes to use: valid boxes within Radius (to their edge) of Focus, nearest first, a box inside an already chosen one dropped,
	 * at most MaxCount.
	 */
	CODEXTACTICS_API TArray<FBox> SelectKillBoxes(const TArray<FBox>& Candidates, const FVector& Focus, double Radius, int32 MaxCount);

	/** The Niagara kill test of KillParticlesInVolume (box mode, Box Size = full size around Volume Origin): Point strictly inside Box. */
	CODEXTACTICS_API bool IsInsideKillBox(const FVector& Point, const FBox& Box);

	/** The user parameter values of one slot: an invalid box (unused slot) -> a zero-size box far below the world. */
	CODEXTACTICS_API void PackKillBox(const FBox& Box, FVector& OutOrigin, FVector& OutSize);
}
