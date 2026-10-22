#include "Data/EnvironmentTuning.h"

#include "CodexTactics.h"
#include "Data/LevelJsonRules.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace EnvironmentTuning
{
	static FEnvironmentConfig Current;

	struct FFloatKey
	{
		const TCHAR* Key;
		float FEnvironmentConfig::* Member;
		/** Lower / upper clamp (mirrors the UPROPERTY meta of the field). */
		float Min;
		float Max;
	};

	static const FFloatKey FloatKeys[] = {
		{ TEXT("early_morning_start_hour"), &FEnvironmentConfig::EarlyMorningStartHour, 0.f, 24.f },
		{ TEXT("midday_start_hour"), &FEnvironmentConfig::MiddayStartHour, 0.f, 24.f },
		{ TEXT("afternoon_start_hour"), &FEnvironmentConfig::AfternoonStartHour, 0.f, 24.f },
		{ TEXT("night_start_hour"), &FEnvironmentConfig::NightStartHour, 0.f, 24.f },
		{ TEXT("early_morning_real_seconds"), &FEnvironmentConfig::EarlyMorningRealSeconds, 1.f, BIG_NUMBER },
		{ TEXT("midday_real_seconds"), &FEnvironmentConfig::MiddayRealSeconds, 1.f, BIG_NUMBER },
		{ TEXT("afternoon_real_seconds"), &FEnvironmentConfig::AfternoonRealSeconds, 1.f, BIG_NUMBER },
		{ TEXT("night_real_seconds"), &FEnvironmentConfig::NightRealSeconds, 1.f, BIG_NUMBER },
		{ TEXT("default_start_hour"), &FEnvironmentConfig::DefaultStartHour, 0.f, 24.f },
		{ TEXT("weather_dwell_phase_fraction"), &FEnvironmentConfig::WeatherDwellPhaseFraction, 0.05f, BIG_NUMBER },
		{ TEXT("weather_transition_seconds"), &FEnvironmentConfig::WeatherTransitionSeconds, 0.f, BIG_NUMBER },
		{ TEXT("start_weight_clear"), &FEnvironmentConfig::StartWeightClear, 0.f, BIG_NUMBER },
		{ TEXT("start_weight_overcast"), &FEnvironmentConfig::StartWeightOvercast, 0.f, BIG_NUMBER },
		{ TEXT("start_weight_snowfall"), &FEnvironmentConfig::StartWeightSnowfall, 0.f, BIG_NUMBER },
		{ TEXT("start_weight_blizzard"), &FEnvironmentConfig::StartWeightBlizzard, 0.f, BIG_NUMBER },
		{ TEXT("overcast_step_up_chance"), &FEnvironmentConfig::OvercastStepUpChance, 0.f, 1.f },
		{ TEXT("snowfall_step_up_chance"), &FEnvironmentConfig::SnowfallStepUpChance, 0.f, 1.f },
		// Phase 2: cold multipliers (open-air cold x weather x phase).
		{ TEXT("cold_mult_clear"), &FEnvironmentConfig::ColdMultClear, 0.f, BIG_NUMBER },
		{ TEXT("cold_mult_overcast"), &FEnvironmentConfig::ColdMultOvercast, 0.f, BIG_NUMBER },
		{ TEXT("cold_mult_snowfall"), &FEnvironmentConfig::ColdMultSnowfall, 0.f, BIG_NUMBER },
		{ TEXT("cold_mult_blizzard"), &FEnvironmentConfig::ColdMultBlizzard, 0.f, BIG_NUMBER },
		{ TEXT("cold_mult_early_morning"), &FEnvironmentConfig::ColdMultEarlyMorning, 0.f, BIG_NUMBER },
		{ TEXT("cold_mult_midday"), &FEnvironmentConfig::ColdMultMidday, 0.f, BIG_NUMBER },
		{ TEXT("cold_mult_afternoon"), &FEnvironmentConfig::ColdMultAfternoon, 0.f, BIG_NUMBER },
		{ TEXT("cold_mult_night"), &FEnvironmentConfig::ColdMultNight, 0.f, BIG_NUMBER },
		// Phase 4: persistent snow cover (per REAL second while it snows; it never recedes).
		{ TEXT("snow_cover_rate_snowfall"), &FEnvironmentConfig::SnowCoverRateSnowfall, 0.f, BIG_NUMBER },
		{ TEXT("snow_cover_rate_blizzard"), &FEnvironmentConfig::SnowCoverRateBlizzard, 0.f, BIG_NUMBER },
		{ TEXT("snow_cover_start"), &FEnvironmentConfig::SnowCoverStart, 0.f, 1.f } };

	static const TCHAR* ColdOnLockedKey = TEXT("cold_on_locked_levels");

	// --- Roster (design section 4.2) ---

	struct FRosterFloatKey
	{
		const TCHAR* Key;
		float FEnvironmentRosterConfig::* Member;
		float Min;
		float Max;
	};

	static const FRosterFloatKey RosterFloatKeys[] = {
		{ TEXT("half_below"), &FEnvironmentRosterConfig::HalfBelow, 0.f, 4.f },
		{ TEXT("all_below"), &FEnvironmentRosterConfig::AllBelow, 0.f, 4.f },
		{ TEXT("bonus_above"), &FEnvironmentRosterConfig::BonusAbove, 0.f, 4.f },
		{ TEXT("bonus_multiplier"), &FEnvironmentRosterConfig::BonusMultiplier, 1.f, BIG_NUMBER } };

	/** Weather keys of a suitability row, in EWeatherState order; each holds 4 scores in ETimeOfDayPhase order. */
	static const TCHAR* const WeatherKeys[4] = { TEXT("Clear"), TEXT("Overcast"), TEXT("Snowfall"), TEXT("Blizzard") };

	static constexpr int32 PhaseCount = 4;
	static constexpr int32 ScoreCount = 16;

	/** Level JSON enemy type name of an archetype (the inverse of LevelJsonRules::ParseEnemyType; see Content/Data/LevelJson). */
	static FString EnemyTypeName(EEnemyArchetype Type)
	{
		switch (Type)
		{
		case EEnemyArchetype::FrostHound: return TEXT("HOUND");
		case EEnemyArchetype::Spitter: return TEXT("SPITTER");
		case EEnemyArchetype::Brute: return TEXT("BRUTE");
		case EEnemyArchetype::Frostbitten: return TEXT("FROSTBITTEN");
		case EEnemyArchetype::Cutter: return TEXT("CUTTER");
		case EEnemyArchetype::CryoDrone: return TEXT("CRYO_DRONE");
		case EEnemyArchetype::Marksman: return TEXT("MARKSMAN");
		default: return TEXT("BASE");
		}
	}

	/** One suitability row; false (with a warning) when the type is unknown or a weather is missing / not 4 numbers. */
	static bool ParseSuitabilityRow(const FString& TypeName, const FJsonObject& RowJson, FEnvironmentSuitabilityRow& OutRow)
	{
		EEnemyArchetype Type = EEnemyArchetype::Base;
		if (!LevelJsonRules::ParseEnemyType(TypeName, Type))
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("environment.json roster.suitability: unknown enemy type '%s', row skipped"), *TypeName);
			return false;
		}
		TArray<float> Scores;
		Scores.Reserve(ScoreCount);
		for (const TCHAR* WeatherKey : WeatherKeys)
		{
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			if (!RowJson.TryGetArrayField(WeatherKey, Values) || Values->Num() != PhaseCount)
			{
				UE_LOG(LogCodexTactics, Warning, TEXT("environment.json roster.suitability.%s: '%s' missing or not %d numbers, row rejected"),
					*TypeName, WeatherKey, PhaseCount);
				return false;
			}
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				double Score = 0.0;
				if (!Value.IsValid() || !Value->TryGetNumber(Score))
				{
					UE_LOG(LogCodexTactics, Warning, TEXT("environment.json roster.suitability.%s.%s: non-numeric score, row rejected"), *TypeName, WeatherKey);
					return false;
				}
				Scores.Add(FMath::Clamp(static_cast<float>(Score), 0.f, 4.f));
			}
		}
		OutRow.Type = Type;
		OutRow.Scores = MoveTemp(Scores);
		return true;
	}

	/** Roster block of Json over Roster (keys absent keep the current value; a present list replaces it). */
	static void ParseRoster(const FJsonObject& RosterJson, FEnvironmentRosterConfig& Roster)
	{
		bool bEnabled = true;
		if (RosterJson.TryGetBoolField(TEXT("enabled"), bEnabled))
		{
			Roster.bEnabled = bEnabled;
		}
		for (const FRosterFloatKey& Entry : RosterFloatKeys)
		{
			double Value = 0.0;
			if (RosterJson.TryGetNumberField(Entry.Key, Value))
			{
				Roster.*Entry.Member = FMath::Clamp(static_cast<float>(Value), Entry.Min, Entry.Max);
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* PairValues = nullptr;
		if (RosterJson.TryGetArrayField(TEXT("pairs"), PairValues))
		{
			Roster.Pairs.Reset();
			for (const TSharedPtr<FJsonValue>& PairValue : *PairValues)
			{
				const TSharedPtr<FJsonObject>* PairJson = nullptr;
				FString FromName;
				FString ToName;
				FEnvironmentRosterPair Pair;
				if (!PairValue.IsValid() || !PairValue->TryGetObject(PairJson) || !(*PairJson)->TryGetStringField(TEXT("from"), FromName)
					|| !(*PairJson)->TryGetStringField(TEXT("to"), ToName)
					|| !LevelJsonRules::ParseEnemyType(FromName, Pair.From) || !LevelJsonRules::ParseEnemyType(ToName, Pair.To))
				{
					UE_LOG(LogCodexTactics, Warning, TEXT("environment.json roster.pairs: a pair needs known \"from\" and \"to\" enemy types, skipped"));
					continue;
				}
				double Ratio = 1.0;
				if ((*PairJson)->TryGetNumberField(TEXT("ratio"), Ratio))
				{
					Pair.Ratio = FMath::Max(1, FMath::RoundToInt(static_cast<float>(Ratio)));
				}
				Roster.Pairs.Add(Pair);
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* BonusValues = nullptr;
		if (RosterJson.TryGetArrayField(TEXT("bonus_types"), BonusValues))
		{
			Roster.BonusTypes.Reset();
			for (const TSharedPtr<FJsonValue>& BonusValue : *BonusValues)
			{
				FString Name;
				EEnemyArchetype Type = EEnemyArchetype::Base;
				if (BonusValue.IsValid() && BonusValue->TryGetString(Name) && LevelJsonRules::ParseEnemyType(Name, Type))
				{
					Roster.BonusTypes.AddUnique(Type);
				}
				else
				{
					UE_LOG(LogCodexTactics, Warning, TEXT("environment.json roster.bonus_types: unknown enemy type '%s', skipped"), *Name);
				}
			}
		}

		const TSharedPtr<FJsonObject>* SuitabilityJson = nullptr;
		if (RosterJson.TryGetObjectField(TEXT("suitability"), SuitabilityJson))
		{
			Roster.Suitability.Reset();
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*SuitabilityJson)->Values)
			{
				const TSharedPtr<FJsonObject>* RowJson = nullptr;
				FEnvironmentSuitabilityRow Row;
				if (!Entry.Value.IsValid() || !Entry.Value->TryGetObject(RowJson))
				{
					UE_LOG(LogCodexTactics, Warning, TEXT("environment.json roster.suitability.%s: not an object, row skipped"), *Entry.Key);
				}
				else if (ParseSuitabilityRow(Entry.Key, **RowJson, Row))
				{
					// A type listed twice (e.g. HOUND and FROST_HOUND): the later row wins.
					Roster.Suitability.RemoveAll([&Row](const FEnvironmentSuitabilityRow& Existing) { return Existing.Type == Row.Type; });
					Roster.Suitability.Add(MoveTemp(Row));
				}
			}
		}
	}

	static TSharedRef<FJsonObject> RosterToJson(const FEnvironmentRosterConfig& Roster)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetBoolField(TEXT("enabled"), Roster.bEnabled);
		for (const FRosterFloatKey& Entry : RosterFloatKeys)
		{
			Json->SetNumberField(Entry.Key, Roster.*Entry.Member);
		}

		TArray<TSharedPtr<FJsonValue>> Pairs;
		for (const FEnvironmentRosterPair& Pair : Roster.Pairs)
		{
			const TSharedRef<FJsonObject> PairJson = MakeShared<FJsonObject>();
			PairJson->SetStringField(TEXT("from"), EnemyTypeName(Pair.From));
			PairJson->SetStringField(TEXT("to"), EnemyTypeName(Pair.To));
			PairJson->SetNumberField(TEXT("ratio"), Pair.Ratio);
			Pairs.Add(MakeShared<FJsonValueObject>(PairJson));
		}
		Json->SetArrayField(TEXT("pairs"), Pairs);

		TArray<TSharedPtr<FJsonValue>> Bonus;
		for (const EEnemyArchetype Type : Roster.BonusTypes)
		{
			Bonus.Add(MakeShared<FJsonValueString>(EnemyTypeName(Type)));
		}
		Json->SetArrayField(TEXT("bonus_types"), Bonus);

		const TSharedRef<FJsonObject> Suitability = MakeShared<FJsonObject>();
		for (const FEnvironmentSuitabilityRow& Row : Roster.Suitability)
		{
			if (Row.Scores.Num() != ScoreCount)
			{
				continue;
			}
			const TSharedRef<FJsonObject> RowJson = MakeShared<FJsonObject>();
			for (int32 Weather = 0; Weather < 4; ++Weather)
			{
				TArray<TSharedPtr<FJsonValue>> Scores;
				for (int32 Phase = 0; Phase < PhaseCount; ++Phase)
				{
					Scores.Add(MakeShared<FJsonValueNumber>(Row.Scores[Weather * PhaseCount + Phase]));
				}
				RowJson->SetArrayField(WeatherKeys[Weather], Scores);
			}
			Suitability->SetObjectField(EnemyTypeName(Row.Type), RowJson);
		}
		Json->SetObjectField(TEXT("suitability"), Suitability);
		return Json;
	}
}

