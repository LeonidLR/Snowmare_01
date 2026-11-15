#pragma once

// UE-only (no Godot reference): asynchronous world preparation before the squad spawns (004 enemy-appear hitch, 2026-11-12 / 11-15).

#include "CoreMinimal.h"
#include "Core/GameplayPreloadCatalog.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayPreloadSubsystem.generated.h"

class ACodexTacticsGameMode;
struct FStreamableHandle;
class USkinnedAsset;

/** Public view of the world preload (UGameplayPreloadSubsystem::GetState). */
enum class EGameplayPreloadState : uint8
{
	/** No preload in this world (no Codex game mode, editor / preview world). */
	NotApplicable,
	/** Switched off by Codex.Preload.Enabled 0: the old synchronous first-use loads run. */
	Disabled,
	/** Loading / binding: startup gates hold the squad, clocks and input. */
	Preparing,
	/** Every required asset resident and bound. */
	Ready,
	/** A required asset missing, the request rejected or timed out; gameplay is released anyway (fail-open). */
	Failed,
};

/** Asynchronously prepares one game world before clocks, movement and the start cover resume. UE-only. */
UCLASS()
class CODEXTACTICS_API UGameplayPreloadSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** The preload state of World (NotApplicable for a null world or one without the subsystem). */
	static EGameplayPreloadState GetState(const UWorld* World);
	/** "not-applicable" / "disabled" / "preparing" / "ready" / "failed" (logs, smokes). */
	static FString StateName(EGameplayPreloadState State);
	/** True when preparation succeeded, was disabled or does not apply. False while preparing AND after a failure. */
	static bool IsGameplayReady(const UWorld* World);
	/** True when the startup gates are released (Ready, Failed, Disabled or not applicable): the fail-open gameplay gate. */
	static bool IsGameplayReleased(const UWorld* World) { return !IsPreparing(World); }
	static bool IsPreparing(const UWorld* World);
	/** Codex.Preload.Enabled (1 default): 0 skips the async preparation and keeps the synchronous first-use loads (A/B of the hitch). */
	static bool IsPreloadEnabled();
	/** Codex.Preload.WorldPause (1 default): also pause the world (engine pause) while preparing, on top of the explicit gates. */
	static bool UsesWorldPause();
	bool IsReady() const { return Phase == EPhase::Ready; }
	bool HasFailed() const { return bFailed; }
	bool CanBindAnimationAssets() const { return Phase == EPhase::Binding || IsReady() || HasFailed(); }
	FSimpleMulticastDelegate OnAssetsPrepared;
	bool IsKnownMissing(const FSoftObjectPath& Path) const { return OptionalMisses.Contains(Path); }
	/** Grants at most one scheduled Pressure/PMC dispatch per world frame. */
	bool TryClaimAutomaticSpawn();
	/** Called once after tuning/level config; completion binds pending instances before releasing startup gates. */
	void StartPreparation(ACodexTacticsGameMode& Mode);

	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
	enum class EPhase : uint8 { WaitingForMode, Initial, Dependencies, Compiling, Binding, Ready, Failed, Stopped, Disabled };
	void RequestBatch(TArray<FGameplayPreloadPath> Paths);
	void OnBatchLoaded();
	void GatherDependencies(UObject* Object, TArray<FGameplayPreloadPath>& Out);
	void Finish(bool bSuccess);
	void ReleaseStartupGates();

	EPhase Phase = EPhase::WaitingForMode;
	TWeakObjectPtr<ACodexTacticsGameMode> ModeOwner;
	TArray<TSharedPtr<FStreamableHandle>> Handles;
	TArray<FGameplayPreloadPath> Batch;
	UPROPERTY(Transient)
	TArray<TObjectPtr<USkinnedAsset>> Meshes;
	double StartSeconds = 0.0;
	double PhaseSeconds = 0.0;
	uint64 LastAutomaticSpawnFrame = MAX_uint64;
	TSet<FSoftObjectPath> OptionalMisses;
	bool bInitialBatchAwaitingBeginPlay = false;
	bool bFailed = false;
	bool bOwnsAIBlocker = false;
};
