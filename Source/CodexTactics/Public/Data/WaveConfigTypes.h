#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/CombatTypes.h"
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

	/** Grenades per operative (UE-only, Wave Editor «Гранаты», user request 2026-10-04); -1: the operatives keep their own. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level")
	int32 GrenadesCount = -1;
};

USTRUCT(BlueprintType)
struct CODEXTACTICS_API FLevelCombatConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level")
	FString LevelId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level")
	FText LevelName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level")
	float PrepPhaseDuration = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level")
	float WaveRestDuration = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level")
	TArray<FWaveDefinition> Waves;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level")
	FSquadLoadout SquadLoadout;
};

UCLASS(BlueprintType)
class CODEXTACTICS_API ULevelConfigAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level")
	FLevelCombatConfig Config;
};
