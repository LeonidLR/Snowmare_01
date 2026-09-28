#pragma once

#include "CoreMinimal.h"

class UWorld;

/** World queries used by combat mode switches. */
namespace CombatQueries
{
	/** Actor tag every enemy carries. */
	CODEXTACTICS_API extern const FName EnemyTag;

	/**
	 * True if any enemy is within Radius (ground plane) of Center.
	 * Godot reference: TacticalEncounterSelector.select_participants (15 m around the combat centre).
	 */
	CODEXTACTICS_API bool HasEnemiesWithin(const UWorld* World, const FVector& Center, float Radius);
}
