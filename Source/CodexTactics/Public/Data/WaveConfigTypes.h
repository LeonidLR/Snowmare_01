#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/CombatTypes.h"
#include "Quests/GateProgressRules.h"
#include "WaveConfigTypes.generated.h"

USTRUCT(BlueprintType)
struct CODEXTACTICS_API FEnemySpawnEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave|Spawn")
	EEnemyArchetype EnemyType = EEnemyArchetype::FrostHound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave|Spawn", meta = (ClampMin = "1"))
	int32 Count = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave|Spawn")
	FString SpawnLane = TEXT("ANY");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave|Spawn", meta = (ClampMin = "0.0"))
	float SpawnDelaySec = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave|Spawn", meta = (ClampMin = "0.0"))
	float InitialDelaySec = 0.0f;

	/** custom_stats.health (0 = the type's own). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave|Spawn")
	float CustomHealth = 0.0f;

	/** custom_stats.damage (0 = the type's own), times the wave's damage multiplier like Godot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave|Spawn")
	float CustomDamage = 0.0f;

	/** custom_stats.speed in m/s (0 = the type's own), times the wave's speed multiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave|Spawn")
	float CustomSpeed = 0.0f;

	/** custom_stats.attack_range in m (0 = the type's own). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave|Spawn")
	float CustomAttackRange = 0.0f;

	/** custom_stats.attack_cooldown in s (0 = the type's own). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave|Spawn")
	float CustomAttackCooldown = 0.0f;
};

USTRUCT(BlueprintType)
struct CODEXTACTICS_API FWaveModifiers
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave|Modifiers")
	float EnemyHpMult = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave|Modifiers")
	float EnemyDamageMult = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave|Modifiers")
	float EnemySpeedMult = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave|Modifiers")
	float ColdDrainMult = 1.0f;
};

USTRUCT(BlueprintType)
struct CODEXTACTICS_API FWaveDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave")
	int32 WaveIndex = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave")
	FText Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave")
	int32 MaxSimultaneousEnemies = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave")
	TArray<FEnemySpawnEntry> Spawns;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave")
	FWaveModifiers Modifiers;

	/**
	 * Level JSON "sudden_start" (UE-only, user decision 2026-10-11; default true): the horde spawns at once and the fight
	 * opens in the free tactical pause with the camera flyover. False keeps the old start of this wave (wave 1: cutscene
	 * + prep_phase_duration countdown; later waves: wave_rest_duration countdown).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave")
	bool bSuddenStart = true;

	int32 GetTotalEnemyCount() const
	{
		int32 Total = 0;
		for (const FEnemySpawnEntry& Entry : Spawns)
		{
			Total += Entry.Count;
		}
		return Total;
	}
};

/**
 * Combat supply of a level (Godot level JSON "squad_loadout"; main.gd _apply_stage_exploration_resources).
 * Defaults are Godot's .get() fallbacks for a level without the block.
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FSquadLoadout
{
	GENERATED_BODY()

	/** EXPLORE_AND_COLLECT (what the squad collected), STARTING_UNIQUE (the role starting set) or EDITOR_PRESET. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level")
	FString SimulationMode = TEXT("EXPLORE_AND_COLLECT");

	/** EDITOR_PRESET tier: MINIMAL, STANDARD, MAXIMAL or CUSTOM (the numbers below). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level")
	FString PresetTier = TEXT("STANDARD");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level")
	int32 TurretsCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level")
	int32 BarricadesCount = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level")
	int32 MinesCount = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level")
	int32 MedkitsCount = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level")
	int32 M16Ammo = 120;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level")
	int32 PistolAmmo = 48;

	/** Grenades per operative (UE-only, Wave Editor Â«Ð“Ñ€Ð°Ð½Ð°Ñ‚Ñ‹Â», user request 2026-10-04); -1: the operatives keep their own. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level")
	int32 GrenadesCount = -1;

	/**
	 * "heat_packs_count" (UE-only, Demo 01 2026-10-12): heat packs EACH operative of the starting squad spawns with,
	 * replacing the tactical_heat.json role kit (C1 / E1 / M2); clamped to heat_pack_max_stack. -1 (default): the role kit.
	 * The demo sets 0 so the first heat pack comes from the A1 hut crate.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level")
	int32 HeatPacksCount = -1;
};

/**
 * How the fight of a level starts (user request 2026-10-06, level JSON "combat_start"). Auto: by ambush when the map
 * holds patrols (an APatrolRouteActor or an enemy with a route / escort leader), else by the button / combat zone.
 */
