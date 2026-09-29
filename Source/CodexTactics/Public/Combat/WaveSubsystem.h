#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Data/CombatTypes.h"
#include "Data/WaveConfigTypes.h"
#include "WaveSubsystem.generated.h"

class AEnemyCharacter;
class AEnemySpawnPoint;
class ULevelConfigAsset;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWaveStartedDynamic, int32, WaveIndex, int32, TotalEnemies);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWaveClearedDynamic, int32, WaveIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnEnemySpawnedDynamic, AEnemyCharacter*, Enemy, EEnemyArchetype, Archetype);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnEnemySpawnedNative, AEnemyCharacter*, EEnemyArchetype);

/**
 * World subsystem managing enemy waves, spawn queues, and pacing.
 * Bridges UGameFlowSubsystem combat phases with enemy lifecycle.
 * Godot reference: Scenes/movements/combat_wave_controller.gd; level waves: main.gd _spawn_custom_json_wave (the whole
 * wave appears at once — spawn_delay_sec / max_simultaneous_enemies are ignored like in Godot, user decision
 * 2026-09-29 — with the wave modifiers and custom_stats.health applied).
 */
UCLASS()
class CODEXTACTICS_API UWaveSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Sets level config asset containing wave definitions. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Wave")
	void SetLevelConfig(ULevelConfigAsset* InConfig);

	/** Starts a specific wave, populating pending spawns from LevelConfig or defaults. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Wave")
	void StartWave(int32 WaveIndex);

	/** Spawns a single enemy of the specified archetype at Location. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Wave")
	AEnemyCharacter* SpawnEnemy(EEnemyArchetype Archetype, const FVector& Location, const FRotator& Rotation = FRotator::ZeroRotator);

	/** Destroys all currently alive enemies immediately. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Wave")
	void ClearAllEnemies();

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Wave")
	int32 GetAliveEnemyCount() const;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Wave")
	int32 GetRemainingSpawnCount() const { return PendingSpawns.Num(); }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Wave")
	int32 GetCurrentWaveIndex() const { return CurrentWaveIndex; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Wave")
	bool IsWaveActive() const { return bWaveActive; }

	/**
	 * Cold drain multiplier of the current level wave (Godot wave_modifiers.cold_drain_mult written to every operative's
	 * cold_rate_modifier); camera zones override it while an operative is inside. 1 without a level wave.
	 */
	float GetColdDrainMultiplier() const { return ColdDrainMultiplier; }

	/** Finds an appropriate spawn location given a lane name. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Wave")
	FVector GetSpawnLocationForLane(const FString& Lane) const;

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Wave")
	FOnWaveStartedDynamic OnWaveStarted;

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Wave")
	FOnWaveClearedDynamic OnWaveCleared;

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Wave")
	FOnEnemySpawnedDynamic OnEnemySpawned;

	FOnEnemySpawnedNative OnEnemySpawnedNative;

private:
	UPROPERTY()
	TObjectPtr<ULevelConfigAsset> LevelConfig;

	TArray<TWeakObjectPtr<AEnemyCharacter>> AliveEnemies;
	TArray<FEnemySpawnEntry> PendingSpawns;

	int32 CurrentWaveIndex = 0;
	int32 TotalWaveEnemies = 0;
	int32 MaxSimultaneousEnemies = 8;
	bool bWaveActive = false;
	float SpawnTimer = 0.0f;
	float ColdDrainMultiplier = 1.0f;

	/** Godot _spawn_custom_json_wave: every enemy of the wave at once, modifiers applied, radio line with counts. */
	void SpawnLevelWave(const FWaveDefinition& Def);

	UFUNCTION()
	void HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode);

	void HandleEnemyDied(AEnemyCharacter* Enemy);
	void ProcessPendingSpawns(float DeltaTime);
	void CheckWaveCompletion();
};