FString EnvironmentTuning::GetDefaultPath()
{
	return FPaths::ProjectContentDir() / TEXT("Data/Environment/environment.json");
}

const FEnvironmentConfig& EnvironmentTuning::Get()
{
	return Current;
}

void EnvironmentTuning::Set(const FEnvironmentConfig& Config)
{
	Current = Config;
}

FEnvironmentConfig EnvironmentTuning::FromJson(const FJsonObject& Json, const FEnvironmentConfig& Base)
{
	FEnvironmentConfig Config = Base;
	for (const FFloatKey& Entry : FloatKeys)
	{
		double Value = 0.0;
		if (Json.TryGetNumberField(Entry.Key, Value))
		{
			Config.*Entry.Member = FMath::Clamp(static_cast<float>(Value), Entry.Min, Entry.Max);
		}
	}
	bool bColdOnLocked = false;
	if (Json.TryGetBoolField(ColdOnLockedKey, bColdOnLocked))
	{
		Config.bColdOnLockedLevels = bColdOnLocked;
	}
	const TSharedPtr<FJsonObject>* RosterJson = nullptr;
	if (Json.TryGetObjectField(TEXT("roster"), RosterJson))
	{
		ParseRoster(**RosterJson, Config.Roster);
	}
	return Config;
}

TSharedRef<FJsonObject> EnvironmentTuning::ToJson(const FEnvironmentConfig& Config)
{
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	for (const FFloatKey& Entry : FloatKeys)
	{
		Json->SetNumberField(Entry.Key, Config.*Entry.Member);
	}
	Json->SetBoolField(ColdOnLockedKey, Config.bColdOnLockedLevels);
	Json->SetObjectField(TEXT("roster"), RosterToJson(Config.Roster));
	return Json;
}