UENUM(BlueprintType)
enum class ECombatStartMode : uint8
{
	/** "auto": ambush on maps with patrols, the button elsewhere. */
	Auto,
	/** "ambush": the fight starts when the squad attacks an enemy or an enemy detects the squad; Â«ÐÐ°Ñ‡Ð°Ñ‚ÑŒ Ð±Ð¾Ð¹Â» is hidden. */
	Ambush,
	/** "button": the classic flow (main menu Â«ÐÐ°Ñ‡Ð°Ñ‚ÑŒ Ð±Ð¾Ð¹Â» / combat zone -> cutscene -> preparation -> wave). */
	Button
};

/**
 * Demo game-mode flags of a level (level JSON "demo" object, Demo 01 producer plan C10): set by the demo level's data,
 * not by the global BP defaults. Applied by UDemoModeSubsystem; no "demo" object = every flag off (normal game).
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FDemoModeSettings
{
	GENERATED_BODY()

	/** True when the level JSON has a "demo" object. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level|Demo")
	bool bDemo = false;

	/** "panic": false switches Panic / stress off on every operative (UPanicComponent::bEnabled). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level|Demo")
	bool bPanicEnabled = true;

	/** "vision_cones": true shows every operative / enemy vision cone (bShowVisionCone), false hides them; absent = the BP defaults. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level|Demo")
	bool bOverrideVisionCones = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level|Demo")
	bool bVisionCones = true;

	/** "tutorial": true runs the tutorial steps file below (the player's tutorial ON/OFF setting still applies). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level|Demo")
	bool bTutorial = false;

	/** "tutorial_steps": Content/Data/Tutorial/<file> (default demo_01_steps.json). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level|Demo")
	FString TutorialSteps;

	/** "hud": "demo" hides the debug squad panel and the labels above operatives (ACodexTacticsHUD::bDemoHudPreset). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level|Demo")
	bool bDemoHud = false;

	/**
	 * "intro_dialogue": false skips the blocking mission-start briefing window (no window, no pause; the tutorial's A0
	 * walk-and-talk carries its content). Default true = classic behaviour; with a "demo" object and no key the parser
	 * sets false (user decision 2026-10-11).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level|Demo")
	bool bIntroDialogue = true;
};

USTRUCT(BlueprintType)
struct CODEXTACTICS_API FLevelCombatConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level")
	FString LevelId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level")
	FText LevelName;

	/** "prep_phase_duration": countdown before wave 1, s â€” only for a wave with "sudden_start": false (2026-10-11). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level")
	float PrepPhaseDuration = 60.0f;

	/** "wave_rest_duration": countdown before a later wave, s â€” only for a wave with "sudden_start": false (2026-10-11). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level")
	float WaveRestDuration = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level")
	TArray<FWaveDefinition> Waves;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level")
	FSquadLoadout SquadLoadout;

	/** "combat_start": "auto" (default) | "ambush" | "button". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level")
	ECombatStartMode CombatStart = ECombatStartMode::Auto;

	/** "patrol_search_seconds": how long patrols hunt after a trap; <= 0 = enemy_perception.json (60 s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level")
	float PatrolSearchSeconds = -1.f;

	/** "horde_enabled": false switches the horde after a long real-time fight off on this level (UHordeSubsystem). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level")
	bool bHordeEnabled = true;

	/**
	 * "horde": this level's overrides of Content/Data/AI/horde.json (same keys, e.g. {"trigger_seconds": 180,
	 * "count": 10}), kept as JSON text; empty = the file's values (HordeRules::ResolveForLevel).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level", meta = (MultiLine = true))
	FString HordeOverrideJson;

	/**
	 * Blast gate tied to the generator during the wave fight (user request 2026-10-11): "gate_open_mode",
	 * "gate_progress_per_second", "gate_max_progress_before_final", "gate_full_open_after_wave" (GateProgressRules).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level")
	FGateProgressConfig Gate;
	/** "demo": demo game-mode flags (Panic off, vision cones, tutorial, demo HUD preset). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level")
	FDemoModeSettings Demo;
	/**
	 * "vehicle": {"shells": N} (APC P2, UE-only): the turret's 30 mm shells for this map; -1 = the vehicle's own value
	 * (Blueprint / weapons_tuning.json "vehicle_weapons.apc_turret.shells_per_map").
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level")
	int32 VehicleShells = -1;
};

UCLASS(BlueprintType)
class CODEXTACTICS_API ULevelConfigAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level")
	FLevelCombatConfig Config;
};
