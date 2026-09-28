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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave|Spawn")
	float CustomHealth = 0.0f;
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
};

UCLASS(BlueprintType)
class CODEXTACTICS_API ULevelConfigAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level")
	FLevelCombatConfig Config;
};
