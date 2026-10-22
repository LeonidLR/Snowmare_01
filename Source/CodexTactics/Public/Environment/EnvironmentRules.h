#pragma once

#include "CoreMinimal.h"
#include "Environment/EnvironmentTypes.h"
#include "GameFlow/GameFlowTypes.h"

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
	 * A fresh state: Hour, Weather, a rolled NextWeather and dwell for the phase of Hour, the RNG seeded with Seed
	 * (0 -> a random seed). Weather is the level's / the random start weather (the caller picks it, e.g. PickStartWeather).
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
}
