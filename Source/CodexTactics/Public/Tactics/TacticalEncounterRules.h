#pragma once

#include "CoreMinimal.h"

/**
 * Which enemies join a turn-based fight. Godot reference: Scripts/tactics/tactical_encounter_selector.gd
 * select_participants (called by main.gd _enter_turn_based_combat with radius 15 m and a cap of 6).
 */
namespace TacticalEncounterRules
{
	/** An enemy that could join: its species (Godot enemy_type) and distance to the fight centre. */
	struct FCandidate
	{
		FName Species;
		float Distance = 0.f;
	};

	/**
	 * Indices of the enemies that fight on the grid, in selection order. Everyone within Radius when that is at most
	 * MaxEnemies; otherwise diversity first — the nearest one of every species — then the nearest of the rest up to
	 * MaxEnemies. Everybody else is frozen (stasis) for the fight.
	 */
	CODEXTACTICS_API TArray<int32> SelectEnemies(const TArray<FCandidate>& Candidates, float Radius, int32 MaxEnemies = 6);
}
