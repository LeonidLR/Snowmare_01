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
	/** The current fight was started by an ambush (StartAmbushCombat): the level's placed enemies are the wave. */
	bool IsAmbushFight() const { return bAmbushFight; }
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
	/**
	 * The current tactical pause is the free one a sudden horde wave opens with (user decision 2026-10-11): it spent no
	 * charge, it has no timer (only the player ends it, like any pause: Space tap / hold).
	 */
	bool IsFreePause() const { return bFreePause && CombatMode == ECodexCombatMode::TacticalPause; }
	/**
	 * Wave InWaveIndex (1-based) starts suddenly: Config.bSuddenWaveStart unless the wave is listed in
	 * Config.PreparedWaveIndices; the console override (SetSuddenStartOverride) wins over both.
	 */
	bool UsesSuddenStart(int32 InWaveIndex) const;
	/** -1: the config decides; 0: every wave has the old prepared start; 1: every wave starts suddenly (Codex.HordeIntro.SuddenStart). */
	void SetSuddenStartOverride(int32 Override) { SuddenStartOverride = Override; }

	// --- Requests ---
	/**
	 * The squad enters the combat zone (once per mission): Exploration -> Cutscene (old prepared start), or with a sudden
	 * wave 1 (UsesSuddenStart) straight to WaveCombat / TacticalPause — the wave spawns now, in the free pause.
	 */
	EGameFlowResult TriggerCombatZone();
	/**
	 * Exploration -> WaveCombat/RealTime of wave 1 at once (no cutscene, no preparation): the squad attacked an enemy or
	 * an enemy detected the squad on an ambush level. Once per mission (CombatAlreadyUnlocked / WrongPhase after).
	 */
	EGameFlowResult StartAmbushCombat();
	/** Cutscene -> Preparation for wave 1 (skip, or automatically after CutsceneDuration). */
	EGameFlowResult FinishCutscene();
	/**
	 * Preparation -> WaveCombat/RealTime (timer expired or player pressed "ready"); a sudden wave (a loaded save resumes
	 * in its preparation) opens in the free tactical pause instead.
	 */
	EGameFlowResult FinishPreparation();
	/**
	 * RealTime -> TacticalPause (spends a charge), or TacticalPause -> RealTime (runs planned orders). Releasing the free
	 * pause of a sudden wave works the same and costs nothing.
	 */
	EGameFlowResult ToggleTacticalPause();
	/**
	 * RealTime -> TurnBased (also TacticalPause -> TurnBased with Config.bAllowTurnBasedFromTacticalPause: the pause ends
	 * without OnTacticalPauseReleased, its plans are dropped). bEnemiesInRange comes from the encounter selector.
	 */
	EGameFlowResult RequestEnterTurnBased(bool bEnemiesInRange);
	/** TurnBased -> TacticalPause for PostTurnBasedPauseDuration, without spending a charge (the grid's own end). */
	EGameFlowResult ExitTurnBased();
	/** TurnBased -> RealTime at once (the Space hold, user request 2026-10-06); no pause, no charge spent. */
	EGameFlowResult ExitTurnBasedToRealTime();
	/** WaveCombat (any sub-mode) -> WaveCleared. */
	EGameFlowResult NotifyWaveCleared();
	/**
	 * WaveCleared -> Preparation for the next wave (a sudden next wave: WaveCombat in the free pause at once), or
	 * PostCombat after the last wave.
	 */
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
	void RestoreForLoad(bool bInCombatUnlocked, bool bInCombatPhase, int32 InWaveIndex, float InPreparationSeconds = -1.f);

	/**
	 * Advances real-time timers: pause cooldown, tactical pause (not the free one), preparation. A preparation of a
	 * sudden wave (restored by a save load) ends on the first tick: the wave starts suddenly.
	 */
	void Tick(float RealDeltaSeconds);

	/** Fires after every phase or combat mode change. */
	FOnGameFlowStateChanged OnStateChanged;
	/** Fires when a tactical pause ends (by toggle or timeout): planned orders must run now. */
	FOnTacticalPauseReleased OnTacticalPauseReleased;

private:
	void SetState(ECodexGamePhase NewPhase, ECodexCombatMode NewMode);
	/** Starts wave WaveIndex: real time, or the free tactical pause when bSudden. */
	void StartWave(bool bSudden = false);
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
	bool bAmbushFight = false;
	/** The running tactical pause is the free, untimed one of a sudden wave start. */
	bool bFreePause = false;
	int32 SuddenStartOverride = -1;
};
