#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CodexLevelPropsSubsystem.generated.h"

class ACodexPropActor;
class FJsonObject;

/**
 * World subsystem for loading and spawning levelgen props (docs/design/levelgen/PROPS_LAYERS.md 3.1, task C6).
 * Runs at OnWorldBeginPlay. Reads <MapName>.props.json, validates against layout, snaps to slab Z,
 * spawns zero-ticking prop actors, applies tags (CodexProp, prop_layer=<layer>, prop_id=<id>),
 * registers UNavModifierComponent on nav-affecting props, and sets navmesh to DynamicModifiersOnly.
 * Zero ticking subsystem.
 */
UCLASS()
class CODEXTACTICS_API UCodexLevelPropsSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** Spawns props for the specified map name (e.g. "L_Gen_Depot_1") */
	int32 SpawnPropsForMap(
		const FString& MapName,
		const TSet<FString>* SkippedIds = nullptr,
		bool bRequireMesh = false);

	/**
	 * Spawns props from the given props JSON file and optional layout file.
	 * Returns the number of successfully spawned prop actors.
	 */
	int32 SpawnPropsFromFile(
		const FString& PropsFilePath,
		const FString& LayoutFilePath = FString(),
		const TSet<FString>* SkippedIds = nullptr,
		bool bRequireMesh = false);

	/** Destroys all spawned prop actors and resets layer counts */
	void ClearSpawnedProps();

	/** Dumps counts per layer and dropped IDs to LogCodexTactics (console command Codex.Props.Dump) */
	void DumpProps() const;

	/** Total count of live spawned prop actors */
	int32 GetSpawnedCount() const;

	/** Count of live spawned prop actors in a given layer */
	int32 GetSpawnedCountForLayer(const FString& LayerId) const;

	/** All spawned prop actors */
	const TArray<TWeakObjectPtr<ACodexPropActor>>& GetSpawnedProps() const { return SpawnedProps; }

	/** IDs of props dropped during validation */
	const TArray<FString>& GetDroppedIds() const { return DroppedPropIds; }

	/** Execution time of the last spawn in milliseconds */
	double GetLastSpawnDurationMs() const { return LastSpawnDurationMs; }

	/** True if props have been spawned in this world */
	bool HasSpawned() const { return bHasSpawned; }

	/** Resolves the UClass to spawn for a given variant name (using class_map or native fallback) */
	UClass* ResolvePropClass(const FString& Variant) const;

	/** True if Codex.Props.Enabled != 0 */
	static bool IsPropsEnabled();

	/** True if Codex.Props.MinesInert != 0 */
	static bool AreMinesInert();

	/** Default path to <MapName>.props.json */
	static FString GetDefaultPropsPathForMap(const FString& MapName);

	/** Default path to <LayoutName>.json */
	static FString GetDefaultLayoutPathForMap(const FString& LayoutName);

private:
	/** Loads balance file (kinds, class_map, budgets) */
	void LoadBalanceFile();

	/** Resolves layout file path for a layout name, checking accepted and fixture directories */
	FString ResolveLayoutPath(const FString& LayoutName, const FString& PropsDir) const;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<ACodexPropActor>> SpawnedProps;

	UPROPERTY(Transient)
	TMap<FString, FString> VariantClassMap;

	TMap<FString, FVector> VariantExtents;
	TMap<FString, bool> VariantNavAffecting;
	TMap<FString, float> VariantHP;
	TMap<FString, float> VariantExplosionRadius;
	TMap<FString, float> VariantExplosionDamage;
	TMap<FString, float> VariantBlastRadius;
	TMap<FString, float> VariantBlastDamage;
	TMap<FString, float> VariantTriggerRadius;
	mutable TMap<FString, TSubclassOf<AActor>> ResolvedClassCache;
	TMap<FString, TArray<TWeakObjectPtr<ACodexPropActor>>> PropsByLayer;
	TArray<FString> DroppedPropIds;

	double LastSpawnDurationMs = 0.0;
	bool bHasSpawned = false;
	bool bBalanceLoaded = false;
};
