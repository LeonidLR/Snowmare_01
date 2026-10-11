#pragma once

#include "CoreMinimal.h"
#include "Data/WaveConfigTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "DemoModeSubsystem.generated.h"

/**
 * Demo game-mode flags (Demo 01 producer plan C10): the level JSON "demo" object switches Panic off, forces the vision
 * cones on / off, runs the tutorial and selects the demo HUD preset (debug squad panel and labels hidden) — data of the
 * demo level, never the global BP defaults. Applied once the game mode has read the level (first tick after StartPlay)
 * and to every operative / enemy spawned later. No "demo" object: nothing changes. UE-only.
 */
UCLASS()
class CODEXTACTICS_API UDemoModeSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** The flags in use (the level's, or the override). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Demo")
	const FDemoModeSettings& GetSettings() const { return Settings; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Demo")
	bool IsDemo() const { return Settings.bDemo; }

	/** Replaces the flags and applies them now (smokes, a future demo menu). */
	void ApplySettings(const FDemoModeSettings& InSettings);

	/** Applies the flags to one actor (operative: panic + cone; enemy: cone). */
	void ApplyToActor(AActor* Actor) const;

private:
	void ApplyFromLevel();
	void ApplyToWorld();
	void HandleActorSpawned(AActor* Actor);

	FDemoModeSettings Settings;
	FDelegateHandle SpawnedHandle;
};
