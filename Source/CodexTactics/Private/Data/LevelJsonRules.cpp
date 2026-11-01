#include "Data/LevelJsonRules.h"
#include "Bot/BotRouteRules.h"
#include "CodexTactics.h"
#include "Environment/EnvironmentRules.h"
#include "GameFlow/LevelEncounterRules.h"
#include "Quests/GateExitRules.h"
#include "Quests/GateProgressRules.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	float LevelJsonNumber(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, float Default)
	{
		double Value = Default;
		return Object.IsValid() && Object->TryGetNumberField(Key, Value) ? static_cast<float>(Value) : Default;
	}

	FString LevelJsonString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, const FString& Default)
	{
		FString Value;
		return Object.IsValid() && Object->TryGetStringField(Key, Value) ? Value : Default;
	}

	TSharedPtr<FJsonObject> LevelJsonObject(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key)
	{
		const TSharedPtr<FJsonObject>* Found = nullptr;
		return Object.IsValid() && Object->TryGetObjectField(Key, Found) && Found ? *Found : nullptr;
	}
}

FString LevelJsonRules::GetLevelsDirectory()
{
	return FPaths::ProjectContentDir() / TEXT("Data/LevelJson");
}

FString LevelJsonRules::NormalizeMapName(const FString& MapName)
{
	FString Name = FPaths::GetBaseFilename(MapName);
	// PIE worlds are prefixed "UEDPIE_<n>_".
	if (Name.StartsWith(TEXT("UEDPIE_")))
	{
		FString Rest;
		FString Tail;
		if (Name.Split(TEXT("_"), nullptr, &Rest) && Rest.Split(TEXT("_"), nullptr, &Tail)) // drop "UEDPIE", then "<n>"
		{
			Name = Tail;
		}
	}
	return Name;
}

FString LevelJsonRules::ChooseLevelJsonFile(const FString& MapName, bool bMapFileExists, const FString& DefaultFile, const FString& CommandLineOverride)
{
	if (!CommandLineOverride.IsEmpty())
	{
		return CommandLineOverride;
	}
	const FString Name = NormalizeMapName(MapName);
	if (bMapFileExists && !Name.IsEmpty())
	{
		return Name + TEXT(".json");
	}
	return DefaultFile;
}

FString LevelJsonRules::ResolveLevelJsonFile(const FString& MapName, const FString& DefaultFile, const FString& CommandLineOverride)
{
	const FString Name = NormalizeMapName(MapName);
	const bool bExists = !Name.IsEmpty() && IFileManager::Get().FileExists(*(GetLevelsDirectory() / (Name + TEXT(".json"))));
	return ChooseLevelJsonFile(MapName, bExists, DefaultFile, CommandLineOverride);
}

bool LevelJsonRules::ParseEnemyType(const FString& Name, EEnemyArchetype& OutType)
{
	static const TMap<FString, EEnemyArchetype> Types = {
		{ TEXT("HOUND"), EEnemyArchetype::FrostHound }, { TEXT("FROST_HOUND"), EEnemyArchetype::FrostHound },
		{ TEXT("SPITTER"), EEnemyArchetype::Spitter }, { TEXT("BRUTE"), EEnemyArchetype::Brute },
		{ TEXT("FROSTBITTEN"), EEnemyArchetype::Frostbitten }, { TEXT("CUTTER"), EEnemyArchetype::Cutter },
		{ TEXT("CRYO_DRONE"), EEnemyArchetype::CryoDrone }, { TEXT("MARKSMAN"), EEnemyArchetype::Marksman } };
	if (const EEnemyArchetype* Found = Types.Find(Name.ToUpper()))
	{
		OutType = *Found;
		return true;
	}
	return false;
}

FDemoModeSettings LevelJsonRules::ParseDemoSettings(const FJsonObject& Demo)
{
	FDemoModeSettings Settings;
	Settings.bDemo = true;
	bool bValue = true;
	if (Demo.TryGetBoolField(TEXT("panic"), bValue))
	{
		Settings.bPanicEnabled = bValue;
	}
	if (Demo.TryGetBoolField(TEXT("vision_cones"), bValue))
	{
		Settings.bOverrideVisionCones = true;
		Settings.bVisionCones = bValue;
	}
	if (Demo.TryGetBoolField(TEXT("tutorial"), bValue))
	{
		Settings.bTutorial = bValue;
	}
	FString Text;
	Settings.TutorialSteps = Demo.TryGetStringField(TEXT("tutorial_steps"), Text) && !Text.IsEmpty() ? Text : FString(TEXT("demo_01_steps.json"));
	Settings.bDemoHud = Demo.TryGetStringField(TEXT("hud"), Text) && Text.Equals(TEXT("demo"), ESearchCase::IgnoreCase);
	// A demo level has no blocking briefing window unless it asks for one (the A0 walk-and-talk replaces it).
	Settings.bIntroDialogue = Demo.TryGetBoolField(TEXT("intro_dialogue"), bValue) && bValue;
	return Settings;
}

