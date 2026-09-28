#pragma once

#include "CoreMinimal.h"
#include "GameFlow/GameFlowTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "MissionSubsystem.generated.h"

class AOperativeCharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMissionObjectiveChanged, const FText&, Objective);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMissionFailed, const FText&, Reason);

/**
 * Mission-level state: the objective banner text, mission failure and restart.
 * The objective follows the quest chain in exploration and the game flow in combat (preparation, wave, victory).
 * Godot reference: Scenes/movements/main.gd update_objective, _update_objective_by_state, _check_squad_vital_signs,
 * _trigger_game_over, _on_restart_pressed, _restart_current_test_mode (Ctrl + X).
 */
UCLASS()
class CODEXTACTICS_API UMissionSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/** Objective banner text without the «ЦЕЛЬ: » prefix. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Mission")
	FText GetObjective() const { return Objective; }

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Mission")
	void SetObjective(const FText& NewObjective);

	/** An operative died: time stops, HQ reports the loss and the mission-failed screen opens (once). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Mission")
	void TriggerMissionFailed(AOperativeCharacter* FallenOperative);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Mission")
	bool IsMissionFailed() const { return bMissionFailed; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Mission")
	FText GetFailureReason() const { return FailureReason; }

	/** Reloads the current level (Godot reload_current_scene; «Начать заново» and Ctrl + X). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Mission")
	void RestartMission();

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Mission")
	FOnMissionObjectiveChanged OnObjectiveChanged;

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Mission")
	FOnMissionFailed OnMissionFailed;

private:
	UFUNCTION()
	void HandleQuestObjectiveChanged(const FText& QuestObjective);

	UFUNCTION()
	void HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode);

	UFUNCTION()
	void HandleWaveStarted(int32 WaveIndex, int32 TotalEnemies);

	FText Objective;
	FText FailureReason;
	ECodexGamePhase LastPhase = ECodexGamePhase::Exploration;
	bool bCombatFinished = false;
	bool bMissionFailed = false;
};
