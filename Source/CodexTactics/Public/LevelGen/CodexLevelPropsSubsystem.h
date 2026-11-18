#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/WorldSubsystem.h"
#include "CodexLevelPropsSubsystem.generated.h"

class ACodexPropActor;
class FJsonObject;
class UCodexLevelPropsSubsystem;
struct FLevelPropsResolvedObject;

/** A props spawn finished (all its frames done): the subsystem (G003b; smokes / AI wait for it). */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnLevelPropsSpawned, UCodexLevelPropsSubsystem*);
/** Blueprint form of FOnLevelPropsSpawned: the live prop count (greybox props + game objects). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLevelPropsSpawnedDynamic, int32, SpawnedCount);

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

	/** Spawns props for the specified map name (e.g. "L_Gen_Depot_1"); bLive as SpawnPropsFromFile. */
	int32 SpawnPropsForMap(
		const FString& MapName,
		const TSet<FString>* SkippedIds = nullptr,
		bool bRequireMesh = false,
		bool bLive = false);

	/**
	 * Spawns props from the given props JSON file and optional layout file. Any earlier spawn (and a pending one) is cleared first.
	 * bLive (OnWorldBeginPlay, G003b): the props spawn as the game's real objects (Codex.Props.Real); the file / layout / validation work runs
	 * on a worker thread and the spawn then is spread over frames (props_balance.json gameplay.spawn_frame_budget_ms); false (tests, tools):
	 * greybox props, everything at once on this thread.
	 * Returns the number of live prop actors so far (a live spawn may still be pending: IsSpawnPending / OnPropsSpawned).
	 */
	int32 SpawnPropsFromFile(
		const FString& PropsFilePath,
		const FString& LayoutFilePath = FString(),
		const TSet<FString>* SkippedIds = nullptr,
		bool bRequireMesh = false,
		bool bLive = false);

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

	/** True once the last spawn finished (every frame of a frame-sliced spawn done; reset by ClearSpawnedProps). */
	bool HasSpawned() const { return bHasSpawned; }

	/** Fired when a spawn finishes (after its last frame; at once for a direct spawn). */
	FOnLevelPropsSpawned OnPropsSpawned;

	/** Blueprint form of OnPropsSpawned. */
	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|LevelGen")
	FOnLevelPropsSpawnedDynamic OnPropsSpawnedDynamic;

	/** Props of the last spawn that got the greybox look (Codex.Props.Greybox, G003). */
	int32 GetGreyboxCount() const { return GreyboxCount; }

	/** A live spawn still has props to place on the next frames. */
	bool IsSpawnPending() const { return Session.IsValid(); }

	/** The real game objects of the last spawn (barrels, loot crates / piles, mines, wires, turrets), tagged like the props. */
	const TArray<TWeakObjectPtr<AActor>>& GetSpawnedGameplayActors() const { return SpawnedGameplay; }

	/** Live count of GetSpawnedGameplayActors. */
	int32 GetSpawnedGameplayCount() const;

	/** The longest game-thread frame of the last spawn (ms; frame 0 includes the spawn call's own work - all of the file / layout /
	 * validation work for a direct spawn, only the set-up for a live one, whose prepare runs on a worker thread). */
	double GetLastMaxFrameMs() const { return LastMaxFrameMs; }

	/** Frames the last spawn took. */
	int32 GetLastSpawnFrames() const { return LastSpawnFrames; }

	/** File + layout + validation time of the last spawn (ms; on a worker thread for a live spawn). */
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

	/** The pure-data half of a spawn (file, layout view, validator, turret / power rules) into the session; no UObject access (worker thread). */
	static void PrepareSession(FPropsSpawnSession& S);
	/** Takes a finished prepare on the game thread (dropped ids, greybox rule); false = it failed and the session is dropped. */
	bool TakePreparedSession();
	/** Spawns the session's next objects until BudgetMs is spent (0 = all); true when some are left for the next frame. */
	bool ProcessSession(double BudgetMs);
	/** The attach pass, the timings and the log line; ends the session and fires OnPropsSpawned. */
	void FinishSession();
	/** Removes the frame-slice ticker (if any) and drops the pending session. */
	void StopSpawnTicker();
	void SpawnOne(FPropsSpawnSession& S, const FLevelPropsResolvedObject& ResObj);
	/** The real game object of a prop (gameplay.classes); false = none (the ACodexProp* actor / greybox then). */
	bool SpawnGameplayActor(FPropsSpawnSession& S, const FLevelPropsResolvedObject& ResObj, const FVector& Ground, const FRotator& Rotation);
	static void TagProp(AActor* Actor, const FString& LayerId, const FString& PropId);

	TSharedPtr<FPropsSpawnSession> Session;
	FTSTicker::FDelegateHandle SpawnTicker;
	/** Bumped by every spawn: a ticker of an older spawn stops itself. */
	uint32 SpawnGeneration = 0;
	TArray<TWeakObjectPtr<AActor>> SpawnedGameplay;
	TMap<FString, TArray<TWeakObjectPtr<AActor>>> GameplayByLayer;
	double LastMaxFrameMs = 0.0;
	double LastPrepareMs = 0.0;
	int32 LastSpawnFrames = 0;

	/** Loads balance file (kinds, class_map, budgets) */
	void LoadBalanceFile();

	/** Resolves layout file path for a layout name, checking accepted and fixture directories */
	static FString ResolveLayoutPath(const FString& LayoutName, const FString& PropsDir);

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