FEnvironmentLevelSettings LevelJsonRules::ParseEnvironmentSettings(const FJsonObject& Environment, FString* OutError)
{
	FEnvironmentLevelSettings Settings;
	bool bLocked = false;
	if (Environment.TryGetBoolField(TEXT("locked"), bLocked))
	{
		Settings.bLocked = bLocked;
	}
	double Number = 0.0;
	if (Environment.TryGetNumberField(TEXT("start_hour"), Number))
	{
		Settings.bHasStartHour = true;
		Settings.StartHour = FMath::Clamp(static_cast<float>(Number), 0.f, 24.f);
	}
	// Section 17: the climate first - the weather names a temperate level may use depend on it.
	FString ClimateName;
	if (Environment.TryGetStringField(TEXT("climate"), ClimateName))
	{
		EClimateProfile Climate = EClimateProfile::Arctic;
		if (EnvironmentRules::ClimateFromString(ClimateName, Climate))
		{
			Settings.Climate = Climate;
		}
		else if (OutError)
		{
			*OutError = FString::Printf(TEXT("environment.climate: unknown climate '%s' (arctic / temperate); arctic"), *ClimateName);
		}
	}
	FString WeatherName;
	if (Environment.TryGetStringField(TEXT("weather"), WeatherName))
	{
		EWeatherState Weather = EWeatherState::Overcast;
		if (EnvironmentRules::WeatherFromString(Settings.Climate, WeatherName, Weather))
		{
			Settings.bHasWeather = true;
			Settings.Weather = Weather;
		}
		else if (OutError)
		{
			const FString Error = Settings.Climate == EClimateProfile::Temperate
				? FString::Printf(TEXT("environment.weather: unknown weather '%s' (Sunny / Overcast / Rain / Storm or Clear / Snowfall / Blizzard); ignored"), *WeatherName)
				: FString::Printf(TEXT("environment.weather: unknown weather '%s' (Clear / Overcast / Snowfall / Blizzard; Sunny / Rain / Storm only with \"climate\": \"temperate\"); ignored"), *WeatherName);
			*OutError = OutError->IsEmpty() ? Error : *OutError + TEXT("; ") + Error;
		}
	}
	if (Environment.TryGetNumberField(TEXT("seed"), Number))
	{
		Settings.Seed = FMath::RoundToInt(Number);
	}
	bool bVisuals = true;
	if (Environment.TryGetBoolField(TEXT("visuals"), bVisuals))
	{
		Settings.bVisuals = bVisuals;
	}
	// Phase 4: the start snow cover of a locked / fresh level (0..1).
	if (Environment.TryGetNumberField(TEXT("snow_cover"), Number))
	{
		Settings.bHasSnowCover = true;
		Settings.SnowCover = FMath::Clamp(static_cast<float>(Number), 0.f, 1.f);
	}
	// Section 17 (temperate): the start wetness / puddles of a locked / fresh level (0..1).
	if (Environment.TryGetNumberField(TEXT("wetness"), Number))
	{
		Settings.bHasWetness = true;
		Settings.Wetness = FMath::Clamp(static_cast<float>(Number), 0.f, 1.f);
	}
	if (Environment.TryGetNumberField(TEXT("puddles"), Number))
	{
		Settings.bHasPuddles = true;
		Settings.Puddles = FMath::Clamp(static_cast<float>(Number), 0.f, 1.f);
	}
	// Section 19.6: an extra exposure bias (EV) for this level's presenter.
	if (Environment.TryGetNumberField(TEXT("exposure_bias"), Number))
	{
		Settings.ExposureBias = FMath::Clamp(static_cast<float>(Number), -4.f, 4.f);
	}
	return Settings;
}

