#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameFlow/GameFlowStateMachine.h"
#include "GameFlowSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGameFlowChangedDynamic, ECodexGamePhase, Phase, ECodexCombatMode, CombatMode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTacticalPauseReleasedDynamic);

/**
 * Owns the mission game flow (FGameFlowStateMachine) for a game world and applies its side effects:
 * global time dilation per state and Blueprint/UI events.
 * The state machine ticks with real (undilated) time, so tactical pause timers are unaffected by dilation.
 * Godot reference: Scenes/movements/main.gd (mode flags, Engine.time_scale).
 */
UCLASS()
class CODEXTACTICS_API UGameFlowSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// UWorldSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableWhenPaused() const override { return false; }

	/** Replaces tuning values and restarts the mission flow from Exploration. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|GameFlow")
	void ResetFlow(const FGameFlowConfig& Config);

	/** Replaces tuning values without touching the current phase (the game mode applies the level config at start). */
	void SetConfig(const FGameFlowConfig& Config) { Machine.SetConfig(Config); }

	/** Save-game load: see FGameFlowStateMachine::RestoreForLoad. */
	void RestoreForLoad(bool bCombatUnlocked, bool bCombatPhase, int32 WaveIndex);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|GameFlow")
	ECodexGamePhase GetPhase() const { return Machine.GetPhase(); }

	/** Tuning values of the flow (pause, turn-based, preparation). */
	const FGameFlowConfig& GetConfig() const { return Machine.GetConfig(); }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|GameFlow")
	ECodexCombatMode GetCombatMode() const { return Machine.GetCombatMode(); }

	/** Commander Mode is on (USquadSubsystem's flag, Sprint 07-A). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|GameFlow")
	bool IsAutonomousSquadCombat() const;

	/** Commander Mode acts now: on, in a wave, real-time (a tactical pause or turn-based fight freezes it). */
	bool IsSquadAutonomyActive() const;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|GameFlow")
	int32 GetWaveIndex() const { return Machine.GetWaveIndex(); }

	/** The combat zone was triggered this mission (Godot is_combat_phase_unlocked). */
	bool IsCombatUnlocked() const { return Machine.IsCombatUnlocked(); }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|GameFlow")
	bool IsWaveActive() const { return Machine.IsWaveActive(); }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|GameFlow")
	int32 GetPauseCharges() const { return Machine.GetPauseCharges(); }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|GameFlow")
	float GetPauseCooldownRemaining() const { return Machine.GetPauseCooldownRemaining(); }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|GameFlow")
	float GetPauseTimeRemaining() const { return Machine.GetPauseTimeRemaining(); }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|GameFlow")
	float GetPreparationTimeRemaining() const { return Machine.GetPreparationTimeRemaining(); }

	/** Real seconds left in the pre-combat cutscene. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|GameFlow")
	float GetCutsceneTimeRemaining() const { return Machine.GetCutsceneTimeRemaining(); }

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|GameFlow")
	EGameFlowResult TriggerCombatZone() { return Machine.TriggerCombatZone(); }

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|GameFlow")
	EGameFlowResult FinishCutscene() { return Machine.FinishCutscene(); }

	/** Ambush level: exploration straight into the real-time fight (see FGameFlowStateMachine::StartAmbushCombat). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|GameFlow")
	EGameFlowResult StartAmbushCombat() { return Machine.StartAmbushCombat(); }

	/** The current fight was started by an ambush (the placed enemies are the wave). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|GameFlow")
	bool IsAmbushFight() const { return Machine.IsAmbushFight(); }

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|GameFlow")
	EGameFlowResult FinishPreparation() { return Machine.FinishPreparation(); }

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|GameFlow")
	EGameFlowResult ToggleTacticalPause() { return Machine.ToggleTacticalPause(); }

	/** Sets the world time dilation of the current state (1, the tactical pause's 0.02, 0 after a wave / game over). */
	void ApplyTimeDilation() const;

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|GameFlow")
	EGameFlowResult RequestEnterTurnBased(bool bEnemiesInRange) { return Machine.RequestEnterTurnBased(bEnemiesInRange); }

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|GameFlow")
	EGameFlowResult ExitTurnBased() { return Machine.ExitTurnBased(); }

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|GameFlow")
	EGameFlowResult NotifyWaveCleared() { return Machine.NotifyWaveCleared(); }

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|GameFlow")
	EGameFlowResult AdvanceAfterWave() { return Machine.AdvanceAfterWave(); }

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|GameFlow")
	EGameFlowResult FinishPostCombat() { return Machine.FinishPostCombat(); }

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|GameFlow")
	void TriggerGameOver() { Machine.TriggerGameOver(); }

	/** Fires after every phase or combat mode change. */
	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|GameFlow")
	FOnGameFlowChangedDynamic OnGameFlowChanged;

	/** Fires when a tactical pause ends: planned squad orders must run now. */
	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|GameFlow")
	FOnTacticalPauseReleasedDynamic OnTacticalPauseReleased;

private:
	void HandleStateChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode);
	void HandlePauseReleased();

	FGameFlowStateMachine Machine;
	FDelegateHandle StateChangedHandle;
	FDelegateHandle PauseReleasedHandle;
};
