#include "Data/LevelJsonRules.h"
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
	FString WeatherName;
	if (Environment.TryGetStringField(TEXT("weather"), WeatherName))
	{
		EWeatherState Weather = EWeatherState::Overcast;
		if (EnvironmentRules::WeatherFromString(WeatherName, Weather))
		{
			Settings.bHasWeather = true;
			Settings.Weather = Weather;
		}
		else if (OutError)
		{
			*OutError = FString::Printf(TEXT("environment.weather: unknown weather '%s' (Clear / Overcast / Snowfall / Blizzard); ignored"), *WeatherName);
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
	return Settings;
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
