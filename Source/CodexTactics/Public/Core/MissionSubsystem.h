#pragma once

#include "CoreMinimal.h"
#include "Core/MissionSessionSubsystem.h"
#include "GameFlow/GameFlowTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "MissionSubsystem.generated.h"

class AOperativeCharacter;
class ACodexTacticsGameMode;
class UDialogueSequenceAsset;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMissionObjectiveChanged, const FText&, Objective);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMissionFailed, const FText&, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMainMenuChanged, bool, bOpen);

/**
 * Mission-level state: the main menu (start mode), the objective banner text, mission failure and restart.
 * The menu opens when a level starts unless Ctrl + X restarted it (same mode again) or the command line skips it
 * (-ExecCmds / -NoMainMenu: headless checks start «Начать игру»; -ForceMainMenu keeps it). The world is paused while
 * the menu is open.
 * The objective follows the quest chain in exploration and the game flow in combat (preparation, wave, victory).
 * Godot reference: Scenes/movements/main.gd update_objective, _update_objective_by_state, _check_squad_vital_signs,
 * _trigger_game_over, _on_restart_pressed, _restart_current_test_mode (Ctrl + X), _ready (quick restart),
 * _on_start_game_pressed / _on_start_combat_pressed / _on_start_exploration_pressed.
 */
UCLASS()
class CODEXTACTICS_API UMissionSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/** Starts the mission in a mode (main menu buttons): closes the menu, unpauses, applies the mode. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Mission")
	void StartMission(EMissionStartMode Mode);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Mission")
	bool IsMainMenuOpen() const { return bMainMenuOpen; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Mission")
	EMissionStartMode GetStartMode() const { return StartMode; }

	/** Tag of the actor marking where «Начать бой» puts the squad (behind the gate; Godot hard-coded (0, -18)). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Mission")
	FName CombatStartTag = TEXT("CombatStart");

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

	/**
	 * Reloads the current level (Godot reload_current_scene). bQuick (Ctrl + X) starts the same mode again without
	 * the menu; «Начать заново» shows the menu.
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Mission")
	void RestartMission(bool bQuick = false);

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Mission")
	FOnMissionObjectiveChanged OnObjectiveChanged;

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Mission")
	FOnMissionFailed OnMissionFailed;

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Mission")
	FOnMainMenuChanged OnMainMenuChanged;

private:
	UFUNCTION()
	void HandleQuestObjectiveChanged(const FText& QuestObjective);

	UFUNCTION()
	void HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode);

	UFUNCTION()
	void HandleWaveStarted(int32 WaveIndex, int32 TotalEnemies);

	void OpenMainMenu();
	/** Story dialogue of the game mode (loads the soft reference), or null. */
	const class UDialogueSequenceAsset* LoadDialogue(TSoftObjectPtr<UDialogueSequenceAsset> ACodexTacticsGameMode::* Member) const;
	void PostRadio(const FText& Speaker, const FText& Text) const;
	void HandleVictoryDialogueFinished();
	/** «Начать бой»: quest chain done, gate open, squad healed / warmed behind the gate, pre-combat cutscene. */
	void StartCombatMode();

	FText Objective;
	EMissionStartMode StartMode = EMissionStartMode::None;
	bool bMainMenuOpen = false;
	/** Headless checks (menu skipped by the command line) get the radio line instead of the blocking intro dialogue. */
	bool bHeadlessStart = false;
	FText FailureReason;
	ECodexGamePhase LastPhase = ECodexGamePhase::Exploration;
	bool bCombatFinished = false;
	bool bMissionFailed = false;
};
