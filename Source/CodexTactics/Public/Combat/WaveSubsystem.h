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

	/** Enemies of the current wave when it started (Godot total_wave_enemies). */
	int32 GetTotalWaveEnemies() const { return TotalWaveEnemies; }

	/**
	 * Cold drain multiplier of the current level wave (Godot wave_modifiers.cold_drain_mult written to every operative's
	 * cold_rate_modifier); camera zones override it while an operative is inside. 1 without a level wave.
	 */
	float GetColdDrainMultiplier() const { return ColdDrainMultiplier; }

	/**
	 * Spawn location for an enemy of Type on Lane (Godot _get_enemy_spawn_pos): a random active, non-dynamic point whose
	 * lane matches and whose allowed type accepts Type; else any non-dynamic point; else any point; else the yard.
	 */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Wave")
	FVector GetSpawnLocationForLane(const FString& Lane, EEnemyArchetype Type = EEnemyArchetype::Base) const;

	/**
	 * A free spot for one more enemy near Point (UE-only; user report 2026-10-04: a whole wave spawned into one point,
	 * the capsules inside each other, and stood there — Godot's physics pushed overlapping bodies apart, UE's does not):
	 * Point itself, else rings of 1.5 / 3 / 4.5 / 6 m, each spot projected onto the navmesh and free of pawns and walls.
	 * Point (projected if possible) when nothing is free; a spawn point far off the navmesh is logged once.
	 */
	FVector FindFreeSpawnSpot(const FVector& Point) const;
	/** Spawn points already reported as off the navmesh. */
	mutable TSet<FVector> WarnedSpawnPoints;

	/** Wave (1-3) of the mission's flank breach (Godot dynamic_breach_wave: [1, 2, 3] shuffled, Susanin takes one first). */
	int32 GetDynamicBreachWave() const { return DynamicBreachWave; }
	/** Wave of the Susanin rescue event (Godot susanin_rescue_wave). */
	int32 GetSusaninRescueWave() const { return SusaninRescueWave; }
	void SetRandomEventWaves(int32 BreachWave, int32 SusaninWave) { DynamicBreachWave = BreachWave; SusaninRescueWave = SusaninWave; }
	bool IsDynamicBreachTriggered() const { return bDynamicBreachTriggered; }
	int32 GetPendingRandomEventCount() const { return PendingRandomEvents.Num(); }

	/**
	 * Godot _check_dynamic_flank_spawners: in the breach wave the first dynamic point that passes its activation roll
	 * breaks through 4-7 s later (at once with bInstantRandomEvents); during turn-based combat the breach waits for
	 * the fight to end.
	 */
	void CheckDynamicFlankSpawners(int32 WaveNum);

	/**
	 * Godot EnemySpawnPoint.trigger_breach: the pack spawns at once around the point with the HQ radio line; after
	 * CameraDelay the camera shows the point for 1.8 s with the squad's line, then returns to the leader.
	 */
	void TriggerBreach(AEnemySpawnPoint* Point, float CameraDelay = 1.f);

	/** Queues Event until turn-based combat ends (Godot _pending_random_events), or runs it now outside a fight. */
	void RunOrDeferRandomEvent(TFunction<void()> Event);

	/** Godot is_headless_test_mode: random events fire without their delays (smokes). */
	bool bInstantRandomEvents = false;

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

	int32 DynamicBreachWave = -1;
	int32 SusaninRescueWave = -1;
	bool bDynamicBreachTriggered = false;
	/** Godot _pending_random_events: run when turn-based combat ends. */
	TArray<TFunction<void()>> PendingRandomEvents;

	bool IsTurnBased() const;
	void SpawnBreachPack(const AEnemySpawnPoint& Point);

	/** Godot _spawn_custom_json_wave: every enemy of the wave at once, modifiers applied, radio line with counts. */
	void SpawnLevelWave(const FWaveDefinition& Def);
	/**
	 * Ambush fight (UGameFlowSubsystem::IsAmbushFight, user request 2026-10-06): the level's living enemies (patrols,
	 * guards) are the wave — none are spawned; the fight is won when they are all down.
	 */
	void AdoptLevelEnemies();

	UFUNCTION()
	void HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode);

	void HandleEnemyDied(AEnemyCharacter* Enemy);
	void ProcessPendingSpawns(float DeltaTime);
	void CheckWaveCompletion();
};
