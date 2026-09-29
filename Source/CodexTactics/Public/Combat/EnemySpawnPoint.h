#pragma once

#include "CoreMinimal.h"
#include "Data/CombatTypes.h"
#include "GameFramework/Actor.h"
#include "EnemySpawnPoint.generated.h"

/** Which enemies a spawn point accepts (Godot enemy_spawn_point.gd allowed_enemy_type). */
UENUM(BlueprintType)
enum class EEnemySpawnFilter : uint8
{
	All,
	Hound,
	Spitter,
	Brute,
	Cutter
};

/**
 * Designated spawn point for enemy waves.
 * Placed in levels to define lanes and spawn origins.
 * Godot reference: Scenes/movements/enemy_spawn_point.gd (lane_name, allowed_enemy_type, the dynamic flank breach group).
 */
UCLASS()
class CODEXTACTICS_API AEnemySpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	AEnemySpawnPoint();

	/** Godot lane_name: a wave entry's spawn_lane matches when either name contains the other (case-insensitive). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Wave")
	FString SpawnLane = TEXT("ANY");

	/** Godot allowed_enemy_type: only this enemy type spawns here (All = any). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Wave")
	EEnemySpawnFilter AllowedEnemyType = EEnemySpawnFilter::All;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Wave")
	bool bIsActive = true;

	/** Godot is_dynamic: a hidden flank / rear breach point — never used by regular wave spawns. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Wave|Dynamic Breach")
	bool bIsDynamic = false;

	/** Godot active_waves (exported, but main.gd uses the mission's random breach wave instead). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Wave|Dynamic Breach", meta = (EditCondition = "bIsDynamic"))
	TArray<int32> ActiveWaves = { 2, 3 };

	/** Godot activation_chance: roll at the start of the breach wave. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Wave|Dynamic Breach", meta = (EditCondition = "bIsDynamic", ClampMin = "0", ClampMax = "1"))
	float ActivationChance = 0.7f;

	/** Godot warning_lead_time (exported; the breach spawns without a warning). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Wave|Dynamic Breach", meta = (EditCondition = "bIsDynamic"))
	float WarningLeadTime = 3.5f;

	/** Godot enemy_count. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Wave|Dynamic Breach", meta = (EditCondition = "bIsDynamic"))
	int32 EnemyCount = 4;

	/** Godot breach_enemy_type (HOUND / CUTTER / SPITTER / BRUTE; anything else spawns hounds like Godot's match). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Wave|Dynamic Breach", meta = (EditCondition = "bIsDynamic"))
	EEnemyArchetype BreachEnemyType = EEnemyArchetype::FrostHound;

	/** Godot allowed_enemy_type == "ALL" or == the wave entry's type. */
	bool Accepts(EEnemyArchetype Type) const;
};
