#pragma once

#include "CoreMinimal.h"
#include "EnvironmentStartRules.generated.h"

struct FWeatherSnowFxTuning;

/**
 * What a level brings of the GLOBAL lighting the weather presenter drives (counted by AWeatherPresenterActor::Adopt from the map's actors).
 * docs/systems/environment-weather.md section 16 (game designer 2026-10-25). UE-only, no Godot reference.
 */
struct CODEXTACTICS_API FLevelLightingInventory
{
	/** ADirectionalLight actors (the brightest / atmosphere light 0 is the sun, a second one the moon). */
	int32 DirectionalLights = 0;
	bool bSkyAtmosphere = false;
	bool bSkyLight = false;
	bool bHeightFog = false;
	/** An enabled unbound APostProcessVolume. */
	bool bUnboundPostProcess = false;
};

/** How the presenter lights a level: with the level's own actors, its own rig, or both (each part from where it exists). */
UENUM(BlueprintType)
enum class EWeatherRigMode : uint8
{
	/** Sun, sky atmosphere, sky light and height fog are the level's (the presenter's moon / post-process fill in as before). */
	Adopted,
	/** The level has none of them: the presenter's own sun / moon / sky atmosphere / sky light / height fog / post-process. */
	Own,
	/** Some are the level's, the missing ones are the presenter's own. */
	Mixed,
};

/** Which part of the rig is the presenter's own component (the rest is adopted from the level). */
struct CODEXTACTICS_API FWeatherRigPlan
{
	bool bOwnSun = false;
	bool bOwnMoon = false;
	bool bOwnSkyAtmosphere = false;
	bool bOwnSkyLight = false;
	bool bOwnHeightFog = false;
	bool bOwnPostProcess = false;
	EWeatherRigMode Mode = EWeatherRigMode::Adopted;
};

/**
 * Pure rules of the level START and the presenter's own lighting rig (docs/systems/environment-weather.md section 16; tests
 * CodexTactics.Environment.StartLighting.*). The start state is shown from the first rendered frame: the presenter applies it in its
 * BeginPlay, the exposure settles at once, the falling snow is pre-warmed and jumps to its target (the smoothing is for weather changes
 * during play only).
 */
namespace EnvironmentStartRules
{
	/**
	 * The rig for a level: every part the level has is adopted (backward compatible); with bBuildMissing each missing part of sun / sky
	 * atmosphere / sky light / height fog is the presenter's own (false = missing parts stay missing, the old behaviour). The moon is
	 * own unless the level has a second directional light; the post-process is own unless the level has an unbound volume (as before).
	 * Mode: Adopted = no own sun / atmosphere / sky light / fog (all from the level, or missing without bBuildMissing); Own = all four
	 * own; Mixed otherwise.
	 */
	CODEXTACTICS_API FWeatherRigPlan PlanRig(const FLevelLightingInventory& Level, bool bBuildMissing);

	/** "adopted" / "own" / "mixed" (logs, smokes). */
	CODEXTACTICS_API FString RigModeToString(EWeatherRigMode Mode);

	/**
	 * Seconds of falling-snow simulation run before the first frame that shows it, so the air is already full: the tuning's
	 * StartWarmupSeconds, or the longest flake life when it is 0 (a flake born at the top falls through the whole spawn height in that
	 * time); clamped to [0, MaxWarmupSeconds].
	 */
	CODEXTACTICS_API float GetSnowWarmupSeconds(const FWeatherSnowFxTuning& Tuning);

	/** Simulation ticks of the warm-up (ceil of seconds / tick, at least 1 when there is anything to warm; 0 for 0 seconds). */
	CODEXTACTICS_API int32 GetSnowWarmupTicks(float WarmupSeconds, float TickSeconds);

	/** Upper bound of the start warm-up (a one-off cost while the level loads). */
	constexpr float MaxWarmupSeconds = 20.f;

	/**
	 * The presenter SNAPS (no smoothing: the falling snow jumps to its target and is pre-warmed when it starts, the eye adaptation settles at
	 * once) on its first application and whenever the environment state jumped (save load / SetState: the subsystem's snap serial moved).
	 */
	CODEXTACTICS_API bool ShouldSnap(bool bFirstApplication, int32 SeenSnapSerial, int32 CurrentSnapSerial);

	/** The eye adaptation runs at the snap speed while SecondsSinceSnap is in [0, WindowSeconds) (a negative time = no snap yet). */
	CODEXTACTICS_API bool IsExposureSnapActive(float SecondsSinceSnap, float WindowSeconds);

	/** Eye adaptation speed (f-stops / s) while snapping: at 60 fps one frame closes e^-(1000/60) of the gap - settled on the first frame. */
	constexpr float ExposureSnapSpeed = 1000.f;

	/** Game seconds the snap speed is held after a snap (the authored adaptation speeds come back after it). */
	constexpr float ExposureSnapSeconds = 1.f;
}
