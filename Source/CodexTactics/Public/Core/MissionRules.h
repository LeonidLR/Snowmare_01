#pragma once

#include "CoreMinimal.h"
#include "Core/MissionSessionSubsystem.h"
#include "GameFlow/GameFlowTypes.h"

/**
 * Pure mission texts and start rules: objective banner per game phase / start mode, mission-failed reasons,
 * whether the main menu opens.
 * Godot reference: Scenes/movements/main.gd update_objective calls (_on_start_game_pressed, _on_start_exploration_pressed,
 * preparation, wave start, wave rest, victory), _trigger_game_over, _ready quick restart; movements_demo.tscn
 * GameOverPanel / StartMenu texts.
 */
namespace MissionRules
{
	/** A fallen operative at or above this cold died of hypothermia (Godot cold_level >= 99). */
	constexpr float FrozenDeathColdLevel = 99.f;

	/** Objective when the mission starts in "Start Game" (Godot _on_start_game_pressed). */
	CODEXTACTICS_API FText GetStartObjective();

	/** Objective and commander radio line for a start mode (Game; Combat: empty, the flow sets the objective). */
	CODEXTACTICS_API FText GetModeObjective(EMissionStartMode Mode);
	CODEXTACTICS_API FText GetModeRadio(EMissionStartMode Mode);

	/**
	 * Mode a mission level starts in. The in-level start menu is retired (2026-10-08: the frontend map's NEW GAME opens
	 * the level, which starts at once): quick restart (Ctrl + X) repeats the last mode, everything else starts "Game".
	 */
	CODEXTACTICS_API EMissionStartMode GetAutoStartMode(bool bQuickRestart, EMissionStartMode LastMode);

	/**
	 * Skip the blocking intro briefing (the radio line is posted instead)? Always when a save is about to be loaded;
	 * otherwise for headless runs (-ExecCmds / -NoMainMenu / -CodexBot) unless the frontend started the game or Ctrl + X
	 * restarted it.
	 */
	CODEXTACTICS_API bool ShouldSkipIntro(bool bHeadlessCommandLine, bool bFrontendStart, bool bQuickRestart, bool bPendingLoad);

	/**
	 * Objective for a game phase change, false when the phase keeps the current objective.
	 * WaveIndex = the flow's wave number (1 = first wave); PreparationSeconds = time left in the preparation.
	 * bAfterCombat: exploration entered after the defence (victory).
	 */
	CODEXTACTICS_API bool GetPhaseObjective(ECodexGamePhase Phase, int32 WaveIndex, float PreparationSeconds, bool bAfterCombat, FText& OutObjective);

	/** Radio lines used when a story dialogue asset is missing (Godot else branches) and after the victory dialogue. */
	CODEXTACTICS_API FText GetPreparationRadio();
	CODEXTACTICS_API FText GetWaveRestRadio(float PreparationSeconds);
	CODEXTACTICS_API FText GetVictoryRadio();
	/** Objective after the victory dialogue. */
	CODEXTACTICS_API FText GetAfterVictoryObjective();

	/** "DEFENSE: Repel wave N! Enemies: M". */
	CODEXTACTICS_API FText GetWaveObjective(int32 WaveIndex, int32 EnemyCount);

	/** Mission failed reason for the fallen operative (hypothermia or wounds). */
	CODEXTACTICS_API FText GetFailureReason(const FText& OperativeName, float ColdLevel);

	/** HQ radio line when an operative is lost. */
	CODEXTACTICS_API FText GetFailureRadio(const FText& OperativeName);

	/**
	 * Defeat rule (user decision 2026-10-08): the mission is lost only when the COMMANDER dies (Engineer, Medic-Sapper and
	 * recruits are permanent losses, the fight goes on) or when no squad member is left alive.
	 * LivingSquadMembers = living members after this death.
	 */
	CODEXTACTICS_API bool ShouldFailMission(bool bFallenIsCommander, int32 LivingSquadMembers);

	/** Full-screen line after the commander's death cinematic, before the mission-failed screen. */
	CODEXTACTICS_API FText GetSquadFallenText();

	/** HQ line when a non-commander member is killed (the fight goes on). */
	CODEXTACTICS_API FText GetMemberLostRadio(const FText& OperativeName);
}
