#pragma once

#include "CoreMinimal.h"
#include "Environment/EnvironmentTypes.h"
#include "GameFlow/GameFlowTypes.h"

class FJsonObject;
struct FRandomStream;

/**
 * Pure logic of the day cycle and the procedural weather ladder (UE-only; docs/systems/environment-weather.md).
 * No world access: UEnvironmentSubsystem owns the state and calls these. Hours are game hours in [0, 24); seconds are real seconds.
 */
namespace EnvironmentRules
{
	/** The phase containing Hour (Night wraps over midnight). */
	CODEXTACTICS_API ETimeOfDayPhase GetPhase(const FEnvironmentConfig& Config, float Hour);

	/** The phase after Phase (Night -> EarlyMorning). */
	CODEXTACTICS_API ETimeOfDayPhase GetNextPhase(ETimeOfDayPhase Phase);

	/** Game hour at which Phase starts. */
	CODEXTACTICS_API float GetPhaseStartHour(const FEnvironmentConfig& Config, ETimeOfDayPhase Phase);

	/** Game hours Phase spans (Night 18 -> 5 = 11 h). */
	CODEXTACTICS_API float GetPhaseGameHours(const FEnvironmentConfig& Config, ETimeOfDayPhase Phase);

	/** Real seconds Phase lasts. */
	CODEXTACTICS_API float GetPhaseRealSeconds(const FEnvironmentConfig& Config, ETimeOfDayPhase Phase);

	/** Clock speed in Phase: game hours per real second (each phase has its own, so every phase lasts its real duration). */
	CODEXTACTICS_API float GetClockRate(const FEnvironmentConfig& Config, ETimeOfDayPhase Phase);

	/** Real seconds in one full day (sum of the four phases; 1200 by default). */
	CODEXTACTICS_API float GetDayRealSeconds(const FEnvironmentConfig& Config);

	/**
	 * Hour after DeltaSeconds of running clock, crossing any number of phase boundaries with each phase's own rate; wrapped into
	 * [0, 24). Negative / zero delta returns Hour (wrapped).
	 */
	CODEXTACTICS_API float AdvanceClock(const FEnvironmentConfig& Config, float Hour, float DeltaSeconds);

	/** The clock and the weather run now: not in the tactical pause, not in turn-based combat, not while the world AI is paused. */
	CODEXTACTICS_API bool ShouldAdvance(ECodexCombatMode CombatMode, bool bWorldAIPaused);

	/** Neighbours on the ladder (one step apart). */
	CODEXTACTICS_API bool AreNeighbours(EWeatherState A, EWeatherState B);

	/** Weighted random start weather (StartWeight*; all zero -> Overcast). */
	CODEXTACTICS_API EWeatherState PickStartWeather(const FEnvironmentConfig& Config, FRandomStream& Random);

	/** The next weather: always a ladder neighbour (Clear -> Overcast, Blizzard -> Snowfall forced; middle by *StepUpChance). */
	CODEXTACTICS_API EWeatherState PickNextWeather(const FEnvironmentConfig& Config, EWeatherState Current, FRandomStream& Random);

	/** Real seconds a weather state started in Phase lasts (WeatherDwellPhaseFraction x the phase's real duration). */
	CODEXTACTICS_API float GetWeatherDwellSeconds(const FEnvironmentConfig& Config, ETimeOfDayPhase Phase);

	/** Real seconds of the blend at the end of a dwell (WeatherTransitionSeconds, clamped to the dwell). */
	CODEXTACTICS_API float GetTransitionSeconds(const FEnvironmentConfig& Config, float DwellSeconds);

	/** 0 while the weather is fully State.Weather, rising to 1 at the end of the dwell (fully State.NextWeather). */
	CODEXTACTICS_API float GetTransitionBlend(const FEnvironmentConfig& Config, const FEnvironmentState& State);

	/**
	 * How much of Weather is in force now, 0..1: State.Weather -> 1 - blend (1 outside a transition), State.NextWeather -> the blend,
	 * any other weather 0 (the amounts of all four always sum to 1).
	 */
	CODEXTACTICS_API float GetWeatherAmount(const FEnvironmentConfig& Config, const FEnvironmentState& State, EWeatherState Weather);

	/**
	 * A fresh state: Hour, Weather, a rolled NextWeather and dwell for the phase of Hour, the RNG seeded with Seed
	 * (0 -> a random seed). Weather is the level's / the random start weather (the caller picks it, e.g. PickStartWeather).
	 * SnowAmount = Config.SnowCoverStart (the caller applies a level's "snow_cover", GetInitialSnowCover).
	 */
	CODEXTACTICS_API FEnvironmentState MakeInitialState(const FEnvironmentConfig& Config, float Hour, EWeatherState Weather, int32 Seed);

