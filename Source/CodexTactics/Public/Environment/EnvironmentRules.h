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
	 * falls smoothly through a transition; a locked state never blends).
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

	/** The rate in force: State.Weather's lerped toward State.NextWeather's by GetTransitionBlend (a locked state never blends). */
	CODEXTACTICS_API float GetBlendedSnowCoverRate(const FEnvironmentConfig& Config, const FEnvironmentState& State);

	/**
	 * Advances State.SnowAmount by DeltaSeconds of running clock at GetBlendedSnowCoverRate, clamped to [0, 1]. It NEVER decreases (user
	 * decision 2026-10-22: in clear weather the snow only stops growing). A locked state (Demo 01) never changes. Called by the
	 * subsystem after StepWeather, only while EnvironmentRules::ShouldAdvance (frozen in the tactical pause / turn-based / AI pause).
	 * Returns the cover gained in this step.
	 */
	CODEXTACTICS_API float StepSnowCover(const FEnvironmentConfig& Config, FEnvironmentState& State, float DeltaSeconds);

	/** The start snow cover of a fresh / locked level: the level's "snow_cover" when present, else Config.SnowCoverStart; clamped 0..1. */
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

	// --- Save (docs/systems/save-load.md, key "game_state.environment") ---

	/**
	 * The whole state as the save object: hour, weather, next_weather, weather_elapsed, weather_dwell, seed, locked, snow_amount and
	 * object_snow {top, px, nx, py, ny}.
	 */
	CODEXTACTICS_API TSharedRef<FJsonObject> StateToSaveJson(const FEnvironmentState& State);

	/**
	 * The state of a save object over Fallback: every key is optional (a missing / mistyped key keeps Fallback's value; an unknown weather
	 * name too); the hour is wrapped into [0, 24), the snow cover clamped to [0, 1], dwell / elapsed to >= 0. object_snow: each pocket
	 * optional (clamped 0..1); a save WITHOUT object_snow (older builds) gets Top = the loaded snow_amount, sides 0 (SeedObjectSnow).
	 */
	CODEXTACTICS_API FEnvironmentState StateFromSaveJson(const FJsonObject& Json, const FEnvironmentState& Fallback);
}
