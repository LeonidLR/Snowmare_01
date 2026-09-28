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

	UFUNCTION(BlueprintPure, Category = "CodexTactics|GameFlow")
	ECodexGamePhase GetPhase() const { return Machine.GetPhase(); }

	/** Tuning values of the flow (pause, turn-based, preparation). */
	const FGameFlowConfig& GetConfig() const { return Machine.GetConfig(); }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|GameFlow")
	ECodexCombatMode GetCombatMode() const { return Machine.GetCombatMode(); }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|GameFlow")
	int32 GetWaveIndex() const { return Machine.GetWaveIndex(); }

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

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|GameFlow")
	EGameFlowResult TriggerCombatZone() { return Machine.TriggerCombatZone(); }

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|GameFlow")
	EGameFlowResult FinishCutscene() { return Machine.FinishCutscene(); }

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|GameFlow")
	EGameFlowResult FinishPreparation() { return Machine.FinishPreparation(); }

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|GameFlow")
	EGameFlowResult ToggleTacticalPause() { return Machine.ToggleTacticalPause(); }

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
	void ApplyTimeDilation() const;

	FGameFlowStateMachine Machine;
	FDelegateHandle StateChangedHandle;
	FDelegateHandle PauseReleasedHandle;
};
