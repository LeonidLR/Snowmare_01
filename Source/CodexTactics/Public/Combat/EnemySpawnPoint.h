#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EnemySpawnPoint.generated.h"

/**
 * Designated spawn point for enemy waves.
 * Placed in levels to define lanes and spawn origins.
 * Godot reference: Scenes/movements/enemy_spawn_point.gd.
 */
UCLASS()
class CODEXTACTICS_API AEnemySpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	AEnemySpawnPoint();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Wave")
	FString SpawnLane = TEXT("ANY");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Wave")
	bool bIsActive = true;
};