	/**
	 * Advances the weather by DeltaSeconds of running clock (State.Hour is NOT advanced here). At the end of a dwell: Weather =
	 * NextWeather, a new NextWeather (neighbour) and a new dwell for the phase of State.Hour are rolled from State.RandomSeed (the
	 * stream state is written back), leftover seconds carry into the new dwell. A locked state never changes.
	 */
	CODEXTACTICS_API FWeatherStepResult StepWeather(const FEnvironmentConfig& Config, FEnvironmentState& State, float DeltaSeconds);

	/** "Clear" / "Overcast" / "Snowfall" / "Blizzard" and back (case-insensitive; false when unknown). */
	CODEXTACTICS_API FString WeatherToString(EWeatherState Weather);
	CODEXTACTICS_API bool WeatherFromString(const FString& Text, EWeatherState& OutWeather);

	/** "EarlyMorning" / "Midday" / "Afternoon" / "Night". */
	CODEXTACTICS_API FString PhaseToString(ETimeOfDayPhase Phase);

	// --- Phase 2: cold (design §4.3; FColdEnvironment::WeatherMultiplier / PhaseMultiplier, combined by ColdRules::StepCold) ---

	/** Open-air cold multiplier of Weather (Config.ColdMult{Clear,Overcast,Snowfall,Blizzard}; negative clamps to 0). */
	CODEXTACTICS_API float GetColdWeatherMultiplier(const FEnvironmentConfig& Config, EWeatherState Weather);

	/** Open-air cold multiplier of Phase (Config.ColdMult{EarlyMorning,Midday,Afternoon,Night}; negative clamps to 0). */
	CODEXTACTICS_API float GetColdPhaseMultiplier(const FEnvironmentConfig& Config, ETimeOfDayPhase Phase);

	/**
	 * The weather cold multiplier in force: State.Weather's, lerped toward State.NextWeather's by GetTransitionBlend (the cold rises /
	 * falls smoothly through a transition; a locked state never blends). Section 17: of State.Climate's table.
	 */
	CODEXTACTICS_API float GetBlendedColdWeatherMultiplier(const FEnvironmentConfig& Config, const FEnvironmentState& State);

	/**
	 * The weather / phase cold multipliers apply on this level: always on a running (unlocked) level; on a LOCKED one (Demo 01 tutorial,
	 * its cold_rate_mult was tuned without weather) only with Config.bColdOnLockedLevels. False -> the cold uses 1 / 1.
	 */
	CODEXTACTICS_API bool ShouldApplyCold(const FEnvironmentConfig& Config, bool bLocked);

	// --- Phase 4: persistent snow cover (FEnvironmentState::SnowAmount; design §6 / §12) ---

	/** Snow cover gained per real second in Weather: Snowfall / Blizzard their rates, Clear / Overcast 0 (negative clamps to 0). */
	CODEXTACTICS_API float GetSnowCoverRate(const FEnvironmentConfig& Config, EWeatherState Weather);

	/**
	 * The rate in force: State.Weather's lerped toward State.NextWeather's by GetTransitionBlend (a locked state never blends).
	 * Section 17: 0 on a Temperate state (it rains there: the ground cover, the objects' and the characters' snow never grow).
	 */
	CODEXTACTICS_API float GetBlendedSnowCoverRate(const FEnvironmentConfig& Config, const FEnvironmentState& State);

	/**
	 * Advances State.SnowAmount by DeltaSeconds of running clock at GetBlendedSnowCoverRate, clamped to [0, 1]. It NEVER decreases (user
	 * decision 2026-10-22: in clear weather the snow only stops growing). A locked state (Demo 01) never changes. Called by the
	 * subsystem after StepWeather, only while EnvironmentRules::ShouldAdvance (frozen in the tactical pause / turn-based / AI pause).
	 * Returns the cover gained in this step.
	 */
	CODEXTACTICS_API float StepSnowCover(const FEnvironmentConfig& Config, FEnvironmentState& State, float DeltaSeconds);

	/**
	 * The start snow cover of a fresh / locked level: the level's "snow_cover" when present, else Config.SnowCoverStart (Arctic) / 0
	 * (Temperate, section 17); clamped 0..1.
	 */
	CODEXTACTICS_API float GetInitialSnowCover(const FEnvironmentConfig& Config, const FEnvironmentLevelSettings& Level);

	// --- Directional snow on objects / characters (FEnvironmentState::ObjectSnow, UCharacterSnowSubsystem; design §15) ---

	/** max(0, -dot(Wind, side)) for side pocket Index (1..4; the top 0): 1 when the side faces straight into the wind. Wind need not be unit. */
	CODEXTACTICS_API float GetSideWindFactor(const FVector2D& WindDirection, int32 PocketIndex);

	/** 0 with the sun at / below the horizon, rising linearly to 1 at Config.ObjectSnowMeltFullSunElevation degrees. */
	CODEXTACTICS_API float GetSunMeltFactor(const FEnvironmentConfig& Config, float SunElevationDegrees);

