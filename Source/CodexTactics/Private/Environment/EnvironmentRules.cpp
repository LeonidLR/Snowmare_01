#include "Environment/EnvironmentRules.h"
#include "Math/RandomStream.h"

// UE-only (no Godot reference). Design: docs/systems/environment-weather.md (user decisions 2026-10-22).

namespace EnvironmentRules
{
	namespace
	{
		constexpr float HoursPerDay = 24.f;
		/** Safety bound for the boundary-crossing clock loop (a day has 4 phases; one delta is reduced to under a day first). */
		constexpr int32 MaxClockIterations = 16;
		/** Safety bound for the weather loop (one huge delta may chain several dwells; 4096 x the shortest dwell is far beyond any frame). */
		constexpr int32 MaxWeatherChanges = 4096;
		/** Shortest dwell the loop accepts, so a broken config can never spin it without progress. */
		constexpr float MinDwellSeconds = 0.05f;

		const ETimeOfDayPhase AllPhases[] = {ETimeOfDayPhase::EarlyMorning, ETimeOfDayPhase::Midday, ETimeOfDayPhase::Afternoon, ETimeOfDayPhase::Night};

		/** Hour wrapped into [0, 24). */
		float WrapHour(float Hour)
		{
			float Wrapped = FMath::Fmod(Hour, HoursPerDay);
			if (Wrapped < 0.f)
			{
				Wrapped += HoursPerDay;
			}
			// Fmod of a tiny negative number + 24 can round up to exactly 24.
			return Wrapped >= HoursPerDay ? 0.f : Wrapped;
		}

		/** Game hours since Phase started, in [0, 24). */
		float HoursSincePhaseStart(const FEnvironmentConfig& Config, ETimeOfDayPhase Phase, float Hour)
		{
			return WrapHour(Hour - GetPhaseStartHour(Config, Phase));
		}

		float GetStartWeight(const FEnvironmentConfig& Config, EWeatherState Weather)
		{
			switch (Weather)
			{
			case EWeatherState::Clear:
				return FMath::Max(0.f, Config.StartWeightClear);
			case EWeatherState::Overcast:
				return FMath::Max(0.f, Config.StartWeightOvercast);
			case EWeatherState::Snowfall:
				return FMath::Max(0.f, Config.StartWeightSnowfall);
			case EWeatherState::Blizzard:
				return FMath::Max(0.f, Config.StartWeightBlizzard);
			}
			return 0.f;
		}

		/** Starts the dwell of the state now in force: rolls its NextWeather and fixes its duration by the phase of State.Hour. */
		void BeginDwell(const FEnvironmentConfig& Config, FEnvironmentState& State, FRandomStream& Random)
		{
			State.NextWeather = PickNextWeather(Config, State.Weather, Random);
			State.WeatherDwellSeconds = GetWeatherDwellSeconds(Config, GetPhase(Config, State.Hour));
			State.WeatherElapsedSeconds = 0.f;
			State.RandomSeed = Random.GetCurrentSeed();
		}
	}

	ETimeOfDayPhase GetPhase(const FEnvironmentConfig& Config, float Hour)
	{
		// The phase whose start passed most recently (ignoring phases that span no hours, e.g. two equal start hours).
		ETimeOfDayPhase Best = ETimeOfDayPhase::EarlyMorning;
		float BestSince = TNumericLimits<float>::Max();
		for (const ETimeOfDayPhase Phase : AllPhases)
		{
			if (GetPhaseGameHours(Config, Phase) <= 0.f)
			{
				continue;
			}
			const float Since = HoursSincePhaseStart(Config, Phase, Hour);
			if (Since < BestSince)
			{
				BestSince = Since;
				Best = Phase;
			}
		}
		return Best;
	}

