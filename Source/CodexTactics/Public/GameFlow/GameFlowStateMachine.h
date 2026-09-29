#pragma once

#include "CoreMinimal.h"
#include "GameFlow/GameFlowTypes.h"

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnGameFlowStateChanged, ECodexGamePhase /*Phase*/, ECodexCombatMode /*CombatMode*/);
DECLARE_MULTICAST_DELEGATE(FOnTacticalPauseReleased);

/**
 * Pure game flow rules: mission phases and combat sub-modes, with no world or actor access.
 * Owned by UGameFlowSubsystem, which applies side effects (time dilation, events).
 *
 * Godot reference: Scenes/movements/main.gd — toggle_active_pause(), _enter_turn_based_combat(),
 * _exit_turn_based_combat(), _start_next_wave(), _on_wave_cleared(), _on_next_wave_pressed().
 * Deviation (user decision 2026-09-28): turn-based combat is entered only from WaveCombat/RealTime,
 * never from exploration, preparation or a tactical pause.
 *
 * All timers use real (undilated) seconds.
 */
class CODEXTACTICS_API FGameFlowStateMachine
{
public:
	explicit FGameFlowStateMachine(const FGameFlowConfig& InConfig = FGameFlowConfig());

	/** Replaces the config and resets to a fresh mission in Exploration. */
	void Reset(const FGameFlowConfig& InConfig);

	/** Replaces the config and keeps the current state (level data applied at mission start). */
	void SetConfig(const FGameFlowConfig& InConfig) { Config = InConfig; }

	// --- Queries ---
	ECodexGamePhase GetPhase() const { return Phase; }
	ECodexCombatMode GetCombatMode() const { return CombatMode; }
	const FGameFlowConfig& GetConfig() const { return Config; }
	/** 1-based index of the current or upcoming wave; 0 before the first preparation. */
	int32 GetWaveIndex() const { return WaveIndex; }
	int32 GetPauseCharges() const { return PauseCharges; }
	float GetPauseCooldownRemaining() const { return PauseCooldownRemaining; }
	float GetPauseTimeRemaining() const { return PauseTimeRemaining; }
	float GetPreparationTimeRemaining() const { return PreparationTimeRemaining; }
	float GetCutsceneTimeRemaining() const { return CutsceneTimeRemaining; }
	int32 GetTurnBasedUsesThisWave() const { return TurnBasedUsesThisWave; }
	bool IsCombatUnlocked() const { return bCombatUnlocked; }
	/**
	 * Godot `is_wave_active`: true during a wave and during the rest before every following wave
	 * (not during the first preparation, and not after a wave is cleared).
	 */
	bool IsWaveActive() const
	{
		return Phase == ECodexGamePhase::WaveCombat || (Phase == ECodexGamePhase::Preparation && WaveIndex > 1);
	}
	/** World time dilation the current state requires (0 = stopped). */
	float GetTimeDilation() const;

	// --- Requests ---
	/** Exploration -> Cutscene when the squad enters the combat zone (once per mission). */
	EGameFlowResult TriggerCombatZone();
	/** Cutscene -> Preparation for wave 1 (skip, or automatically after CutsceneDuration). */
	EGameFlowResult FinishCutscene();
	/** Preparation -> WaveCombat/RealTime (timer expired or player pressed "ready"). */
	EGameFlowResult FinishPreparation();
	/** RealTime -> TacticalPause (spends a charge), or TacticalPause -> RealTime (runs planned orders). */
	EGameFlowResult ToggleTacticalPause();
	/** RealTime -> TurnBased. bEnemiesInRange comes from the encounter selector. */
	EGameFlowResult RequestEnterTurnBased(bool bEnemiesInRange);
	/** TurnBased -> TacticalPause for PostTurnBasedPauseDuration, without spending a charge. */
	EGameFlowResult ExitTurnBased();
	/** WaveCombat (any sub-mode) -> WaveCleared. */
	EGameFlowResult NotifyWaveCleared();
	/** WaveCleared -> Preparation for the next wave, or PostCombat after the last wave. */
	EGameFlowResult AdvanceAfterWave();
	/** PostCombat -> Exploration; the combat zone can be triggered again. */
	EGameFlowResult FinishPostCombat();
	/** Any phase -> GameOver. */
	void TriggerGameOver();

	/**
	 * Save-game load (Godot SaveManager _deserialize_game_state sets is_combat_phase_unlocked / is_preparation_active /
	 * is_wave_active / current_wave_index): Exploration before the combat, otherwise the preparation of the saved wave
	 * (an active wave is resumed from its preparation — enemies are not saved, like in Godot).
	 */
	void RestoreForLoad(bool bInCombatUnlocked, bool bInCombatPhase, int32 InWaveIndex);

	/** Advances real-time timers: pause cooldown, tactical pause, preparation. */
	void Tick(float RealDeltaSeconds);

	/** Fires after every phase or combat mode change. */
	FOnGameFlowStateChanged OnStateChanged;
	/** Fires when a tactical pause ends (by toggle or timeout): planned orders must run now. */
	FOnTacticalPauseReleased OnTacticalPauseReleased;

private:
	void SetState(ECodexGamePhase NewPhase, ECodexCombatMode NewMode);
	void StartWave();
	void ReleaseTacticalPause();

	FGameFlowConfig Config;
	ECodexGamePhase Phase = ECodexGamePhase::Exploration;
	ECodexCombatMode CombatMode = ECodexCombatMode::None;
	int32 WaveIndex = 0;
	int32 PauseCharges = 0;
	float PauseCooldownRemaining = 0.f;
	float PauseTimeRemaining = 0.f;
	float PreparationTimeRemaining = 0.f;
	float CutsceneTimeRemaining = 0.f;
	int32 TurnBasedUsesThisWave = 0;
	bool bCombatUnlocked = false;
};
