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
 * Clear = clear sky and hard frost (not "sunny"). A Temperate level (EClimateProfile) runs the SAME ladder with another meaning:
 * Clear = SunnyDay, Overcast = mild overcast, Snowfall = Rain, Blizzard = Storm (EnvironmentRules::GetWeatherDisplayName).
 */
UENUM(BlueprintType)
enum class EWeatherState : uint8
{
	Clear,
	Overcast,
	Snowfall,
	Blizzard
};

/**
 * The climate of a level (design §17, user decision 2026-10-26): the whole game is Arctic (the winter ladder, snow, cold); only
 * specific narrative levels are Temperate (level JSON "environment": {"climate": "temperate"}): a sunny day and rain on the same
 * one-step ladder, no snow growth, mild cold, wetness / puddles. Absent = Arctic.
 */
UENUM(BlueprintType)
enum class EClimateProfile : uint8
{
	Arctic,
	Temperate
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

/**
 * Snow held by a surface in five directional pockets, 0..1 each (design §15, user decision 2026-10-23): Top = faces pointing up
 * (+Z), PX / NX / PY / NY = faces pointing along +X / -X / +Y / -Y. Static objects use WORLD axes (FEnvironmentState::ObjectSnow,
 * MPC_Environment.SnowTop / SnowPX / SnowNX / SnowPY / SnowNY); characters use their body mesh's LOCAL axes (UCharacterSnowSubsystem,
 * Custom Primitive Data CharacterSnowRules::FirstCustomDataIndex..+4). A pocket keeps its snow when the wind turns. Shader mask:
 * Top x max(0, N.z) + sum of side_i x max(0, N . dir_i), plus noise.
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FDirectionalSnow
{
	GENERATED_BODY()

	static constexpr int32 PocketCount = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow", meta = (ClampMin = "0", ClampMax = "1"))
	float Top = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow", meta = (ClampMin = "0", ClampMax = "1"))
	float PX = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow", meta = (ClampMin = "0", ClampMax = "1"))
	float NX = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow", meta = (ClampMin = "0", ClampMax = "1"))
	float PY = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow", meta = (ClampMin = "0", ClampMax = "1"))
	float NY = 0.f;

	/** Pocket Index: 0 Top, 1 PX, 2 NX, 3 PY, 4 NY (out of range = 0). */
	float Get(int32 Index) const
	{
		switch (Index)
		{
		case 0: return Top;
		case 1: return PX;
		case 2: return NX;
		case 3: return PY;
		case 4: return NY;
		default: return 0.f;
		}
	}

	void Set(int32 Index, float Value)
	{
		switch (Index)
		{
		case 0: Top = Value; break;
		case 1: PX = Value; break;
		case 2: NX = Value; break;
		case 3: PY = Value; break;
		case 4: NY = Value; break;
		default: break;
		}
	}

	/** Unit face direction of pocket Index (+Z, +X, -X, +Y, -Y). */
	static FVector GetPocketDirection(int32 Index)
	{
		switch (Index)
		{
		case 1: return FVector(1.0, 0.0, 0.0);
		case 2: return FVector(-1.0, 0.0, 0.0);
		case 3: return FVector(0.0, 1.0, 0.0);
		case 4: return FVector(0.0, -1.0, 0.0);
		default: return FVector(0.0, 0.0, 1.0);
		}
	}

	float GetMax() const { return FMath::Max(FMath::Max(Top, PX), FMath::Max(FMath::Max(NX, PY), NY)); }

	bool IsZero() const { return Top == 0.f && PX == 0.f && NX == 0.f && PY == 0.f && NY == 0.f; }

	/** Every pocket clamped to [0, 1]. */
	FDirectionalSnow Clamped() const
	{
		FDirectionalSnow Out;
		for (int32 Index = 0; Index < PocketCount; ++Index)
		{
			Out.Set(Index, FMath::Clamp(Get(Index), 0.f, 1.f));
		}
		return Out;
	}
};

