#pragma once

#include "CoreMinimal.h"
#include "Data/CombatTypes.h"
#include "EnvironmentTypes.generated.h"

// Environment: day cycle + procedural weather (UE-only, no Godot reference). Design: docs/systems/environment-weather.md
// (user decisions 2026-10-22). Defaults below are the design values; the game reads Content/Data/Environment/environment.json
// over them (Data/EnvironmentTuning, Unreal is the master copy).

/** The four time-of-day phases, always in this order (EarlyMorning -> Midday -> Afternoon -> Night -> EarlyMorning ...). */
UENUM(BlueprintType)
enum class ETimeOfDayPhase : uint8
{
	EarlyMorning,
	Midday,
	Afternoon,
	Night
};

/**
 * The four winter weather states, in LADDER order: weather only ever moves one step (Clear <-> Overcast <-> Snowfall <-> Blizzard).
 * Clear = clear sky and hard frost (not "sunny").
 */
UENUM(BlueprintType)
enum class EWeatherState : uint8
{
	Clear,
	Overcast,
	Snowfall,
	Blizzard
};

/** Phase 2: one pair of the roster (design §4.2): the good-conditions form From becomes Ratio x To when From's suitability is low. */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FEnvironmentRosterPair
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roster")
	EEnemyArchetype From = EEnemyArchetype::Marksman;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roster")
	EEnemyArchetype To = EEnemyArchetype::Frostbitten;

	/** Enemies of To per substituted From (MARKSMAN -> 2 FROSTBITTEN, SPITTER -> 1 CUTTER). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roster", meta = (ClampMin = "1"))
	int32 Ratio = 1;
};

/**
 * Phase 2: how well one enemy type suits each of the 16 time-of-day x weather cells, 0 (useless) .. 4 (ideal).
 * Seeded from Jev (Scripts/Tools/jev_roster_matrix.py, roster v2 approved by the user 2026-10-22).
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FEnvironmentSuitabilityRow
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roster")
	EEnemyArchetype Type = EEnemyArchetype::Marksman;

	/** 16 scores, index = int32(EWeatherState) * 4 + int32(ETimeOfDayPhase). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roster")
	TArray<float> Scores;
};

/**
 * Phase 2: the roster rule (user-approved v2): a pair's From with suitability < AllBelow -> every one swaps, < HalfBelow -> half
 * (round half up per spawn entry); a BonusTypes type with suitability > BonusAbove -> count x BonusMultiplier (round half up).
 * Types without a row (bosses, future types) never change. Map-placed enemies never change; a locked level (Demo 01) keeps its waves.
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FEnvironmentRosterConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roster")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roster", meta = (ClampMin = "0", ClampMax = "4"))
	float HalfBelow = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roster", meta = (ClampMin = "0", ClampMax = "4"))
	float AllBelow = 1.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roster", meta = (ClampMin = "0", ClampMax = "4"))
	float BonusAbove = 2.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roster", meta = (ClampMin = "1"))
	float BonusMultiplier = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roster")
	TArray<FEnvironmentRosterPair> Pairs;

	/** "Hunting weather" reinforcements (HOUND). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roster")
	TArray<EEnemyArchetype> BonusTypes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roster")
	TArray<FEnvironmentSuitabilityRow> Suitability;
};

/** Tuning of the day cycle and the weather ladder (environment.json; snake_case keys of these fields). */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FEnvironmentConfig
{
	GENERATED_BODY()

	// --- Day cycle: each phase starts at a game hour and lasts a number of REAL seconds (~20 real minutes per day) ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayCycle", meta = (ClampMin = "0", ClampMax = "24"))
	float EarlyMorningStartHour = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayCycle", meta = (ClampMin = "0", ClampMax = "24"))
	float MiddayStartHour = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayCycle", meta = (ClampMin = "0", ClampMax = "24"))
	float AfternoonStartHour = 14.f;

	/** Night runs from here over midnight to EarlyMorningStartHour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayCycle", meta = (ClampMin = "0", ClampMax = "24"))
	float NightStartHour = 18.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayCycle", meta = (ClampMin = "1"))
	float EarlyMorningRealSeconds = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayCycle", meta = (ClampMin = "1"))
	float MiddayRealSeconds = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayCycle", meta = (ClampMin = "1"))
	float AfternoonRealSeconds = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayCycle", meta = (ClampMin = "1"))
	float NightRealSeconds = 300.f;

	/** Start hour of a level that sets none and does not continue a previous level (early morning). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayCycle", meta = (ClampMin = "0", ClampMax = "24"))
	float DefaultStartHour = 6.f;

	// --- Weather ladder ---

	/** A weather state lasts this fraction of the real duration of the phase it started in (user decision: 75 % -> 3:45 of 5:00). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0.05"))
	float WeatherDwellPhaseFraction = 0.75f;

	/** The last seconds of a dwell blend into the next state (included in the dwell, never longer than it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0"))
	float WeatherTransitionSeconds = 45.f;

	/** Relative weights of the random start weather (a run that does not continue a previous level). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0"))
	float StartWeightClear = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0"))
	float StartWeightOvercast = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0"))
	float StartWeightSnowfall = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0"))
	float StartWeightBlizzard = 0.15f;

	/** Chance that Overcast steps UP to Snowfall (else down to Clear). The ladder ends are forced (Clear -> Overcast, Blizzard -> Snowfall). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float OvercastStepUpChance = 0.5f;

	/** Chance that Snowfall steps UP to Blizzard (else down to Overcast). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float SnowfallStepUpChance = 0.5f;

	// --- Phase 2: cold (FColdEnvironment::WeatherMultiplier / PhaseMultiplier; design §4.3) ---
	// Open-air cold x weather (blended through a transition) x phase. The weather multiplier applies to zones up to "Standard" (closed 0,
	// shelter 0.5 x weather); a hand-placed Blizzard camera zone (x2.5) already models a storm: max(zone, weather), never zone x weather.

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold", meta = (ClampMin = "0"))
	float ColdMultClear = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold", meta = (ClampMin = "0"))
	float ColdMultOvercast = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold", meta = (ClampMin = "0"))
	float ColdMultSnowfall = 1.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold", meta = (ClampMin = "0"))
	float ColdMultBlizzard = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold", meta = (ClampMin = "0"))
	float ColdMultEarlyMorning = 1.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold", meta = (ClampMin = "0"))
	float ColdMultMidday = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold", meta = (ClampMin = "0"))
	float ColdMultAfternoon = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold", meta = (ClampMin = "0"))
	float ColdMultNight = 1.3f;

	/** Weather / phase cold multipliers on a LOCKED level (Demo 01 tutorial; its cold_rate_mult 0.4 was tuned without weather). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold")
	bool bColdOnLockedLevels = false;

	// --- Phase 2: enemy roster by conditions (design §4.2; defaults filled by EnvironmentTuning from environment.json) ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roster")
	FEnvironmentRosterConfig Roster;
};

/**
 * The authoritative environment state (UEnvironmentSubsystem; saved, and carried over to the next level).
 * The transition blend and the phase are DERIVED from it (EnvironmentRules).
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FEnvironmentState
{
	GENERATED_BODY()

	/** Game clock, hours in [0, 24). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
	float Hour = 6.f;

	/** The weather in force (fully shown until the transition starts). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
	EWeatherState Weather = EWeatherState::Overcast;

	/** The neighbour the weather moves to at the end of this dwell (rolled when the dwell starts). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
	EWeatherState NextWeather = EWeatherState::Snowfall;

	/** Real seconds spent in the current weather state (advances only while the clock runs). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
	float WeatherElapsedSeconds = 0.f;

	/** Real seconds this weather state lasts in total (blend included), fixed when it started. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
	float WeatherDwellSeconds = 225.f;

	/** Seed state of the weather RNG (FRandomStream::GetCurrentSeed), so a load / carry-over continues the same sequence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
	int32 RandomSeed = 0;

	/** Locked (Demo 01 tutorial: Night + Snowfall): neither the clock nor the weather moves. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
	bool bLocked = false;
};

/** What one weather step of EnvironmentRules::StepWeather did. */
struct FWeatherStepResult
{
	/** The blend toward NextWeather began during this step. */
	bool bTransitionStarted = false;
	/** The dwell ended: Weather changed (OldWeather -> State.Weather) and a new NextWeather / dwell were rolled. */
	bool bWeatherChanged = false;
	EWeatherState OldWeather = EWeatherState::Clear;
};

/**
 * Per-level environment settings (level JSON key "environment"). Absent fields keep the procedural default.
 * User decisions: only the Demo 01 tutorial is locked (Night + Snowfall); every later level CONTINUES the time and weather where
 * the previous level ended; a fresh run without a previous level starts at DefaultStartHour with a random start weather.
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FEnvironmentLevelSettings
{
	GENERATED_BODY()

	/** "locked": true - clock and weather never move on this level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
	bool bLocked = false;

	/** "start_hour" present (used when the level is locked or there is no previous level to continue). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
	bool bHasStartHour = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment", meta = (ClampMin = "0", ClampMax = "24"))
	float StartHour = 6.f;

	/** "weather" present ("Clear" / "Overcast" / "Snowfall" / "Blizzard"; same rule as StartHour). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
	bool bHasWeather = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
	EWeatherState Weather = EWeatherState::Overcast;

	/** "seed" present: fixed weather RNG seed for a fresh start (smokes, the bot); 0 = random. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
	int32 Seed = 0;

	/**
	 * "visuals" (phase 3, default true): the weather presenter (AWeatherPresenterActor) drives this level's lighting from the day cycle
	 * and the weather; false = it is not spawned and the map's authored lighting stays.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
	bool bVisuals = true;
};
