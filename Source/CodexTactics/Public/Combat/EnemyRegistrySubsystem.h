#pragma once

#include "CoreMinimal.h"
#include "Combat/EnemyRegistryRules.h"
#include "Subsystems/WorldSubsystem.h"
#include "EnemyRegistrySubsystem.generated.h"

class AActor;
class AEnemyCharacter;

/**
 * The world's enemies without a world actor scan (audit 2026-10-11 #2 / P2-P3; Godot reference: the "enemies" group,
 * get_tree().get_nodes_in_group). Every AEnemyCharacter registers in BeginPlay and leaves in EndPlay; any other actor
 * carrying the "Enemy" tag (CombatQueries::EnemyTag) is picked up when the world begins play (placed in the map), when
 * it is spawned with the tag, or by RegisterTaggedActor (a tag added after the spawn, e.g. a dev smoke's stand-in).
 * The per-frame "any live enemy near me?" queries (operative targeting, traps, turrets, panic / rage, HUD plates,
 * sight, pack tactics, vehicles) read it instead of TActorIterator<AActor> / TActorIterator<AEnemyCharacter>.
 *
 * Queries return a SNAPSHOT (inline storage, no heap allocation below 64 enemies): the caller may damage, kill, spawn
 * or destroy enemies while it walks the result. Before the world has begun play (actors not registered yet: editor
 * worlds, OnWorldBeginPlay code, the actors' own BeginPlay) and in a world without the subsystem they fall back to the
 * world scan, so every caller sees the same set the old iterator gave.
 */
UCLASS()
class CODEXTACTICS_API UEnemyRegistrySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Snapshot of the actors carrying the "Enemy" tag. */
	using FActorList = TArray<AActor*, TInlineAllocator<64>>;
	/** Snapshot of the enemy characters (alive or dying: everything between BeginPlay and EndPlay). */
	using FEnemyList = TArray<AEnemyCharacter*, TInlineAllocator<64>>;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/** AEnemyCharacter::BeginPlay. */
	void RegisterEnemy(AEnemyCharacter* Enemy);
	/** AEnemyCharacter::EndPlay. */
	void UnregisterEnemy(AEnemyCharacter* Enemy);
	/** Another actor that carries (or will carry) the "Enemy" tag; dropped automatically when it is destroyed. */
	void RegisterTaggedActor(AActor* Actor);

	/** Registered enemy characters (tests / probes). */
	int32 GetRegisteredEnemyCount() const { return Enemies.Num(); }

	/**
	 * Actors that carry the "Enemy" tag right now (an enemy loses it when it dies), enemy characters first in their
	 * registration order, then the other tagged actors. Callers keep their own health / hidden / distance checks.
	 */
	static FActorList GetTaggedEnemies(const UWorld* World);
	/** Every enemy character in the world (TActorIterator<AEnemyCharacter> without the scan). */
	static FEnemyList GetEnemyCharacters(const UWorld* World);
	/** True when the queries above read the registry; false: they scan the world (not begun play / no subsystem / off). */
	static bool IsServing(const UWorld* World);
	/** Console variable Codex.EnemyRegistry (default 1); 0 = the old world scans, for A / B measurements. */
	static bool IsEnabled();

private:
	void HandleActorSpawned(AActor* Actor);
	void PruneStale();

	EnemyRegistryRules::TOrderedRegistry<TWeakObjectPtr<AEnemyCharacter>> Enemies;
	EnemyRegistryRules::TOrderedRegistry<TWeakObjectPtr<AActor>> TaggedActors;
	FDelegateHandle SpawnedHandle;
};