bool LevelJsonRules::ParsePressureSettings(const FJsonObject& Pressure, FCombatPressureConfig& OutConfig, FString* OutError)
{
	FCombatPressureConfig Config;
	const auto Number = [](const FJsonObject& Object, const TCHAR* Key, float Default)
	{
		double Value = Default;
		return Object.TryGetNumberField(Key, Value) ? static_cast<float>(Value) : Default;
	};
	const auto Child = [](const FJsonObject& Object, const TCHAR* Key) -> const FJsonObject*
	{
		const TSharedPtr<FJsonObject>* Found = nullptr;
		return Object.TryGetObjectField(Key, Found) && Found && Found->IsValid() ? Found->Get() : nullptr;
	};
	bool bEnabled = true;
	Config.bEnabled = !Pressure.TryGetBoolField(TEXT("enabled"), bEnabled) || bEnabled;
	Config.IntervalSeconds = FMath::Max(5.f, Number(Pressure, TEXT("interval_sec"), Config.IntervalSeconds));
	Config.FirstEventSeconds = FMath::Max(0.f, Number(Pressure, TEXT("first_event_sec"), Config.FirstEventSeconds));
	Config.GroupMin = FMath::Max(1, FMath::RoundToInt(Number(Pressure, TEXT("group_min"), static_cast<float>(Config.GroupMin))));
	Config.GroupMax = FMath::Max(Config.GroupMin, FMath::RoundToInt(Number(Pressure, TEXT("group_max"), static_cast<float>(Config.GroupMax))));
	Config.MaxAlive = FMath::Max(1, FMath::RoundToInt(Number(Pressure, TEXT("max_alive"), static_cast<float>(Config.MaxAlive))));
	Config.PatrolShare = FMath::Clamp(Number(Pressure, TEXT("patrol_share"), Config.PatrolShare), 0.f, 1.f);
	Config.Seed = FMath::RoundToInt(Number(Pressure, TEXT("seed"), static_cast<float>(Config.Seed)));
	Config.WallClearanceCm = FMath::Max(0.f, Number(Pressure, TEXT("wall_clearance_cm"), Config.WallClearanceCm));
	Config.MinSquadDistanceCm = FMath::Max(0.f, Number(Pressure, TEXT("min_squad_distance_cm"), Config.MinSquadDistanceCm));
	Config.MaxSquadDistanceCm = FMath::Max(Config.MinSquadDistanceCm + 100.f, Number(Pressure, TEXT("max_squad_distance_cm"), Config.MaxSquadDistanceCm));
	if (const FJsonObject* Types = Child(Pressure, TEXT("types")))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : Types->Values)
		{
			EEnemyArchetype Type = EEnemyArchetype::Base;
			if (!ParseEnemyType(Entry.Key, Type))
			{
				if (OutError)
				{
					*OutError = FString::Printf(TEXT("unknown enemy type '%s' in types"), *Entry.Key);
				}
				return false;
			}
			if (!CombatPressureRules::IsAllowedType(Type))
			{
				if (OutError)
				{
					*OutError = FString::Printf(TEXT("'%s' is never spawned by the pressure (HOUND, FROSTBITTEN, SPITTER, CUTTER, MARKSMAN)"), *Entry.Key);
				}
				return false;
			}
			double Weight = 0.0;
			if (Entry.Value.IsValid() && Entry.Value->TryGetNumber(Weight) && Weight > 0.0)
			{
				Config.TypeWeights.Add({ Type, static_cast<float>(Weight) });
			}
		}
	}
	if (const FJsonObject* Patrol = Child(Pressure, TEXT("patrol")))
	{
		Config.PatrolPoints = FMath::Clamp(FMath::RoundToInt(Number(*Patrol, TEXT("points"), static_cast<float>(Config.PatrolPoints))), 2, 12);
		Config.PatrolRadiusCm = FMath::Max(200.f, Number(*Patrol, TEXT("radius_cm"), Config.PatrolRadiusCm));
		Config.PatrolWaitSeconds = FMath::Max(0.f, Number(*Patrol, TEXT("wait_sec"), Config.PatrolWaitSeconds));
	}
	if (const FJsonObject* Ambush = Child(Pressure, TEXT("ambush")))
	{
		Config.AmbushMinDistanceCm = FMath::Max(0.f, Number(*Ambush, TEXT("min_squad_distance_cm"), Config.AmbushMinDistanceCm));
		Config.AmbushMaxDistanceCm = FMath::Max(Config.AmbushMinDistanceCm + 100.f, Number(*Ambush, TEXT("max_squad_distance_cm"), Config.AmbushMaxDistanceCm));
		Config.Watch.WatchHalfAngleDeg = FMath::Clamp(Number(*Ambush, TEXT("watch_half_angle_deg"), Config.Watch.WatchHalfAngleDeg), 0.f, 180.f);
		Config.Watch.WatchRangeCm = FMath::Max(0.f, Number(*Ambush, TEXT("watch_range_cm"), Config.Watch.WatchRangeCm));
		Config.Watch.StrikeRangeCm = FMath::Max(100.f, Number(*Ambush, TEXT("strike_range_cm"), Config.Watch.StrikeRangeCm));
		// Game designer answers 2026-11-01: hiding spots (wall ends / doorways / authored points), coordinated plans, relocation.
		if (const FJsonObject* Spots = Child(*Ambush, TEXT("spots")))
		{
			FAmbushSpotParams& S = Config.Spots;
			S.WallStandoffCm = FMath::Max(50.f, Number(*Spots, TEXT("wall_standoff_cm"), S.WallStandoffCm));
			S.EndInsetCm = FMath::Max(0.f, Number(*Spots, TEXT("end_inset_cm"), S.EndInsetCm));
			S.MinWallLengthCm = FMath::Max(0.f, Number(*Spots, TEXT("min_wall_length_cm"), S.MinWallLengthCm));
			S.JoinedGapCm = FMath::Max(1.f, Number(*Spots, TEXT("joined_gap_cm"), S.JoinedGapCm));
			S.DoorwayMinCm = FMath::Max(0.f, Number(*Spots, TEXT("doorway_min_cm"), S.DoorwayMinCm));
			S.DoorwayMaxCm = FMath::Max(S.DoorwayMinCm, Number(*Spots, TEXT("doorway_max_cm"), S.DoorwayMaxCm));
			S.AuthoredBonus = Number(*Spots, TEXT("authored_bonus"), S.AuthoredBonus);
			S.DoorwayBonus = Number(*Spots, TEXT("doorway_bonus"), S.DoorwayBonus);
			S.MergeCm = FMath::Max(0.f, Number(*Spots, TEXT("merge_cm"), S.MergeCm));
		}
		const TArray<TSharedPtr<FJsonValue>>* Points = nullptr;
		if (Ambush->TryGetArrayField(TEXT("points"), Points) && Points)
		{
			for (const TSharedPtr<FJsonValue>& Point : *Points)
			{
				const TArray<TSharedPtr<FJsonValue>>* XY = nullptr;
				double X = 0.0;
				double Y = 0.0;
				if (!Point.IsValid() || !Point->TryGetArray(XY) || !XY || XY->Num() < 2 || !(*XY)[0]->TryGetNumber(X) || !(*XY)[1]->TryGetNumber(Y))
				{
					if (OutError)
					{
						*OutError = TEXT("ambush.points: every point is [x, y] (world cm)");
					}
					return false;
				}
				Config.AuthoredPoints.Add(FVector2D(X, Y));
			}
		}
		if (const FJsonObject* Plan = Child(*Ambush, TEXT("coordinated")))
		{
			FAmbushPlanParams& C = Config.Coordinated;
			C.Share = FMath::Clamp(Number(*Plan, TEXT("share"), C.Share), 0.f, 1.f);
			C.BodiesMin = FMath::Max(4, FMath::RoundToInt(Number(*Plan, TEXT("bodies_min"), static_cast<float>(C.BodiesMin))));
			C.BodiesMax = FMath::Max(C.BodiesMin, FMath::RoundToInt(Number(*Plan, TEXT("bodies_max"), static_cast<float>(C.BodiesMax))));
			C.MaxAlive = FMath::Max(4, FMath::RoundToInt(Number(*Plan, TEXT("max_alive"), static_cast<float>(C.MaxAlive))));
			C.MinSquadDistanceCm = FMath::Max(0.f, Number(*Plan, TEXT("min_squad_distance_cm"), C.MinSquadDistanceCm));
			C.MaxSquadDistanceCm = FMath::Max(C.MinSquadDistanceCm + 100.f, Number(*Plan, TEXT("max_squad_distance_cm"), C.MaxSquadDistanceCm));
			C.AnchorAheadCm = Number(*Plan, TEXT("anchor_ahead_cm"), C.AnchorAheadCm);
			C.FlankOffsetCm = FMath::Max(100.f, Number(*Plan, TEXT("flank_offset_cm"), C.FlankOffsetCm));
			C.FrontOffsetCm = FMath::Max(100.f, Number(*Plan, TEXT("front_offset_cm"), C.FrontOffsetCm));
			C.RearOffsetCm = FMath::Max(100.f, Number(*Plan, TEXT("rear_offset_cm"), C.RearOffsetCm));
			C.MinSeparationCm = FMath::Max(0.f, Number(*Plan, TEXT("min_separation_cm"), C.MinSeparationCm));
			C.TriggerRangeCm = FMath::Max(100.f, Number(*Plan, TEXT("trigger_range_cm"), C.TriggerRangeCm));
			C.AssaultMinSeconds = FMath::Max(0.f, Number(*Plan, TEXT("assault_min_sec"), C.AssaultMinSeconds));
			C.AssaultMaxSeconds = FMath::Max(C.AssaultMinSeconds, Number(*Plan, TEXT("assault_max_sec"), C.AssaultMaxSeconds));
			C.BlockDistanceCm = FMath::Max(100.f, Number(*Plan, TEXT("block_distance_cm"), C.BlockDistanceCm));
			C.MinRanged = FMath::Clamp(FMath::RoundToInt(Number(*Plan, TEXT("min_ranged"), static_cast<float>(C.MinRanged))), 0, 6);
		}
		if (const FJsonObject* Relocate = Child(*Ambush, TEXT("relocate")))
		{
			FAmbushRelocateParams& R = Config.Relocate;
			bool bRelocate = true;
			R.bEnabled = !Relocate->TryGetBoolField(TEXT("enabled"), bRelocate) || bRelocate;
			R.AfterSeconds = FMath::Max(0.f, Number(*Relocate, TEXT("after_sec"), R.AfterSeconds));
			R.BehindAngleDeg = FMath::Clamp(Number(*Relocate, TEXT("behind_angle_deg"), R.BehindAngleDeg), 0.f, 180.f);
			R.FarCm = FMath::Max(0.f, Number(*Relocate, TEXT("far_cm"), R.FarCm));
			R.MinAheadCos = FMath::Clamp(Number(*Relocate, TEXT("min_ahead_cos"), R.MinAheadCos), -1.f, 1.f);
		}
	}
	if (const FJsonObject* Recycle = Child(Pressure, TEXT("recycle")))
	{
		Config.RecycleDistanceCm = FMath::Max(0.f, Number(*Recycle, TEXT("distance_cm"), Config.RecycleDistanceCm));
		Config.RecycleAfterSeconds = FMath::Max(0.f, Number(*Recycle, TEXT("after_sec"), Config.RecycleAfterSeconds));
	}
	OutConfig = MoveTemp(Config);
	return true;
}