	ETimeOfDayPhase GetNextPhase(ETimeOfDayPhase Phase)
	{
		switch (Phase)
		{
		case ETimeOfDayPhase::EarlyMorning:
			return ETimeOfDayPhase::Midday;
		case ETimeOfDayPhase::Midday:
			return ETimeOfDayPhase::Afternoon;
		case ETimeOfDayPhase::Afternoon:
			return ETimeOfDayPhase::Night;
		case ETimeOfDayPhase::Night:
			return ETimeOfDayPhase::EarlyMorning;
		}
		return ETimeOfDayPhase::EarlyMorning;
	}

	float GetPhaseStartHour(const FEnvironmentConfig& Config, ETimeOfDayPhase Phase)
	{
		switch (Phase)
		{
		case ETimeOfDayPhase::EarlyMorning:
			return WrapHour(Config.EarlyMorningStartHour);
		case ETimeOfDayPhase::Midday:
			return WrapHour(Config.MiddayStartHour);
		case ETimeOfDayPhase::Afternoon:
			return WrapHour(Config.AfternoonStartHour);
		case ETimeOfDayPhase::Night:
			return WrapHour(Config.NightStartHour);
		}
		return 0.f;
	}

	float GetPhaseGameHours(const FEnvironmentConfig& Config, ETimeOfDayPhase Phase)
	{
		return WrapHour(GetPhaseStartHour(Config, GetNextPhase(Phase)) - GetPhaseStartHour(Config, Phase));
	}

	float GetPhaseRealSeconds(const FEnvironmentConfig& Config, ETimeOfDayPhase Phase)
	{
		float Seconds = 0.f;
		switch (Phase)
		{
		case ETimeOfDayPhase::EarlyMorning:
			Seconds = Config.EarlyMorningRealSeconds;
			break;
		case ETimeOfDayPhase::Midday:
			Seconds = Config.MiddayRealSeconds;
			break;
		case ETimeOfDayPhase::Afternoon:
			Seconds = Config.AfternoonRealSeconds;
			break;
		case ETimeOfDayPhase::Night:
			Seconds = Config.NightRealSeconds;
			break;
		}
		// Same floor as the UPROPERTY ClampMin; a JSON value is not clamped by the editor.
		return FMath::Max(1.f, Seconds);
	}

	float GetClockRate(const FEnvironmentConfig& Config, ETimeOfDayPhase Phase)
	{
		return GetPhaseGameHours(Config, Phase) / GetPhaseRealSeconds(Config, Phase);
	}

	float GetDayRealSeconds(const FEnvironmentConfig& Config)
	{
		float Total = 0.f;
		for (const ETimeOfDayPhase Phase : AllPhases)
		{
			Total += GetPhaseRealSeconds(Config, Phase);
		}
		return Total;
	}

	float AdvanceClock(const FEnvironmentConfig& Config, float Hour, float DeltaSeconds)
	{
		float Current = WrapHour(Hour);
		if (DeltaSeconds <= 0.f)
		{
			return Current;
		}
		// A full day returns to the same hour, so whole days are dropped first (keeps the loop short for any delta).
		float Remaining = DeltaSeconds;
		const float DayReal = GetDayRealSeconds(Config);
		if (Remaining >= DayReal)
		{
			Remaining = FMath::Fmod(Remaining, DayReal);
		}

		for (int32 Iteration = 0; Iteration < MaxClockIterations && Remaining > 0.f; ++Iteration)
		{
			const ETimeOfDayPhase Phase = GetPhase(Config, Current);
			const float PhaseHours = GetPhaseGameHours(Config, Phase);
			if (PhaseHours <= 0.f)
			{
				break; // Degenerate config (all start hours equal): the clock cannot move.
			}
			const float PhaseSeconds = GetPhaseRealSeconds(Config, Phase);
			const float Rate = PhaseHours / PhaseSeconds;
			const float HoursLeft = FMath::Max(0.f, PhaseHours - HoursSincePhaseStart(Config, Phase, Current));
			const float SecondsLeft = HoursLeft * PhaseSeconds / PhaseHours;
			if (Remaining < SecondsLeft)
			{
				Current = WrapHour(Current + Remaining * Rate);
				Remaining = 0.f;
			}
			else
			{
				// Land exactly on the next phase's start hour, then continue at that phase's own rate.
				Remaining -= SecondsLeft;
				Current = GetPhaseStartHour(Config, GetNextPhase(Phase));
			}
		}
		return Current;
	}

