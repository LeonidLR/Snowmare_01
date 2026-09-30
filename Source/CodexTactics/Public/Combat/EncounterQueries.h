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

	/**
	 * Turn-based fights start on flat ground only (user decision 2026-09-30): the ground level is the median of
	 * GroundSamples (the floor under the future grid); every squad member's feet must be within Tolerance of it —
	 * not up on a platform, not down in a pit. OutGround gets the median.
	 */
	CODEXTACTICS_API bool IsSquadOnFlatGround(TArray<float> GroundSamples, const TArray<float>& SquadFeet, float Tolerance, float& OutGround);

	/** Floor heights on a Count x Count lattice over the square of HalfExtent around Center (characters ignored). */
	CODEXTACTICS_API TArray<float> SampleGroundHeights(const UWorld* World, const FVector& Center, float HalfExtent, int32 Count = 7);
}