bool LevelJsonRules::ParseLevel(const FString& Json, const FString& FallbackId, FLevelCombatConfig& OutConfig, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
	{
		OutError = TEXT("invalid JSON");
		return false;
	}
	FLevelCombatConfig Config;
	Config.LevelId = LevelJsonString(Root, TEXT("level_id"), FallbackId);
	Config.LevelName = FText::FromString(LevelJsonString(Root, TEXT("level_name"), FallbackId));
	Config.PrepPhaseDuration = LevelJsonNumber(Root, TEXT("prep_phase_duration"), 60.f);
	Config.WaveRestDuration = LevelJsonNumber(Root, TEXT("wave_rest_duration"), 20.f);
	// UE-only (user request 2026-10-06): ambush combat start and the patrol search time.
	if (!LevelEncounterRules::ParseCombatStart(LevelJsonString(Root, TEXT("combat_start"), TEXT("auto")), Config.CombatStart))
	{
		OutError = FString::Printf(TEXT("unknown combat_start '%s' (auto | ambush | button)"), *LevelJsonString(Root, TEXT("combat_start"), FString()));
		return false;
	}
	Config.PatrolSearchSeconds = LevelJsonNumber(Root, TEXT("patrol_search_seconds"), -1.f);
	Config.ColdRateMult = FMath::Max(0.f, LevelJsonNumber(Root, TEXT("cold_rate_mult"), 1.f));
	// UE-only (user request 2026-10-06): the horde after a long real-time fight (defaults in Content/Data/AI/horde.json).
	bool bHordeEnabled = true;
	Config.bHordeEnabled = !Root->TryGetBoolField(TEXT("horde_enabled"), bHordeEnabled) || bHordeEnabled;
	// 2026-10-21: "ambush_fight": "level" | "skirmish" (placed-enemy fights) and "susanin_wave" (0 = off).
	const FString AmbushFight = LevelJsonString(Root, TEXT("ambush_fight"), TEXT("level"));
	if (AmbushFight != TEXT("level") && AmbushFight != TEXT("skirmish"))
	{
		OutError = FString::Printf(TEXT("unknown ambush_fight '%s' (level | skirmish)"), *AmbushFight);
		return false;
	}
	Config.bAmbushSkirmish = AmbushFight == TEXT("skirmish");
	Config.SusaninWave = FMath::Max(-1, static_cast<int32>(LevelJsonNumber(Root, TEXT("susanin_wave"), -1.f)));
	if (const TSharedPtr<FJsonObject> Horde = LevelJsonObject(Root, TEXT("horde")))
	{
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Config.HordeOverrideJson);
		FJsonSerializer::Serialize(Horde.ToSharedRef(), Writer);
	}
	// UE-only (user request 2026-10-11): the blast gate tied to the generator during the wave fight (GateProgressRules).
	const FString GateMode = LevelJsonString(Root, TEXT("gate_open_mode"), GateProgressRules::OpenModeToString(Config.Gate.Mode));
	if (!GateProgressRules::ParseOpenMode(GateMode, Config.Gate.Mode))
	{
		OutError = FString::Printf(TEXT("unknown gate_open_mode '%s' (generator_waves | instant)"), *GateMode);
		return false;
	}
	Config.Gate.ProgressPerSecond = FMath::Max(0.f, LevelJsonNumber(Root, TEXT("gate_progress_per_second"), Config.Gate.ProgressPerSecond));
	Config.Gate.MaxProgressBeforeFinal = FMath::Clamp(LevelJsonNumber(Root, TEXT("gate_max_progress_before_final"), Config.Gate.MaxProgressBeforeFinal), 0.f, 0.99f);
	Config.Gate.FullOpenAfterWave = FMath::Max(0, FMath::RoundToInt(LevelJsonNumber(Root, TEXT("gate_full_open_after_wave"), static_cast<float>(Config.Gate.FullOpenAfterWave))));
	// Game designer 2026-10-22: the level ends through the fully opened gate (GateExitRules): "gate_exit": "off" | "auto" | "+x" | "-x",
	// or {"outward": ..., "fade_seconds": s, "return_to_menu_seconds": s}.
	{
		FString ExitSide;
		if (const TSharedPtr<FJsonObject> Exit = LevelJsonObject(Root, TEXT("gate_exit")))
		{
			ExitSide = LevelJsonString(Exit, TEXT("outward"), TEXT("auto"));
			Config.GateExit.FadeSeconds = FMath::Max(0.1f, LevelJsonNumber(Exit, TEXT("fade_seconds"), Config.GateExit.FadeSeconds));
			Config.GateExit.ReturnToMenuSeconds = FMath::Max(0.f, LevelJsonNumber(Exit, TEXT("return_to_menu_seconds"), Config.GateExit.ReturnToMenuSeconds));
			Config.GateExit.NextLevelSeconds = FMath::Max(0.f, LevelJsonNumber(Exit, TEXT("next_level_seconds"), Config.GateExit.NextLevelSeconds));
		}
		else
		{
			ExitSide = LevelJsonString(Root, TEXT("gate_exit"), TEXT("off"));
		}
		if (!GateExitRules::ParseSide(ExitSide, Config.GateExit.bEnabled, Config.GateExit.Side))
		{
			OutError = FString::Printf(TEXT("unknown gate_exit '%s' (off | auto | +x | -x)"), *ExitSide);
			return false;
		}
	}

	// 2026-10-22 b (campaign): the level starts behind a black screen that fades in over this many real seconds (0 = no fade).
	Config.StartFadeSeconds = FMath::Max(0.f, LevelJsonNumber(Root, TEXT("start_fade_seconds"), 0.f));
	// 2026-10-22 d: "intro_dialogue": false drops the mission start briefing on this level (not only on demo levels).
	{
		bool bIntro = true;
		Config.bIntroDialogue = !Root->TryGetBoolField(TEXT("intro_dialogue"), bIntro) || bIntro;
	}

	// UE-only (APC P2): the turret's shells for this map.
	if (const TSharedPtr<FJsonObject> Vehicle = LevelJsonObject(Root, TEXT("vehicle")))
	{
		Config.VehicleShells = FMath::Max(-1, FMath::RoundToInt(LevelJsonNumber(Vehicle, TEXT("shells"), -1.f)));
	}

	// UE-only (minimap 2026-10-18): the upper floors' walking heights and the mapped area.
	if (const TSharedPtr<FJsonObject> Minimap = LevelJsonObject(Root, TEXT("minimap")))
	{
		const TArray<TSharedPtr<FJsonValue>>* Floors = nullptr;
		if (Minimap->TryGetArrayField(TEXT("floors_cm"), Floors))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Floors)
			{
				double Z = 0.0;
				if (Value.IsValid() && Value->TryGetNumber(Z))
				{
					Config.MinimapFloorsCm.Add(static_cast<float>(Z));
				}
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Bounds = nullptr;
		if (Minimap->TryGetArrayField(TEXT("bounds_cm"), Bounds) && Bounds->Num() == 4)
		{
			double V[4] = {};
			bool bNumbers = true;
			for (int32 Index = 0; Index < 4; ++Index)
			{
				bNumbers &= (*Bounds)[Index].IsValid() && (*Bounds)[Index]->TryGetNumber(V[Index]);
			}
			if (bNumbers && V[2] > V[0] && V[3] > V[1])
			{
				Config.MinimapBoundsCm = FBox2D(FVector2D(V[0], V[1]), FVector2D(V[2], V[3]));
			}
		}
	}

	// UE-only (Demo 01 C10): demo game-mode flags.
	if (const TSharedPtr<FJsonObject> Demo = LevelJsonObject(Root, TEXT("demo")))
	{
		Config.Demo = ParseDemoSettings(*Demo);
	}

	// UE-only (environment 2026-10-22): the day cycle / weather start of this level.
	if (const TSharedPtr<FJsonObject> Environment = LevelJsonObject(Root, TEXT("environment")))
	{
		FString EnvironmentWarning;
		Config.Environment = ParseEnvironmentSettings(*Environment, &EnvironmentWarning);
		if (!EnvironmentWarning.IsEmpty())
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Level JSON %s: %s"), *Config.LevelId, *EnvironmentWarning);
		}
	}

	// Combat lab (game designer 2026-11-01, docs/design/COMBAT_LAB.md): the squad's cheats on by default and the endless enemy pressure.
	if (const TSharedPtr<FJsonObject> Cheats = LevelJsonObject(Root, TEXT("cheats")))
	{
		bool bFlag = false;
		Config.bCheatImmortal = Cheats->TryGetBoolField(TEXT("immortal"), bFlag) && bFlag;
		bFlag = false;
		Config.bCheatInfiniteAmmo = Cheats->TryGetBoolField(TEXT("infinite_ammo"), bFlag) && bFlag;
	}
	if (const TSharedPtr<FJsonObject> Pressure = LevelJsonObject(Root, TEXT("pressure")))
	{
		FString PressureError;
		if (!ParsePressureSettings(*Pressure, Config.Pressure, &PressureError))
		{
			OutError = FString::Printf(TEXT("pressure: %s"), *PressureError);
			return false;
		}
	}
	if (const TSharedPtr<FJsonObject> Route = LevelJsonObject(Root, TEXT("bot_route")))
	{
		FString RouteError;
		if (!BotRouteRules::Parse(*Route, Config.BotRoute, &RouteError))
		{
			OutError = RouteError;
			return false;
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* Waves = nullptr;
	if (Root->TryGetArrayField(TEXT("waves"), Waves))
	{
		for (const TSharedPtr<FJsonValue>& WaveValue : *Waves)
		{
			const TSharedPtr<FJsonObject> Wave = WaveValue.IsValid() ? WaveValue->AsObject() : nullptr;
			bool bActive = true;
			if (!Wave.IsValid() || (Wave->TryGetBoolField(TEXT("is_active"), bActive) && !bActive))
			{
				continue; // Godot _load_active_level_config keeps only active waves
			}
			FWaveDefinition Definition;
			Definition.WaveIndex = FMath::RoundToInt(LevelJsonNumber(Wave, TEXT("wave_index"), Config.Waves.Num() + 1));
			Definition.Name = FText::FromString(LevelJsonString(Wave, TEXT("name"), FString()));
			Definition.MaxSimultaneousEnemies = FMath::RoundToInt(LevelJsonNumber(Wave, TEXT("max_simultaneous_enemies"), 8.f));
			// UE-only (user decision 2026-10-11): horde waves start suddenly unless the wave says otherwise.
			bool bSudden = true;
			Definition.bSuddenStart = !Wave->TryGetBoolField(TEXT("sudden_start"), bSudden) || bSudden;
			const TArray<TSharedPtr<FJsonValue>>* Spawns = nullptr;
			if (Wave->TryGetArrayField(TEXT("spawns"), Spawns))
			{
				for (const TSharedPtr<FJsonValue>& SpawnValue : *Spawns)
				{
					const TSharedPtr<FJsonObject> Spawn = SpawnValue.IsValid() ? SpawnValue->AsObject() : nullptr;
					if (!Spawn.IsValid())
					{
						continue;
					}
					FEnemySpawnEntry Entry;
					const FString Type = LevelJsonString(Spawn, TEXT("enemy_type"), FString());
					if (!ParseEnemyType(Type, Entry.EnemyType))
					{
						OutError = FString::Printf(TEXT("wave %d: unknown enemy_type '%s'"), Definition.WaveIndex, *Type);
						return false;
					}
					Entry.Count = FMath::RoundToInt(LevelJsonNumber(Spawn, TEXT("count"), 1.f));
					Entry.SpawnLane = LevelJsonString(Spawn, TEXT("spawn_lane"), TEXT("ANY"));
					Entry.SpawnDelaySec = LevelJsonNumber(Spawn, TEXT("spawn_delay_sec"), 1.f);
					Entry.InitialDelaySec = LevelJsonNumber(Spawn, TEXT("initial_delay_sec"), 0.f);
					const TSharedPtr<FJsonObject> Stats = LevelJsonObject(Spawn, TEXT("custom_stats"));
					Entry.CustomHealth = LevelJsonNumber(Stats, TEXT("health"), LevelJsonNumber(Spawn, TEXT("custom_health"), 0.f));
					Entry.CustomDamage = LevelJsonNumber(Stats, TEXT("damage"), 0.f);
					Entry.CustomSpeed = LevelJsonNumber(Stats, TEXT("speed"), 0.f);
					Entry.CustomAttackRange = LevelJsonNumber(Stats, TEXT("attack_range"), 0.f);
					Entry.CustomAttackCooldown = LevelJsonNumber(Stats, TEXT("attack_cooldown"), 0.f);
					// Environment phase 2b: a Wave Editor row lock keeps the entry as written under any weather / time of day.
					bool bEnvironmentLock = false;
					Entry.bEnvironmentLocked = Spawn->TryGetBoolField(TEXT("environment_lock"), bEnvironmentLock) && bEnvironmentLock;
					Definition.Spawns.Add(Entry);
				}
			}
			const TSharedPtr<FJsonObject> Mods = LevelJsonObject(Wave, TEXT("wave_modifiers"));
			Definition.Modifiers.EnemyHpMult = LevelJsonNumber(Mods, TEXT("enemy_hp_mult"), 1.f);
			Definition.Modifiers.EnemyDamageMult = LevelJsonNumber(Mods, TEXT("enemy_damage_mult"), 1.f);
			Definition.Modifiers.EnemySpeedMult = LevelJsonNumber(Mods, TEXT("enemy_speed_mult"), 1.f);
			Definition.Modifiers.ColdDrainMult = LevelJsonNumber(Mods, TEXT("cold_drain_mult"), 1.f);
			Config.Waves.Add(Definition);
		}
	}

	// Godot _apply_stage_exploration_resources: squad_loadout with its .get() fallbacks.
	const TSharedPtr<FJsonObject> Loadout = LevelJsonObject(Root, TEXT("squad_loadout"));
	Config.SquadLoadout.SimulationMode = LevelJsonString(Loadout, TEXT("simulation_mode"), TEXT("EXPLORE_AND_COLLECT"));
	Config.SquadLoadout.PresetTier = LevelJsonString(Loadout, TEXT("preset_tier"), TEXT("STANDARD"));
	Config.SquadLoadout.TurretsCount = FMath::RoundToInt(LevelJsonNumber(Loadout, TEXT("turrets_count"), 1.f));
	Config.SquadLoadout.BarricadesCount = FMath::RoundToInt(LevelJsonNumber(Loadout, TEXT("barricades_count"), 2.f));
	Config.SquadLoadout.MinesCount = FMath::RoundToInt(LevelJsonNumber(Loadout, TEXT("mines_count"), 2.f));
	Config.SquadLoadout.MedkitsCount = FMath::RoundToInt(LevelJsonNumber(Loadout, TEXT("medkits_count"), 2.f));
	Config.SquadLoadout.M16Ammo = FMath::RoundToInt(LevelJsonNumber(Loadout, TEXT("m16_ammo"), 120.f));
	Config.SquadLoadout.PistolAmmo = FMath::RoundToInt(LevelJsonNumber(Loadout, TEXT("pistol_ammo"), 48.f));
	Config.SquadLoadout.GrenadesCount = FMath::RoundToInt(LevelJsonNumber(Loadout, TEXT("grenades_count"), -1.f));
	Config.SquadLoadout.HeatPacksCount = FMath::RoundToInt(LevelJsonNumber(Loadout, TEXT("heat_packs_count"), -1.f));
	OutConfig = MoveTemp(Config);
	return true;
}

bool LevelJsonRules::LoadLevel(const FString& FileName, FLevelCombatConfig& OutConfig, FString& OutError)
{
	// A bare name is looked up in the level folder; an absolute path (the -LevelJson= override) is used as it is.
	const FString Path = FPaths::IsRelative(FileName) ? GetLevelsDirectory() / FileName : FileName;
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		OutError = FString::Printf(TEXT("cannot read %s"), *Path);
		return false;
	}
	return ParseLevel(Json, FPaths::GetBaseFilename(FileName), OutConfig, OutError);
}