	bool ShouldAdvance(ECodexCombatMode CombatMode, bool bWorldAIPaused)
	{
		return CombatMode != ECodexCombatMode::TacticalPause && CombatMode != ECodexCombatMode::TurnBased && !bWorldAIPaused;
	}

	bool AreNeighbours(EWeatherState A, EWeatherState B)
	{
		return FMath::Abs(static_cast<int32>(A) - static_cast<int32>(B)) == 1;
	}

	EWeatherState PickStartWeather(const FEnvironmentConfig& Config, FRandomStream& Random)
	{
		const EWeatherState States[] = {EWeatherState::Clear, EWeatherState::Overcast, EWeatherState::Snowfall, EWeatherState::Blizzard};
		float Total = 0.f;
		for (const EWeatherState State : States)
		{
			Total += GetStartWeight(Config, State);
		}
		if (Total <= 0.f)
		{
			return EWeatherState::Overcast;
		}
		const float Roll = Random.FRand() * Total;
		float Cumulative = 0.f;
		EWeatherState LastWeighted = EWeatherState::Overcast;
		for (const EWeatherState State : States)
		{
			const float Weight = GetStartWeight(Config, State);
			if (Weight <= 0.f)
			{
				continue;
			}
			Cumulative += Weight;
			LastWeighted = State;
			if (Roll < Cumulative)
			{
				return State;
			}
		}
		// Float rounding at the very top of the range.
		return LastWeighted;
	}

	EWeatherState PickNextWeather(const FEnvironmentConfig& Config, EWeatherState Current, FRandomStream& Random)
	{
		switch (Current)
		{
		case EWeatherState::Clear:
			return EWeatherState::Overcast;
		case EWeatherState::Overcast:
			return Random.FRand() < FMath::Clamp(Config.OvercastStepUpChance, 0.f, 1.f) ? EWeatherState::Snowfall : EWeatherState::Clear;
		case EWeatherState::Snowfall:
			return Random.FRand() < FMath::Clamp(Config.SnowfallStepUpChance, 0.f, 1.f) ? EWeatherState::Blizzard : EWeatherState::Overcast;
		case EWeatherState::Blizzard:
			return EWeatherState::Snowfall;
		}
		return EWeatherState::Overcast;
	}

	float GetWeatherDwellSeconds(const FEnvironmentConfig& Config, ETimeOfDayPhase Phase)
	{
		return FMath::Max(0.05f, Config.WeatherDwellPhaseFraction) * GetPhaseRealSeconds(Config, Phase);
	}

	float GetTransitionSeconds(const FEnvironmentConfig& Config, float DwellSeconds)
	{
		return FMath::Clamp(Config.WeatherTransitionSeconds, 0.f, FMath::Max(0.f, DwellSeconds));
	}

	float GetTransitionBlend(const FEnvironmentConfig& Config, const FEnvironmentState& State)
	{
		if (State.bLocked)
		{
			return 0.f;
		}
		const float Dwell = FMath::Max(0.f, State.WeatherDwellSeconds);
		const float Transition = GetTransitionSeconds(Config, Dwell);
		if (Transition <= 0.f)
		{
			return State.WeatherElapsedSeconds >= Dwell ? 1.f : 0.f;
		}
		return FMath::Clamp((State.WeatherElapsedSeconds - (Dwell - Transition)) / Transition, 0.f, 1.f);
	}

