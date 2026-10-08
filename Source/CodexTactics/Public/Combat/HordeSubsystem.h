#pragma once

#include "CoreMinimal.h"
#include "Combat/HordeRules.h"
#include "GameFlow/GameFlowTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "HordeSubsystem.generated.h"

class AEnemyCharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnHordeReleased, int32, HordeIndex, int32, EnemyCount, FVector, Location);

/**
 * Horde after a long real-time fight (user request 2026-10-06, UE-only; no Godot counterpart). Every fight (a wave, an
 * ambush fight) has a clock that runs only while the fight runs in full real time — not in the tactical pause, not in
 * turn-based combat, not while a dialogue / cutscene holds the world AI (FHordeTimer, HordeRules::IsCountingTime). At
 * TriggerSeconds (240 s) a group of enemies (8: frost hounds + frostbitten) appears at a random reachable navmesh point
 * 25-45 m from the squad's centre, out of the squad's sight when possible, clustered within 4 m; they know where the
 * squad is (AEnemyCharacter::bKnowsSquadPosition: no patrol, no perception, no fall-back) and rush it. They join the
 * wave (UWaveSubsystem::SpawnEnemy), so the fight ends only when they are dead. Once per fight unless "repeats".
 * HUD: "HORDE!" banner + an arrow towards it (ACodexTacticsHUD::DrawHordeWarning), the radio line, OnHordeReleased (hook
 * for audio / VFX) and the optional "warning_sound". Data: Content/Data/AI/horde.json + the level JSON "horde_enabled" /
 * "horde" (HordeRules::ResolveForLevel); console Codex.Horde.TriggerSeconds / Codex.Horde.Enabled, command
 * CodexTactics.Horde.Release.
 */
UCLASS()
class CODEXTACTICS_API UHordeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** The config of the current fight (defaults + level + console overrides). */
	const FHordeConfig& GetConfig() const { return Config; }

	/** Running real-time seconds of the current fight (the horde clock). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Horde")
	float GetCombatSeconds() const { return Timer.CombatSeconds; }

	/** Hordes released in the current fight. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Horde")
	int32 GetHordesReleased() const { return Timer.HordesReleased; }

	/** A fight is on and its horde clock exists (it may be stopped by the pause / turn-based combat). */
	bool IsFightTracked() const { return bFightTracked; }

	/** The clock ran on the last tick. */
	bool IsCounting() const { return bCountingNow; }

	/**
	 * Releases a horde now (the clock's trigger, the dev command, smokes): finds the spawn point and spawns the group.
	 * Returns how many enemies appeared (0: no spawn point / no squad).
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Horde")
	int32 ReleaseHorde();

	/** The HUD warning while it lasts: where the horde appeared, how many, seconds left. */
	bool GetActiveWarning(FVector& OutLocation, int32& OutCount, float& OutSecondsLeft) const;

	/** Members of the last horde (smokes). */
	const TArray<TWeakObjectPtr<AEnemyCharacter>>& GetLastHorde() const { return LastHorde; }

	/** Where the last horde appeared and the squad's centre then (smokes / logs). */
	FVector GetLastSpawnPoint() const { return LastSpawnPoint; }
	FVector GetLastSquadCentre() const { return LastSquadCentre; }
	bool WasLastSpawnHidden() const { return bLastSpawnHidden; }

	/** Re-reads the config for the current level (start of every fight; smokes after changing the console values). */
	void RefreshConfig();

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Horde")
	FOnHordeReleased OnHordeReleased;

private:
	UFUNCTION()
	void HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode);

	/** The living operatives' mean position (false without one). */
	bool GetSquadCentre(FVector& OutCentre) const;
	/** Samples the spawn candidates around Centre (navmesh, path, squad sight) and picks one. */
	bool FindSpawnPoint(const FVector& Centre, FVector& OutPoint, bool& bOutHidden) const;
	/** Some living operative sees Point (Visibility channel, pawns ignored). */
	bool IsVisibleToSquad(const FVector& Point) const;
	/** Spawns horde number HordeIndex (0 = first of the fight); returns how many enemies appeared. */
	int32 SpawnHorde(int32 HordeIndex);
	/** Seconds until the next attempt after a failed one (no spawn point). */
	float RetryCooldown = 0.f;

	FHordeConfig Config;
	FHordeTimer Timer;
	ECodexGamePhase LastPhase = ECodexGamePhase::Exploration;
	bool bFightTracked = false;
	bool bCountingNow = false;
	double WarningUntil = -1.0;
	int32 WarningCount = 0;
	FVector LastSpawnPoint = FVector::ZeroVector;
	FVector LastSquadCentre = FVector::ZeroVector;
	bool bLastSpawnHidden = false;
	TArray<TWeakObjectPtr<AEnemyCharacter>> LastHorde;
};
