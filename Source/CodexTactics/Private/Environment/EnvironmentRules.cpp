#include "Environment/EnvironmentRules.h"
#include "Dom/JsonObject.h"
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

		/** Save keys of the FDirectionalSnow pockets, in pocket order. */
		const TCHAR* const ObjectSnowKeys[FDirectionalSnow::PocketCount] = {TEXT("top"), TEXT("px"), TEXT("nx"), TEXT("py"), TEXT("ny")};

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

	float GetWeatherAmount(const FEnvironmentConfig& Config, const FEnvironmentState& State, EWeatherState Weather)
	{
		if (State.Weather == State.NextWeather)
		{
			return Weather == State.Weather ? 1.f : 0.f; // degenerate (never rolled): one weather, fully
		}
		const float Blend = GetTransitionBlend(Config, State);
		if (Weather == State.Weather)
		{
			return 1.f - Blend;
		}
		return Weather == State.NextWeather ? Blend : 0.f;
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
		State.SnowAmount = FMath::Clamp(Config.SnowCoverStart, 0.f, 1.f);
		State.ObjectSnow.Top = State.SnowAmount;
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

	float GetColdWeatherMultiplier(const FEnvironmentConfig& Config, EWeatherState Weather)
	{
		float Multiplier = 1.f;
		switch (Weather)
		{
		case EWeatherState::Clear:
			Multiplier = Config.ColdMultClear;
			break;
		case EWeatherState::Overcast:
			Multiplier = Config.ColdMultOvercast;
			break;
		case EWeatherState::Snowfall:
			Multiplier = Config.ColdMultSnowfall;
			break;
		case EWeatherState::Blizzard:
			Multiplier = Config.ColdMultBlizzard;
			break;
		}
		// Same floor as the UPROPERTY ClampMin; a JSON value is not clamped by the editor.
		return FMath::Max(0.f, Multiplier);
	}

	float GetColdPhaseMultiplier(const FEnvironmentConfig& Config, ETimeOfDayPhase Phase)
	{
		float Multiplier = 1.f;
		switch (Phase)
		{
		case ETimeOfDayPhase::EarlyMorning:
			Multiplier = Config.ColdMultEarlyMorning;
			break;
		case ETimeOfDayPhase::Midday:
			Multiplier = Config.ColdMultMidday;
			break;
		case ETimeOfDayPhase::Afternoon:
			Multiplier = Config.ColdMultAfternoon;
			break;
		case ETimeOfDayPhase::Night:
			Multiplier = Config.ColdMultNight;
			break;
		}
		return FMath::Max(0.f, Multiplier);
	}

	float GetBlendedColdWeatherMultiplier(const FEnvironmentConfig& Config, const FEnvironmentState& State)
	{
		// Section 17: the state's climate picks the table (Arctic = the top-level cold_mult_* exactly as before).
		return FMath::Lerp(GetColdWeatherMultiplier(Config, State.Climate, State.Weather), GetColdWeatherMultiplier(Config, State.Climate, State.NextWeather),
			GetTransitionBlend(Config, State));
	}

	bool ShouldApplyCold(const FEnvironmentConfig& Config, bool bLocked)
	{
		return !bLocked || Config.bColdOnLockedLevels;
	}

	float GetSnowCoverRate(const FEnvironmentConfig& Config, EWeatherState Weather)
	{
		switch (Weather)
		{
		case EWeatherState::Snowfall:
			return FMath::Max(0.f, Config.SnowCoverRateSnowfall);
		case EWeatherState::Blizzard:
			return FMath::Max(0.f, Config.SnowCoverRateBlizzard);
		default:
			return 0.f; // Clear / Overcast: the cover holds (never recedes).
		}
	}

	float GetBlendedSnowCoverRate(const FEnvironmentConfig& Config, const FEnvironmentState& State)
	{
		if (State.Climate == EClimateProfile::Temperate)
		{
			return 0.f; // section 17: the Snowfall / Blizzard rungs are rain and storm there - nothing whitens
		}
		return FMath::Lerp(GetSnowCoverRate(Config, State.Weather), GetSnowCoverRate(Config, State.NextWeather), GetTransitionBlend(Config, State));
	}

	float StepSnowCover(const FEnvironmentConfig& Config, FEnvironmentState& State, float DeltaSeconds)
	{
		const float Before = FMath::Clamp(State.SnowAmount, 0.f, 1.f);
		if (State.bLocked || DeltaSeconds <= 0.f)
		{
			return 0.f;
		}
		// Max(Before, ...): never below what was there, even with a broken (negative) rate.
		State.SnowAmount = FMath::Clamp(Before + GetBlendedSnowCoverRate(Config, State) * DeltaSeconds, Before, 1.f);
		return State.SnowAmount - Before;
	}

	float GetInitialSnowCover(const FEnvironmentConfig& Config, const FEnvironmentLevelSettings& Level)
	{
		const float Default = Level.Climate == EClimateProfile::Temperate ? 0.f : Config.SnowCoverStart;
		return FMath::Clamp(Level.bHasSnowCover ? Level.SnowCover : Default, 0.f, 1.f);
	}

	float GetSideWindFactor(const FVector2D& WindDirection, int32 PocketIndex)
	{
		if (PocketIndex < 1 || PocketIndex >= FDirectionalSnow::PocketCount)
		{
			return 0.f;
		}
		const FVector2D Wind = WindDirection.GetSafeNormal();
		const FVector Side = FDirectionalSnow::GetPocketDirection(PocketIndex);
		return FMath::Max(0.f, -static_cast<float>(Wind.X * Side.X + Wind.Y * Side.Y));
	}

	float GetSunMeltFactor(const FEnvironmentConfig& Config, float SunElevationDegrees)
	{
		return FMath::Clamp(SunElevationDegrees / FMath::Max(0.1f, Config.ObjectSnowMeltFullSunElevation), 0.f, 1.f);
	}

	float GetObjectSnowMeltRate(const FEnvironmentConfig& Config, const FEnvironmentState& State, float SunElevationDegrees)
	{
		return FMath::Max(0.f, Config.ObjectSnowMeltRate) * GetWeatherAmount(Config, State, EWeatherState::Clear)
			* GetSunMeltFactor(Config, SunElevationDegrees);
	}

	void StepDirectionalSnow(FDirectionalSnow& Pockets, const FDirectionalSnowInputs& Inputs, float DeltaSeconds)
	{
		if (DeltaSeconds <= 0.f)
		{
			return;
		}
		const float Grow = FMath::Max(0.f, Inputs.GrowRate) * DeltaSeconds;
		const float SideGrow = Grow * FMath::Max(0.f, Inputs.SideRateScale) * FMath::Clamp(Inputs.WindStrength, 0.f, 1.f);
		const float Melt = FMath::Max(0.f, Inputs.MeltRate) * DeltaSeconds;
		const float Clear = FMath::Max(0.f, Inputs.ClearRate) * DeltaSeconds;
		const float TopFloor = FMath::Clamp(Inputs.TopFloor, 0.f, 1.f);
		for (int32 Index = 0; Index < FDirectionalSnow::PocketCount; ++Index)
		{
			float Value = FMath::Clamp(Pockets.Get(Index), 0.f, 1.f);
			Value = FMath::Min(1.f, Value + (Index == 0 ? Grow : SideGrow * GetSideWindFactor(Inputs.WindDirection, Index)));
			if (Melt > 0.f)
			{
				// Down to the floor, never through it; a pocket already under its floor (it never grew that far) stays where it is.
				const float Floor = Index == 0 ? TopFloor : 0.f;
				if (Value > Floor)
				{
					Value = FMath::Max(Floor, Value - Melt);
				}
			}
			Value = FMath::Max(0.f, Value - Clear);
			Pockets.Set(Index, Value);
		}
	}

	FDirectionalSnowInputs MakeObjectSnowInputs(const FEnvironmentConfig& Config, const FEnvironmentState& State, const FVector2D& WorldWindDirection,
		float WindStrength, float SunElevationDegrees)
	{
		FDirectionalSnowInputs Inputs;
		Inputs.GrowRate = GetBlendedSnowCoverRate(Config, State);
		Inputs.WindDirection = WorldWindDirection;
		Inputs.WindStrength = WindStrength;
		Inputs.SideRateScale = Config.ObjectSnowSideRateScale;
		Inputs.MeltRate = GetObjectSnowMeltRate(Config, State, SunElevationDegrees);
		Inputs.TopFloor = FMath::Clamp(Config.ObjectSnowMeltFloor, 0.f, 1.f) * FMath::Clamp(State.SnowAmount, 0.f, 1.f);
		return Inputs;
	}

	void StepObjectSnow(const FEnvironmentConfig& Config, FEnvironmentState& State, float DeltaSeconds, const FVector2D& WorldWindDirection,
		float WindStrength, float SunElevationDegrees)
	{
		if (State.bLocked || DeltaSeconds <= 0.f)
		{
			return;
		}
		StepDirectionalSnow(State.ObjectSnow, MakeObjectSnowInputs(Config, State, WorldWindDirection, WindStrength, SunElevationDegrees), DeltaSeconds);
	}

	bool SeedObjectSnow(FEnvironmentState& State)
	{
		if (!State.ObjectSnow.IsZero() || State.SnowAmount <= 0.f)
		{
			return false;
		}
		State.ObjectSnow.Top = FMath::Clamp(State.SnowAmount, 0.f, 1.f);
		return true;
	}

	TSharedRef<FJsonObject> StateToSaveJson(const FEnvironmentState& State)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetNumberField(TEXT("hour"), State.Hour);
		Json->SetStringField(TEXT("weather"), WeatherToString(State.Weather));
		Json->SetStringField(TEXT("next_weather"), WeatherToString(State.NextWeather));
		Json->SetNumberField(TEXT("weather_elapsed"), State.WeatherElapsedSeconds);
		Json->SetNumberField(TEXT("weather_dwell"), State.WeatherDwellSeconds);
		Json->SetNumberField(TEXT("seed"), State.RandomSeed);
		Json->SetBoolField(TEXT("locked"), State.bLocked);
		Json->SetNumberField(TEXT("snow_amount"), State.SnowAmount);
		TSharedRef<FJsonObject> Pockets = MakeShared<FJsonObject>();
		for (int32 Index = 0; Index < FDirectionalSnow::PocketCount; ++Index)
		{
			Pockets->SetNumberField(ObjectSnowKeys[Index], State.ObjectSnow.Get(Index));
		}
		Json->SetObjectField(TEXT("object_snow"), Pockets);
		// Section 17: the climate profile and its wet ground (0 / 0 on arctic levels).
		Json->SetStringField(TEXT("climate"), ClimateToString(State.Climate));
		Json->SetNumberField(TEXT("wetness"), State.Wetness);
		Json->SetNumberField(TEXT("puddles"), State.PuddleAmount);
		return Json;
	}

	FEnvironmentState StateFromSaveJson(const FJsonObject& Json, const FEnvironmentState& Fallback)
	{
		FEnvironmentState State = Fallback;
		double Number = 0.0;
		if (Json.TryGetNumberField(TEXT("hour"), Number))
		{
			State.Hour = WrapHour(static_cast<float>(Number));
		}
		FString Name;
		EWeatherState Weather = EWeatherState::Overcast;
		if (Json.TryGetStringField(TEXT("weather"), Name) && WeatherFromString(Name, Weather))
		{
			State.Weather = Weather;
		}
		if (Json.TryGetStringField(TEXT("next_weather"), Name) && WeatherFromString(Name, Weather))
		{
			State.NextWeather = Weather;
		}
		if (Json.TryGetNumberField(TEXT("weather_elapsed"), Number))
		{
			State.WeatherElapsedSeconds = FMath::Max(0.f, static_cast<float>(Number));
		}
		if (Json.TryGetNumberField(TEXT("weather_dwell"), Number))
		{
			State.WeatherDwellSeconds = FMath::Max(0.f, static_cast<float>(Number));
		}
		if (Json.TryGetNumberField(TEXT("seed"), Number))
		{
			State.RandomSeed = static_cast<int32>(Number);
		}
		bool bLocked = false;
		if (Json.TryGetBoolField(TEXT("locked"), bLocked))
		{
			State.bLocked = bLocked;
		}
		if (Json.TryGetNumberField(TEXT("snow_amount"), Number))
		{
			State.SnowAmount = FMath::Clamp(static_cast<float>(Number), 0.f, 1.f);
		}
		const TSharedPtr<FJsonObject>* Pockets = nullptr;
		if (Json.TryGetObjectField(TEXT("object_snow"), Pockets) && Pockets && Pockets->IsValid())
		{
			for (int32 Index = 0; Index < FDirectionalSnow::PocketCount; ++Index)
			{
				if ((*Pockets)->TryGetNumberField(ObjectSnowKeys[Index], Number))
				{
					State.ObjectSnow.Set(Index, FMath::Clamp(static_cast<float>(Number), 0.f, 1.f));
				}
			}
		}
		else
		{
			// A save of an older build: the objects carried the ground cover on their tops.
			State.ObjectSnow = FDirectionalSnow();
			SeedObjectSnow(State);
		}
		EClimateProfile Climate = EClimateProfile::Arctic;
		if (Json.TryGetStringField(TEXT("climate"), Name) && ClimateFromString(Name, Climate))
		{
			State.Climate = Climate;
		}
		if (Json.TryGetNumberField(TEXT("wetness"), Number))
		{
			State.Wetness = FMath::Clamp(static_cast<float>(Number), 0.f, 1.f);
		}
		if (Json.TryGetNumberField(TEXT("puddles"), Number))
		{
			State.PuddleAmount = FMath::Clamp(static_cast<float>(Number), 0.f, 1.f);
		}
		return State;
	}

	// --- Section 17: climate profiles ---

	FString ClimateToString(EClimateProfile Climate)
	{
		return Climate == EClimateProfile::Temperate ? TEXT("temperate") : TEXT("arctic");
	}

	bool IsArcticBlizzardClimate(EClimateProfile Climate)
	{
		return Climate == EClimateProfile::Arctic;
	}

	bool ClimateFromString(const FString& Text, EClimateProfile& OutClimate)
	{
		const FString Trimmed = Text.TrimStartAndEnd();
		if (Trimmed.Equals(TEXT("arctic"), ESearchCase::IgnoreCase))
		{
			OutClimate = EClimateProfile::Arctic;
			return true;
		}
		if (Trimmed.Equals(TEXT("temperate"), ESearchCase::IgnoreCase))
		{
			OutClimate = EClimateProfile::Temperate;
			return true;
		}
		return false;
	}

	FString GetWeatherDisplayName(EClimateProfile Climate, EWeatherState Weather)
	{
		if (Climate != EClimateProfile::Temperate)
		{
			return WeatherToString(Weather);
		}
		switch (Weather)
		{
		case EWeatherState::Clear:
			return TEXT("SunnyDay");
		case EWeatherState::Overcast:
			return TEXT("Overcast");
		case EWeatherState::Snowfall:
			return TEXT("Rain");
		case EWeatherState::Blizzard:
			return TEXT("Storm");
		}
		return TEXT("Overcast");
	}

	bool WeatherFromString(EClimateProfile Climate, const FString& Text, EWeatherState& OutWeather)
	{
		if (WeatherFromString(Text, OutWeather))
		{
			return true;
		}
		if (Climate != EClimateProfile::Temperate)
		{
			return false;
		}
		const FString Trimmed = Text.TrimStartAndEnd();
		if (Trimmed.Equals(TEXT("Sunny"), ESearchCase::IgnoreCase) || Trimmed.Equals(TEXT("SunnyDay"), ESearchCase::IgnoreCase))
		{
			OutWeather = EWeatherState::Clear;
			return true;
		}
		if (Trimmed.Equals(TEXT("Rain"), ESearchCase::IgnoreCase))
		{
			OutWeather = EWeatherState::Snowfall;
			return true;
		}
		if (Trimmed.Equals(TEXT("Storm"), ESearchCase::IgnoreCase))
		{
			OutWeather = EWeatherState::Blizzard;
			return true;
		}
		return false;
	}

	float GetColdWeatherMultiplier(const FEnvironmentConfig& Config, EClimateProfile Climate, EWeatherState Weather)
	{
		if (Climate != EClimateProfile::Temperate)
		{
			return GetColdWeatherMultiplier(Config, Weather);
		}
		const FTemperateClimateConfig& T = Config.Temperate;
		float Multiplier = 1.f;
		switch (Weather)
		{
		case EWeatherState::Clear:
			Multiplier = T.ColdMultSunny;
			break;
		case EWeatherState::Overcast:
			Multiplier = T.ColdMultOvercast;
			break;
		case EWeatherState::Snowfall:
			Multiplier = T.ColdMultRain;
			break;
		case EWeatherState::Blizzard:
			Multiplier = T.ColdMultStorm;
			break;
		}
		return FMath::Max(0.f, Multiplier);
	}

	float GetColdPhaseMultiplier(const FEnvironmentConfig& Config, EClimateProfile Climate, ETimeOfDayPhase Phase)
	{
		if (Climate != EClimateProfile::Temperate)
		{
			return GetColdPhaseMultiplier(Config, Phase);
		}
		const FTemperateClimateConfig& T = Config.Temperate;
		float Multiplier = 1.f;
		switch (Phase)
		{
		case ETimeOfDayPhase::EarlyMorning:
			Multiplier = T.ColdMultEarlyMorning;
			break;
		case ETimeOfDayPhase::Midday:
			Multiplier = T.ColdMultMidday;
			break;
		case ETimeOfDayPhase::Afternoon:
			Multiplier = T.ColdMultAfternoon;
			break;
		case ETimeOfDayPhase::Night:
			Multiplier = T.ColdMultNight;
			break;
		}
		return FMath::Max(0.f, Multiplier);
	}

	bool IsRosterActive(const FEnvironmentConfig& Config, EClimateProfile Climate)
	{
		return Climate != EClimateProfile::Temperate || Config.Temperate.bRosterEnabled;
	}

	bool IsFallingSnowAllowed(const FEnvironmentConfig& Config, EClimateProfile Climate)
	{
		return Climate != EClimateProfile::Temperate || Config.Temperate.bFallingSnow;
	}

	float GetRainIntensity(const FEnvironmentConfig& Config, const FEnvironmentState& State)
	{
		if (State.Climate != EClimateProfile::Temperate)
		{
			return 0.f;
		}
		const float Rain = FMath::Clamp(Config.Temperate.RainIntensityRain, 0.f, 1.f) * GetWeatherAmount(Config, State, EWeatherState::Snowfall);
		const float Storm = FMath::Clamp(Config.Temperate.RainIntensityStorm, 0.f, 1.f) * GetWeatherAmount(Config, State, EWeatherState::Blizzard);
		return FMath::Clamp(Rain + Storm, 0.f, 1.f);
	}

	float ComputeIsRaining(float RainIntensity)
	{
		return FMath::Clamp(RainIntensity / 0.25f, 0.f, 1.f);
	}

	bool ShouldShowScreenDrops(const FEnvironmentConfig& Config, const FEnvironmentState& State)
	{
		return State.Climate == EClimateProfile::Temperate && Config.Temperate.bScreenDrops && ComputeIsRaining(GetRainIntensity(Config, State)) >= 0.5f;
	}

	float GetSnowingAmount(const FEnvironmentConfig& Config, const FEnvironmentState& State)
	{
		if (State.Climate == EClimateProfile::Temperate)
		{
			return 0.f;
		}
		return FMath::Clamp(GetWeatherAmount(Config, State, EWeatherState::Snowfall) + GetWeatherAmount(Config, State, EWeatherState::Blizzard), 0.f, 1.f);
	}

	float GetWetnessRate(const FEnvironmentConfig& Config, const FEnvironmentState& State, float SunElevationDegrees)
	{
		if (State.Climate != EClimateProfile::Temperate)
		{
			return 0.f;
		}
		const FTemperateClimateConfig& T = Config.Temperate;
		const float Overcast = FMath::Max(0.f, T.DryingRateOvercast);
		const float SunRamp = FMath::Clamp(SunElevationDegrees / FMath::Max(0.1f, T.DryingFullSunElevation), 0.f, 1.f);
		const float Sunny = FMath::Lerp(Overcast, FMath::Max(0.f, T.DryingRateSunny), SunRamp);
		return GetWeatherAmount(Config, State, EWeatherState::Snowfall) * FMath::Max(0.f, T.WetnessRateRain)
			+ GetWeatherAmount(Config, State, EWeatherState::Blizzard) * FMath::Max(0.f, T.WetnessRateStorm)
			- GetWeatherAmount(Config, State, EWeatherState::Overcast) * Overcast
			- GetWeatherAmount(Config, State, EWeatherState::Clear) * Sunny;
	}

	float GetPuddleTarget(const FEnvironmentConfig& Config, float Wetness)
	{
		const float Threshold = FMath::Clamp(Config.Temperate.PuddleWetnessThreshold, 0.f, 0.99f);
		return FMath::Clamp((Wetness - Threshold) / (1.f - Threshold), 0.f, 1.f);
	}

	void StepWetness(const FEnvironmentConfig& Config, FEnvironmentState& State, float DeltaSeconds, float SunElevationDegrees)
	{
		if (State.Climate != EClimateProfile::Temperate || State.bLocked || DeltaSeconds <= 0.f)
		{
			return;
		}
		const FTemperateClimateConfig& T = Config.Temperate;
		const float Rate = GetWetnessRate(Config, State, SunElevationDegrees);
		State.Wetness = FMath::Clamp(State.Wetness + Rate * DeltaSeconds, 0.f, 1.f);
		const float Target = GetPuddleTarget(Config, State.Wetness);
		const float Puddles = FMath::Clamp(State.PuddleAmount, 0.f, 1.f);
		if (Target > Puddles)
		{
			// Standing water only collects while the ground gets wetter (it rains); a drying ground never fills a puddle.
			State.PuddleAmount = Rate > 0.f ? FMath::Min(Target, Puddles + FMath::Max(0.f, T.PuddleFillRate) * DeltaSeconds) : Puddles;
		}
		else
		{
			State.PuddleAmount = FMath::Max(Target, Puddles - FMath::Max(0.f, T.PuddleDrainRate) * DeltaSeconds);
		}
	}

	void GetInitialWetness(const FEnvironmentConfig& Config, const FEnvironmentLevelSettings& Level, EWeatherState Weather, float& OutWetness, float& OutPuddles)
	{
		OutWetness = 0.f;
		OutPuddles = 0.f;
		if (Level.Climate != EClimateProfile::Temperate)
		{
			return;
		}
		const FTemperateClimateConfig& T = Config.Temperate;
		if (Weather == EWeatherState::Snowfall)
		{
			OutWetness = T.StartWetnessRain;
			OutPuddles = T.StartPuddlesRain;
		}
		else if (Weather == EWeatherState::Blizzard)
		{
			OutWetness = T.StartWetnessStorm;
			OutPuddles = T.StartPuddlesStorm;
		}
		OutWetness = FMath::Clamp(Level.bHasWetness ? Level.Wetness : OutWetness, 0.f, 1.f);
		OutPuddles = FMath::Clamp(Level.bHasPuddles ? Level.Puddles : OutPuddles, 0.f, 1.f);
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