	FEnvironmentState MakeInitialState(const FEnvironmentConfig& Config, float Hour, EWeatherState Weather, int32 Seed)
	{
		int32 ActualSeed = Seed;
		if (ActualSeed == 0)
		{
			// FMath::Rand gives 15 bits on some platforms: combine two calls for a wider seed; never 0 (0 means "random").
			ActualSeed = ((FMath::Rand() & 0x7fff) << 15) | (FMath::Rand() & 0x7fff);
			if (ActualSeed == 0)
			{
				ActualSeed = 1;
			}
		}

		FEnvironmentState State;
		State.Hour = WrapHour(Hour);
		State.Weather = Weather;
		State.bLocked = false;
		FRandomStream Random(ActualSeed);
		BeginDwell(Config, State, Random);
		return State;
	}

	FWeatherStepResult StepWeather(const FEnvironmentConfig& Config, FEnvironmentState& State, float DeltaSeconds)
	{
		FWeatherStepResult Result;
		Result.OldWeather = State.Weather;
		if (State.bLocked || DeltaSeconds <= 0.f)
		{
			return Result;
		}

		FRandomStream Random(State.RandomSeed);
		float Remaining = DeltaSeconds;
		for (int32 Changes = 0; Changes <= MaxWeatherChanges && Remaining > 0.f; ++Changes)
		{
			const float Dwell = FMath::Max(MinDwellSeconds, State.WeatherDwellSeconds);
			const float BlendStart = Dwell - GetTransitionSeconds(Config, Dwell);
			const float Before = State.WeatherElapsedSeconds;
			const float ToEnd = FMath::Max(0.f, Dwell - Before);

			if (Remaining < ToEnd)
			{
				State.WeatherElapsedSeconds = Before + Remaining;
				Remaining = 0.f;
				if (Before < BlendStart && State.WeatherElapsedSeconds >= BlendStart)
				{
					Result.bTransitionStarted = true;
				}
				break;
			}

			// The dwell ends inside this step: the blend (if it had not begun yet) started in this step too.
			Remaining -= ToEnd;
			if (Before < BlendStart)
			{
				Result.bTransitionStarted = true;
			}
			// OldWeather is the state that just ended, so OldWeather -> Weather is always one ladder step even when one huge delta
			// chains several dwells.
			Result.OldWeather = State.Weather;
			Result.bWeatherChanged = true;
			State.Weather = State.NextWeather;
			BeginDwell(Config, State, Random);
			if (Changes == MaxWeatherChanges)
			{
				break; // Absurd delta: drop the rest rather than loop on.
			}
		}
		return Result;
	}

	FString WeatherToString(EWeatherState Weather)
	{
		switch (Weather)
		{
		case EWeatherState::Clear:
			return TEXT("Clear");
		case EWeatherState::Overcast:
			return TEXT("Overcast");
		case EWeatherState::Snowfall:
			return TEXT("Snowfall");
		case EWeatherState::Blizzard:
			return TEXT("Blizzard");
		}
		return TEXT("Overcast");
	}

	bool WeatherFromString(const FString& Text, EWeatherState& OutWeather)
	{
		const FString Trimmed = Text.TrimStartAndEnd();
		const EWeatherState States[] = {EWeatherState::Clear, EWeatherState::Overcast, EWeatherState::Snowfall, EWeatherState::Blizzard};
		for (const EWeatherState State : States)
		{
			if (Trimmed.Equals(WeatherToString(State), ESearchCase::IgnoreCase))
			{
				OutWeather = State;
				return true;
			}
		}
		return false;
	}

	FString PhaseToString(ETimeOfDayPhase Phase)
	{
		switch (Phase)
		{
		case ETimeOfDayPhase::EarlyMorning:
			return TEXT("EarlyMorning");
		case ETimeOfDayPhase::Midday:
			return TEXT("Midday");
		case ETimeOfDayPhase::Afternoon:
			return TEXT("Afternoon");
		case ETimeOfDayPhase::Night:
			return TEXT("Night");
		}
		return TEXT("EarlyMorning");
	}
}