	/**
	 * The objects' melt per real second now: ObjectSnowMeltRate x the Clear weather amount (GetWeatherAmount: 0 while it snows or is
	 * overcast, blended through a transition) x GetSunMeltFactor. No melt at night, under cloud or while it snows.
	 */
	CODEXTACTICS_API float GetObjectSnowMeltRate(const FEnvironmentConfig& Config, const FEnvironmentState& State, float SunElevationDegrees);

	/**
	 * One step of directional pockets (pure): Top += GrowRate x Dt; side += GrowRate x SideRateScale x GetSideWindFactor x WindStrength x Dt;
	 * then every pocket loses MeltRate x Dt (never below TopFloor for the top, 0 for a side; a pocket already below its floor stays) and
	 * ClearRate x Dt (down to 0). Clamped to [0, 1]. A pocket the wind does not reach keeps its snow (memory).
	 */
	CODEXTACTICS_API void StepDirectionalSnow(FDirectionalSnow& Pockets, const FDirectionalSnowInputs& Inputs, float DeltaSeconds);

	/**
	 * The inputs of the objects' step: GrowRate = GetBlendedSnowCoverRate, the wind as given (world travel direction, xy), SideRateScale =
	 * ObjectSnowSideRateScale, MeltRate = GetObjectSnowMeltRate, TopFloor = ObjectSnowMeltFloor x State.SnowAmount.
	 */
	CODEXTACTICS_API FDirectionalSnowInputs MakeObjectSnowInputs(const FEnvironmentConfig& Config, const FEnvironmentState& State,
		const FVector2D& WorldWindDirection, float WindStrength, float SunElevationDegrees);

	/**
	 * Advances State.ObjectSnow by DeltaSeconds of running clock (MakeObjectSnowInputs + StepDirectionalSnow). Called by the subsystem after
	 * StepSnowCover. A locked state (Demo 01) never changes, like the ground cover.
	 */
	CODEXTACTICS_API void StepObjectSnow(const FEnvironmentConfig& Config, FEnvironmentState& State, float DeltaSeconds,
		const FVector2D& WorldWindDirection, float WindStrength, float SunElevationDegrees);

	/**
	 * Back-compat / fresh start: when every object pocket is 0 but the ground has snow, the objects' top takes the ground cover
	 * (Top = SnowAmount, sides 0). Returns true when it seeded.
	 */
	CODEXTACTICS_API bool SeedObjectSnow(FEnvironmentState& State);

	// --- Section 17: climate profiles (EClimateProfile; Arctic = everything above unchanged) ---

	/** "arctic" / "temperate". */
	CODEXTACTICS_API FString ClimateToString(EClimateProfile Climate);

	/** "arctic" / "temperate" (case-insensitive, trimmed); false when unknown. */
	CODEXTACTICS_API bool ClimateFromString(const FString& Text, EClimateProfile& OutClimate);

	/**
	 * The name of Weather on Climate for the UI / logs: Arctic = WeatherToString ("Clear" / "Overcast" / "Snowfall" / "Blizzard");
	 * Temperate = "SunnyDay" / "Overcast" / "Rain" / "Storm". Saves and environment.json keep the ladder names (WeatherToString).
	 */
	CODEXTACTICS_API FString GetWeatherDisplayName(EClimateProfile Climate, EWeatherState Weather);

	/**
	 * WeatherFromString (the ladder names, any climate) plus, on a Temperate climate only, its own names: "Sunny" / "SunnyDay" -> Clear,
	 * "Rain" -> Snowfall, "Storm" -> Blizzard (case-insensitive). On Arctic "Rain" is unknown (false).
	 */
	CODEXTACTICS_API bool WeatherFromString(EClimateProfile Climate, const FString& Text, EWeatherState& OutWeather);

	/** Open-air cold multiplier of Weather on Climate: Arctic = GetColdWeatherMultiplier(Config, Weather); Temperate = Config.Temperate.ColdMult*. */
	CODEXTACTICS_API float GetColdWeatherMultiplier(const FEnvironmentConfig& Config, EClimateProfile Climate, EWeatherState Weather);

	/** Open-air cold multiplier of Phase on Climate (Arctic = GetColdPhaseMultiplier(Config, Phase); Temperate its own block). */
	CODEXTACTICS_API float GetColdPhaseMultiplier(const FEnvironmentConfig& Config, EClimateProfile Climate, ETimeOfDayPhase Phase);

	/** The weather / time-of-day roster runs on Climate: Arctic always (Config.Roster.bEnabled is applied by EnvironmentRosterRules); Temperate when Config.Temperate.bRosterEnabled. */
	CODEXTACTICS_API bool IsRosterActive(const FEnvironmentConfig& Config, EClimateProfile Climate);