bool EnvironmentTuning::ApplyFile(const FString& Path)
{
	FString Text;
	TSharedPtr<FJsonObject> Json;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
	{
		return false;
	}
	Current = FromJson(*Json);
	UE_LOG(LogCodexTactics, Display, TEXT("Environment from %s: phases %.0f/%.0f/%.0f/%.0f h, %.0f/%.0f/%.0f/%.0f s, weather dwell %.2f of phase, blend %.0f s"),
		*Path, Current.EarlyMorningStartHour, Current.MiddayStartHour, Current.AfternoonStartHour, Current.NightStartHour,
		Current.EarlyMorningRealSeconds, Current.MiddayRealSeconds, Current.AfternoonRealSeconds, Current.NightRealSeconds,
		Current.WeatherDwellPhaseFraction, Current.WeatherTransitionSeconds);
	return true;
}

namespace EnvironmentTuning
{
	static void DumpCommand(const TArray<FString>& Args)
	{
		FString Out;
		FJsonSerializer::Serialize(ToJson(Current), TJsonWriterFactory<>::Create(&Out));
		UE_LOG(LogCodexTactics, Display, TEXT("Environment: %s"), *Out);
		const FEnvironmentConfig& C = Current;
		UE_LOG(LogCodexTactics, Display, TEXT("Environment cold: weather x%.2f/%.2f/%.2f/%.2f (Clear/Overcast/Snowfall/Blizzard), phase x%.2f/%.2f/%.2f/%.2f (EarlyMorning/Midday/Afternoon/Night), on locked levels %s"),
			C.ColdMultClear, C.ColdMultOvercast, C.ColdMultSnowfall, C.ColdMultBlizzard,
			C.ColdMultEarlyMorning, C.ColdMultMidday, C.ColdMultAfternoon, C.ColdMultNight, C.bColdOnLockedLevels ? TEXT("yes") : TEXT("no"));
		FString Pairs;
		for (const FEnvironmentRosterPair& Pair : C.Roster.Pairs)
		{
			Pairs += FString::Printf(TEXT(" %s->%dx%s"), *EnemyTypeName(Pair.From), Pair.Ratio, *EnemyTypeName(Pair.To));
		}
		FString Bonus;
		for (const EEnemyArchetype Type : C.Roster.BonusTypes)
		{
			Bonus += TEXT(" ") + EnemyTypeName(Type);
		}
		FString Rows;
		for (const FEnvironmentSuitabilityRow& Row : C.Roster.Suitability)
		{
			Rows += TEXT(" ") + EnemyTypeName(Row.Type);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Environment roster: %s, half < %.2f, all < %.2f, bonus > %.2f x%.2f; pairs:%s; bonus types:%s; %d suitability rows:%s"),
			C.Roster.bEnabled ? TEXT("enabled") : TEXT("disabled"), C.Roster.HalfBelow, C.Roster.AllBelow, C.Roster.BonusAbove, C.Roster.BonusMultiplier,
			*Pairs, *Bonus, C.Roster.Suitability.Num(), *Rows);
	}

	static FAutoConsoleCommand DumpEnvironment(TEXT("CodexTactics.DumpEnvironment"), TEXT("Logs the day cycle / weather values in force (environment.json)."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&DumpCommand));
}
