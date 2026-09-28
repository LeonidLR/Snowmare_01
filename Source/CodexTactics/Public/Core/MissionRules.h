#pragma once

#include "CoreMinimal.h"
#include "GameFlow/GameFlowTypes.h"

/**
 * Pure mission texts: objective banner per game phase and mission-failed reasons.
 * Godot reference: Scenes/movements/main.gd update_objective calls (_on_start_game_pressed, preparation, wave start,
 * wave rest, victory) and _trigger_game_over; movements_demo.tscn GameOverPanel texts.
 */
namespace MissionRules
{
	/** A fallen operative at or above this cold died of hypothermia (Godot cold_level >= 99). */
	constexpr float FrozenDeathColdLevel = 99.f;

	/** Objective when the mission starts (Godot _on_start_game_pressed). */
	CODEXTACTICS_API FText GetStartObjective();

	/**
	 * Objective for a game phase change, false when the phase keeps the current objective.
	 * WaveIndex = the flow's wave number (1 = first wave); PreparationSeconds = time left in the preparation.
	 * bAfterCombat: exploration entered after the defence (victory).
	 */
	CODEXTACTICS_API bool GetPhaseObjective(ECodexGamePhase Phase, int32 WaveIndex, float PreparationSeconds, bool bAfterCombat, FText& OutObjective);

	/** «ОБОРОНА: Отразить волну N! Врагов: M». */
	CODEXTACTICS_API FText GetWaveObjective(int32 WaveIndex, int32 EnemyCount);

	/** Mission failed reason for the fallen operative (hypothermia or wounds). */
	CODEXTACTICS_API FText GetFailureReason(const FText& OperativeName, float ColdLevel);

	/** HQ radio line when an operative is lost. */
	CODEXTACTICS_API FText GetFailureRadio(const FText& OperativeName);
}