	/** Falling snow may show on Climate (Arctic always; Temperate only with Config.Temperate.bFallingSnow). */
	CODEXTACTICS_API bool IsFallingSnowAllowed(const FEnvironmentConfig& Config, EClimateProfile Climate);

	/**
	 * Rain 0..1 in force (UEnvironmentSubsystem::GetRainIntensity, MPC RainIntensity): Temperate only - Rain (the Snowfall rung)
	 * Config.Temperate.RainIntensityRain, Storm (Blizzard) RainIntensityStorm, SunnyDay / Overcast 0, weighted by GetWeatherAmount
	 * (blended through a transition). 0 on an Arctic state.
	 */
	CODEXTACTICS_API float GetRainIntensity(const FEnvironmentConfig& Config, const FEnvironmentState& State);

	/** MPC IsRaining 0..1 from the rain intensity: smooth, 1 from 0.25 up (the IsSnowing rule). */
	CODEXTACTICS_API float ComputeIsRaining(float RainIntensity);

	/** MPC ScreenDrops: a Temperate state while it rains (IsRaining >= 0.5) and Config.Temperate.bScreenDrops (user decision: ON). */
	CODEXTACTICS_API bool ShouldShowScreenDrops(const FEnvironmentConfig& Config, const FEnvironmentState& State);

	/** How much it SNOWS now, 0..1: the Snowfall + Blizzard amounts on an Arctic state; 0 on a Temperate one (those rungs are rain). */
	CODEXTACTICS_API float GetSnowingAmount(const FEnvironmentConfig& Config, const FEnvironmentState& State);

	/**
	 * Wetness change per real second now (Temperate; 0 on Arctic): Rain +WetnessRateRain, Storm +WetnessRateStorm, Overcast
	 * -DryingRateOvercast, SunnyDay -lerp(DryingRateOvercast, DryingRateSunny, sun ramp 0 at the horizon -> 1 at DryingFullSunElevation),
	 * weighted by GetWeatherAmount (blended through a transition).
	 */
	CODEXTACTICS_API float GetWetnessRate(const FEnvironmentConfig& Config, const FEnvironmentState& State, float SunElevationDegrees);

	/** The puddle level the ground supports at Wetness: (wetness - PuddleWetnessThreshold) / (1 - threshold), clamped 0..1. */
	CODEXTACTICS_API float GetPuddleTarget(const FEnvironmentConfig& Config, float Wetness);

	/**
	 * Advances State.Wetness / State.PuddleAmount by DeltaSeconds of running clock (Temperate only; an Arctic or locked state never
	 * changes): Wetness += GetWetnessRate x Dt (clamped 0..1); the puddles move toward GetPuddleTarget - up at PuddleFillRate only while
	 * the ground gets wetter, down at PuddleDrainRate (slower than it dries). Called by the subsystem after the snow steps.
	 */
	CODEXTACTICS_API void StepWetness(const FEnvironmentConfig& Config, FEnvironmentState& State, float DeltaSeconds, float SunElevationDegrees);

	/**
	 * Start wetness / puddles of a state that starts on Level.Climate in Weather: Arctic 0 / 0; Temperate SunnyDay / Overcast 0 / 0,
	 * Rain StartWetnessRain / StartPuddlesRain, Storm StartWetnessStorm / StartPuddlesStorm; the level's "wetness" / "puddles" win.
	 */
	CODEXTACTICS_API void GetInitialWetness(const FEnvironmentConfig& Config, const FEnvironmentLevelSettings& Level, EWeatherState Weather,
		float& OutWetness, float& OutPuddles);

	// --- Save (docs/systems/save-load.md, key "game_state.environment") ---

	/**
	 * The whole state as the save object: hour, weather, next_weather, weather_elapsed, weather_dwell, seed, locked, snow_amount,
	 * object_snow {top, px, nx, py, ny} and (section 17) climate ("arctic" / "temperate"), wetness, puddles.
	 */
	CODEXTACTICS_API TSharedRef<FJsonObject> StateToSaveJson(const FEnvironmentState& State);

	/**
	 * The state of a save object over Fallback: every key is optional (a missing / mistyped key keeps Fallback's value; an unknown weather
	 * name too); the hour is wrapped into [0, 24), the snow cover clamped to [0, 1], dwell / elapsed to >= 0. object_snow: each pocket
	 * optional (clamped 0..1); a save WITHOUT object_snow (older builds) gets Top = the loaded snow_amount, sides 0 (SeedObjectSnow).
	 * Section 17: climate / wetness / puddles optional (an unknown climate name keeps Fallback's; wetness / puddles clamped 0..1).
	 */
	CODEXTACTICS_API FEnvironmentState StateFromSaveJson(const FJsonObject& Json, const FEnvironmentState& Fallback);
}
