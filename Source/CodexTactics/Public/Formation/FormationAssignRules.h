#pragma once

#include "CoreMinimal.h"

/**
 * Who takes which place when the formation changes (Godot reference: none (UE-only, coordinator fix 2026-10-25, contract
 * docs/design/FORMATION_CONTRACT.md 10.6; reference implementation Tools/TrainingGround/src/movement/rules.ts FormationAssignRules)).
 * Pure. Tests: CodexTactics.Formation.Assign.*, parity CodexTactics.Formation.Parity.Assign.
 *
 * A slot belongs to a PLACE (its role label: the sectors go with the place), not to a fixed man. Whenever the formation in effect
 * changes (fold, unfold, mirror, menu pick, terrain change, trail column on / off) the followers take the new formation's follower
 * places by minimal total travel from where they stand (the leader keeps K); an optimal assignment never has two straight paths
 * crossing, so nobody walks through anybody. Between changes the places stay.
 */
namespace FormationAssignRules
{
	/**
	 * Minimal total travel (sum of straight distances): Result[i] = the index into Places member i takes (INDEX_NONE for members beyond
	 * the place count). Depth-first, member-major, places in index order; a complete assignment replaces the best only when cheaper by
	 * more than 1e-3 cm (ties keep the earlier one: no movement needed -> the identity). Exhaustive: meant for a squad (<= 4 followers).
	 */
	CODEXTACTICS_API TArray<int32> AssignPlaces(const TArray<FVector2D>& Members, const TArray<FVector2D>& Places);
}
