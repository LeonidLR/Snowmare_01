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

/**
 * Mission-level state: the start mode, the objective banner text, mission failure and restart.
 * A mission level starts at once (the in-level start menu is retired, 2026-10-08: the frontend map L_MainMenu's NEW GAME
 * opens the level): "Game" mode, or the last mode after Ctrl + X. The intro briefing plays unless a save is about to be
 * loaded (UMissionSessionSubsystem::HasPendingLoad, applied by USaveGameSubsystem) or a headless check started the level
 * (-ExecCmds / -NoMainMenu / -CodexBot without a frontend start: the radio line instead).
 * Worlds without ACodexTacticsGameMode (the frontend map) have no mission.
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

	/** Starts the mission in a mode (level start; Combat = dev / bot shortcut straight to the fight). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Mission")
	void StartMission(EMissionStartMode Mode);

	/** False on worlds without ACodexTacticsGameMode (the frontend map): nothing to start or save there. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Mission")
	bool IsMissionWorld() const { return bMissionWorld; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Mission")
	EMissionStartMode GetStartMode() const { return StartMode; }

	/** Tag of the actor marking where "Start Battle" puts the squad (behind the gate; Godot hard-coded (0, -18)). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Mission")
	FName CombatStartTag = TEXT("CombatStart");

	/** Objective banner text without the "OBJECTIVE: " prefix. */
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
	 * Reloads the current level (Godot reload_current_scene). bQuick (Ctrl + X) starts the same mode again; "Restart"
	 * starts the level fresh in "Game" mode.
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Mission")
	void RestartMission(bool bQuick = false);

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

	/** Story dialogue of the game mode (loads the soft reference), or null. */
	const class UDialogueSequenceAsset* LoadDialogue(TSoftObjectPtr<UDialogueSequenceAsset> ACodexTacticsGameMode::* Member) const;
	void PostRadio(const FText& Speaker, const FText& Text) const;
	void HandleVictoryDialogueFinished();
	/** "Start Battle": quest chain done, gate open, squad healed / warmed behind the gate, pre-combat cutscene. */
	void StartCombatMode();

	FText Objective;
	/** Godot _apply_stage_exploration_resources at the end of the cutscene (LoadoutRules). */
	void ApplyStageLoadout();

	EMissionStartMode StartMode = EMissionStartMode::None;
	bool bMissionWorld = false;
	/** Headless checks (menu skipped by the command line) get the radio line instead of the blocking intro dialogue. */
	bool bHeadlessStart = false;
	FText FailureReason;
	ECodexGamePhase LastPhase = ECodexGamePhase::Exploration;
	bool bCombatFinished = false;
	bool bMissionFailed = false;
};