/**
 * One step of a set of directional snow pockets (EnvironmentRules::StepDirectionalSnow; pure). Growth first, then melt / clear. The
 * wind is the TRAVEL direction (where the snow is blown to, MPC WindDirection) in the pockets' frame; a side fills when it faces it.
 */
struct FDirectionalSnowInputs
{
	/** Top gain per second (the blended snow cover rate; 0 when it does not snow / indoors). */
	float GrowRate = 0.f;
	/** Horizontal unit wind travel direction in the pockets' frame (zero = no side growth). */
	FVector2D WindDirection = FVector2D::ZeroVector;
	/** 0..1 blended preset wind. */
	float WindStrength = 0.f;
	/** Side gain = GrowRate x SideRateScale x max(0, -dot(wind, side)) x WindStrength. */
	float SideRateScale = 1.f;
	/** Loss per second on every pocket (sun melt), never below TopFloor for the top / 0 for the sides. */
	float MeltRate = 0.f;
	float TopFloor = 0.f;
	/** Loss per second on every pocket down to 0 (heat / indoors), applied after the melt. */
	float ClearRate = 0.f;
};

/**
 * Section 17: the Temperate climate profile (environment.json block "temperate"; the Arctic values stay the top-level keys). The ladder
 * states mean SunnyDay / Overcast / Rain / Storm here (Clear / Overcast / Snowfall / Blizzard). Defaults are the design proposal.
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FTemperateClimateConfig
{
	GENERATED_BODY()

	// --- Cold: open-air cold x weather (blended) x phase, mild (~0.25-0.3 of the arctic values; rain colder than sun) ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold", meta = (ClampMin = "0"))
	float ColdMultSunny = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold", meta = (ClampMin = "0"))
	float ColdMultOvercast = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold", meta = (ClampMin = "0"))
	float ColdMultRain = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold", meta = (ClampMin = "0"))
	float ColdMultStorm = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold", meta = (ClampMin = "0"))
	float ColdMultEarlyMorning = 1.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold", meta = (ClampMin = "0"))
	float ColdMultMidday = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold", meta = (ClampMin = "0"))
	float ColdMultAfternoon = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold", meta = (ClampMin = "0"))
	float ColdMultNight = 1.2f;

	/** The weather / time-of-day roster on a temperate level (false = the waves as written: narrative levels are hand-authored). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roster")
	bool bRosterEnabled = false;

	/** Falling snow on a temperate level (false = the snow Niagara system stays off whatever the preset says). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals")
	bool bFallingSnow = false;

	/** Screen raindrops on the tactical camera while it rains (MPC ScreenDrops; user decision 2026-10-26: ON). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals")
	bool bScreenDrops = true;

	// --- Rain (UEnvironmentSubsystem::GetRainIntensity, MPC RainIntensity; 0 in SunnyDay / Overcast, blended through a transition) ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain", meta = (ClampMin = "0", ClampMax = "1"))
	float RainIntensityRain = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain", meta = (ClampMin = "0", ClampMax = "1"))
	float RainIntensityStorm = 1.f;

	// --- Wetness (FEnvironmentState::Wetness 0..1) and puddles (PuddleAmount 0..1), per REAL second of running clock ---

	/** Wetness gained per second in Rain (1 / 120: dry to soaked in 2 real minutes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness", meta = (ClampMin = "0"))
	float WetnessRateRain = 1.f / 120.f;

	/** Wetness gained per second in a Storm (1 / 45). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness", meta = (ClampMin = "0"))
	float WetnessRateStorm = 1.f / 45.f;

	/** Wetness lost per second in SunnyDay with the sun high (1 / 240: soaked to dry in 4 real minutes); at night like overcast. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness", meta = (ClampMin = "0"))
	float DryingRateSunny = 1.f / 240.f;

	/** Wetness lost per second under Overcast (and in SunnyDay with the sun down): 1 / 900. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness", meta = (ClampMin = "0"))
	float DryingRateOvercast = 1.f / 900.f;

	/** Sun elevation (degrees) from which SunnyDay dries at the full sunny rate (linear ramp from the horizon). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness", meta = (ClampMin = "0.1", ClampMax = "90"))
	float DryingFullSunElevation = 20.f;

	/** Puddles form once the ground is wetter than this (target = (wetness - threshold) / (1 - threshold)). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness", meta = (ClampMin = "0", ClampMax = "0.99"))
	float PuddleWetnessThreshold = 0.5f;

	/** Puddles fill toward the target at this rate per second, only while the ground gets wetter (1 / 90). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness", meta = (ClampMin = "0"))
	float PuddleFillRate = 1.f / 90.f;

	/** Puddles drain toward the target at this rate per second, slower than the ground dries (1 / 600). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness", meta = (ClampMin = "0"))
	float PuddleDrainRate = 1.f / 600.f;

	/**
	 * A temperate level that starts (fresh / locked / arriving from an arctic level) in Rain or Storm is already wet: these start values
	 * (SunnyDay / Overcast start dry). A level JSON "wetness" / "puddles" overrides them.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness", meta = (ClampMin = "0", ClampMax = "1"))
	float StartWetnessRain = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness", meta = (ClampMin = "0", ClampMax = "1"))
	float StartPuddlesRain = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness", meta = (ClampMin = "0", ClampMax = "1"))
	float StartWetnessStorm = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness", meta = (ClampMin = "0", ClampMax = "1"))
	float StartPuddlesStorm = 1.f;
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

	// --- Phase 4: persistent snow cover (FEnvironmentState::SnowAmount; design §6 / §12; user decision 2026-10-22: it NEVER recedes) ---

	/** Snow cover gained per REAL second of running clock while it snows (Snowfall): 1 / 480 -> bare to full in ~8 real minutes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SnowCover", meta = (ClampMin = "0"))
	float SnowCoverRateSnowfall = 1.f / 480.f;

	/** Snow cover gained per REAL second in a Blizzard: 1 / 180 -> bare to full in ~3 real minutes. Clear / Overcast: 0 (it holds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SnowCover", meta = (ClampMin = "0"))
	float SnowCoverRateBlizzard = 1.f / 180.f;

	/** Snow cover of a fresh run (0..1): arctic ground already carries old snow. A level JSON "snow_cover" overrides it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SnowCover", meta = (ClampMin = "0", ClampMax = "1"))
	float SnowCoverStart = 0.35f;

	// --- Directional snow on objects / characters (FEnvironmentState::ObjectSnow, UCharacterSnowSubsystem; design §15, user decisions
	// 2026-10-23): the top pocket grows at the snow cover rates, a side pocket at rate x max(0, -dot(wind, side)) x wind strength x
	// object_snow_side_rate_scale; every pocket melts in Clear while the sun is up (the ground cover never recedes) ---

	/** x the snow cover rate for the windward side pockets (1 = a side facing a full-strength wind fills as fast as the top). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DirectionalSnow", meta = (ClampMin = "0"))
	float ObjectSnowSideRateScale = 1.f;

	/** Pocket loss per REAL second in Clear with the sun high (1 / 600: a full pocket melts away in ~10 real minutes of midday sun). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DirectionalSnow", meta = (ClampMin = "0"))
	float ObjectSnowMeltRate = 1.f / 600.f;

	/** The objects' TOP pocket never melts below this share of the ground cover (FEnvironmentState::SnowAmount); sides melt to 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DirectionalSnow", meta = (ClampMin = "0", ClampMax = "1"))
	float ObjectSnowMeltFloor = 0.5f;

	/** Sun elevation (degrees) from which the melt runs at full rate; it ramps up linearly from the horizon (0 at or below it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DirectionalSnow", meta = (ClampMin = "0.1", ClampMax = "90"))
	float ObjectSnowMeltFullSunElevation = 20.f;

	/** Characters: x the snow cover rates (top and windward sides) while outdoors. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DirectionalSnow", meta = (ClampMin = "0"))
	float CharacterSnowFillScale = 1.f;

	/** Characters: x object_snow_melt_rate in the sun (floor 0: a character's snow melts away completely). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DirectionalSnow", meta = (ClampMin = "0"))
	float CharacterSnowMeltScale = 1.f;

	/** Characters near a heat source / inside a Closed or Shelter zone lose this much per real second on every pocket (1/20: ~20 s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DirectionalSnow", meta = (ClampMin = "0"))
	float CharacterSnowClearRate = 1.f / 20.f;

	/** An enemy appearing while it snows starts with this share of the objects' pockets (x the snowing amount). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DirectionalSnow", meta = (ClampMin = "0", ClampMax = "1"))
	float CharacterSnowSpawnShare = 0.5f;

	// --- Phase 2: enemy roster by conditions (design §4.2; defaults filled by EnvironmentTuning from environment.json) ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roster")
	FEnvironmentRosterConfig Roster;

	/** Section 17: the Temperate climate profile (block "temperate"); every value above stays the Arctic one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climate")
	FTemperateClimateConfig Temperate;

	/**
	 * Section 19.6: per-map exposure bias in EV (block "level_overrides": {"<MapName>": {"exposure_bias": -0.75}}), for a map without a
	 * level JSON (L_MovementTest). Added to the presenter's post-process exposure bias in every climate; a map not listed gets 0.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
	TMap<FString, float> LevelExposureBias;
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

	/**
	 * Phase 4: persistent snow cover on the ground / objects, 0 (bare) .. 1 (fully white). Grows only while it snows
	 * (EnvironmentRules::StepSnowCover), holds in Clear / Overcast and NEVER decreases (user decision 2026-10-22); saved and carried to
	 * the next level with the rest of the state. The presenter writes it to MPC_Environment.SnowAmount (visual only).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment", meta = (ClampMin = "0", ClampMax = "1"))
	float SnowAmount = 0.f;

	/**
	 * Directional snow on static objects (§15, WORLD axes): the top grows with the snow cover rates, the windward sides with the wind;
	 * every pocket melts in Clear daylight (EnvironmentRules::StepObjectSnow), the top never below ObjectSnowMeltFloor x SnowAmount.
	 * Saved / carried with the state; the presenter writes it to MPC_Environment.SnowTop / SnowPX / SnowNX / SnowPY / SnowNY.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
	FDirectionalSnow ObjectSnow;

	/**
	 * Section 17: the climate this state belongs to (the level's profile). A carried state of another climate is re-initialised for
	 * the new level's profile (the time of day continues). Arctic for every state of an arctic level (and for older saves / carries).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
	EClimateProfile Climate = EClimateProfile::Arctic;

	/** Section 17 (Temperate only, 0 on arctic levels): how wet the ground is, 0 dry .. 1 soaked (EnvironmentRules::StepWetness). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment", meta = (ClampMin = "0", ClampMax = "1"))
	float Wetness = 0.f;

	/** Section 17 (Temperate only): standing water 0..1; lags Wetness (fills once the ground is wet, drains slower than it dries). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment", meta = (ClampMin = "0", ClampMax = "1"))
	float PuddleAmount = 0.f;
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

	/** Phase 4: "snow_cover" present (0..1): the start snow cover when the level is locked or starts fresh (else snow_cover_start). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
	bool bHasSnowCover = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment", meta = (ClampMin = "0", ClampMax = "1"))
	float SnowCover = 0.35f;

	/** Section 17: "climate": "arctic" (default) | "temperate" (a narrative level with a sunny day / rain; no snow growth). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
	EClimateProfile Climate = EClimateProfile::Arctic;

	/** Section 17 (temperate): "wetness" present (0..1): the start wetness when the level is locked / starts fresh for its climate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
	bool bHasWetness = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment", meta = (ClampMin = "0", ClampMax = "1"))
	float Wetness = 0.f;

	/** Section 17 (temperate): "puddles" present (0..1): the start puddle amount (same rule as Wetness). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
	bool bHasPuddles = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment", meta = (ClampMin = "0", ClampMax = "1"))
	float Puddles = 0.f;

	/** Section 19.6: "exposure_bias" (EV, default 0, clamped -4..4): added to the presenter's exposure bias on this level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment", meta = (ClampMin = "-4", ClampMax = "4"))
	float ExposureBias = 0.f;
};
