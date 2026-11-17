#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/WorldSubsystem.h"
#include "CodexLevelPropsSubsystem.generated.h"

class ACodexPropActor;
class FJsonObject;
struct FLevelPropsResolvedObject;

/**
 * World subsystem for loading and spawning levelgen props (docs/design/levelgen/PROPS_LAYERS.md 3.1, task C6).
 * Runs at OnWorldBeginPlay. Reads <MapName>.props.json, validates against layout, snaps to slab Z,
 * spawns zero-ticking prop actors, applies tags (CodexProp, prop_layer=<layer>, prop_id=<id>),
 * and registers UNavModifierComponent on nav-affecting props (leaving navmesh runtime generation setting intact).
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

	/** Props of the last spawn that got the greybox look (Codex.Props.Greybox, G003). */
	int32 GetGreyboxCount() const { return GreyboxCount; }

	/**
	 * A live map (set by OnWorldBeginPlay, G003b): the props spawn as the game's real objects (Codex.Props.Real) and the spawn is spread
	 * over frames (props_balance.json gameplay.spawn_frame_budget_ms). Off for a direct SpawnPropsFromFile (tests): everything at once.
	 */
	bool bLiveSpawn = false;

	/** A live spawn still has props to place on the next frames. */
	bool IsSpawnPending() const { return Session.IsValid(); }

	/** The real game objects of the last spawn (barrels, loot crates / piles, mines, wires, turrets), tagged like the props. */
	const TArray<TWeakObjectPtr<AActor>>& GetSpawnedGameplayActors() const { return SpawnedGameplay; }

	/** Live count of GetSpawnedGameplayActors. */
	int32 GetSpawnedGameplayCount() const;

	/** The longest frame of the last spawn (ms; the first frame includes the file / layout / validation work). */
	double GetLastMaxFrameMs() const { return LastMaxFrameMs; }

	/** Frames the last spawn took. */
	int32 GetLastSpawnFrames() const { return LastSpawnFrames; }

	/** File + layout + validation time of the last spawn (ms). */
	double GetLastPrepareMs() const { return LastPrepareMs; }

	/** Resolves the UClass to spawn for a given variant name (using class_map or native fallback) */
	UClass* ResolvePropClass(const FString& Variant);

	/** True if Codex.Props.Enabled != 0 */
	static bool IsPropsEnabled();

	/** True if Codex.Props.MinesInert != 0 */
	static bool AreMinesInert();

	/** Default path to <MapName>.props.json */
	static FString GetDefaultPropsPathForMap(const FString& MapName);

	/** Default path to <LayoutName>.json */
	static FString GetDefaultLayoutPathForMap(const FString& LayoutName);

private:
	struct FPropsSpawnSession;

	/** Spawns the session's next objects until BudgetMs is spent (0 = all); true when some are left for the next frame. */
	bool ProcessSession(double BudgetMs);
	/** The attach pass, the timings and the log line; ends the session. */
	void FinishSession();
	void SpawnOne(FPropsSpawnSession& S, const FLevelPropsResolvedObject& ResObj);
	/** The real game object of a prop (gameplay.classes); false = none (the ACodexProp* actor / greybox then). */
	bool SpawnGameplayActor(FPropsSpawnSession& S, const FLevelPropsResolvedObject& ResObj, const FVector& Ground, const FRotator& Rotation);
	static void TagProp(AActor* Actor, const FString& LayerId, const FString& PropId);

	TSharedPtr<FPropsSpawnSession> Session;
	FTSTicker::FDelegateHandle SpawnTicker;
	TArray<TWeakObjectPtr<AActor>> SpawnedGameplay;
	TMap<FString, TArray<TWeakObjectPtr<AActor>>> GameplayByLayer;
	double LastMaxFrameMs = 0.0;
	double LastPrepareMs = 0.0;
	int32 LastSpawnFrames = 0;

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
	UPROPERTY(Transient)
	TMap<FString, TSubclassOf<AActor>> ResolvedClassCache;
	TMap<FString, TArray<TWeakObjectPtr<ACodexPropActor>>> PropsByLayer;
	/** The parsed props_balance.json (greybox look; null until LoadBalanceFile). */
	TSharedPtr<FJsonObject> BalanceRoot;
	int32 GreyboxCount = 0;
	TArray<FString> DroppedPropIds;

	double LastSpawnDurationMs = 0.0;
	bool bHasSpawned = false;
	bool bBalanceLoaded = false;
};
