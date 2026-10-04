#pragma once

#include "CoreMinimal.h"
#include "Characters/OperativeMovementRules.h"
#include "Data/CombatTypes.h"

/**
 * Turn-based enemy tactics (UE-only; Godot turn_based_combat_manager.gd gives every enemy the same orthogonal bite
 * for the nearest operative). User decisions 2026-10-04: per-archetype turns, ranged enemies (spitter, cryo drone,
 * marksman) shoot at range with a line of fire and a hit roll, melee enemies seek the back / flank arc; targets are
 * chosen by the same EnemyTacticsRules as in real time. Pure rules, tested in CodexTactics.Tactics.EnemyTurn.*.
 */
struct CODEXTACTICS_API FEnemyTurnProfile
{
	/** AP and damage as multiples of the turn-based balance (tactical_enemy_max_ap 6 / base damage 18). */
	float APScale = 1.f;
	float DamageScale = 1.f;
	/** AP one attack costs (Godot bite: 2). */
	int32 AttackAPCost = 2;
	/** After a melee bite / a shot from an adjacent cell: one step back (Godot _enemy_perform_retreat). */
	bool bHitAndRun = true;
	/** How much more a back (1) / flank (0.5) arc is worth than the front when picking the cell to bite from. */
	float BackArcWeight = 0.f;
	bool bRanged = false;
	/** Shooting range in cells (Chebyshev) and the band it prefers to shoot from. */
	int32 MinRange = 1;
	int32 MaxRange = 1;
	int32 PreferredMin = 1;
	int32 PreferredMax = 1;
	/** Hit chance at MinRange against a standing target in the open, and the loss per cell beyond. */
	float BaseHitChance = 0.75f;
	float HitFalloffPerCell = 0.05f;
};

/** A cell a melee enemy could bite Target from. */
struct CODEXTACTICS_API FEnemyMeleeCell
{
	FIntPoint Cell = FIntPoint::ZeroValue;
	/** AP to walk there (0: it stands there); < 0: unreachable. */
	int32 PathCost = 0;
	/** Godot attack-arc damage multiplier from that cell (front 1, flank / back more). */
	float ArcMultiplier = 1.f;
};

/** A cell a ranged enemy could shoot Target from. */
struct CODEXTACTICS_API FEnemyFiringCell
{
	FIntPoint Cell = FIntPoint::ZeroValue;
	int32 PathCost = 0;
	/** Chebyshev cells to the target. */
	int32 Distance = 0;
	bool bLineOfFire = false;
	/** An operative stands next to that cell (it would be bitten back / shot point-blank). */
	bool bNextToOperative = false;
	float HitChance = 0.f;
};

namespace EnemyTurnRules
{
	CODEXTACTICS_API FEnemyTurnProfile ProfileFor(EEnemyArchetype Archetype);

	/** Max AP / base damage of the archetype from the balance values. */
	CODEXTACTICS_API int32 MaxAP(const FEnemyTurnProfile& Profile, int32 BalanceMaxAP);
	CODEXTACTICS_API float BaseDamage(const FEnemyTurnProfile& Profile, float BalanceBaseDamage);

	/** Ranged hit chance: falls off beyond MinRange, x0.8 crouched / x0.6 prone, x CoverMultiplier behind a barricade; 0.1-0.9. */
	CODEXTACTICS_API float RangedHitChance(const FEnemyTurnProfile& Profile, int32 DistanceCells, EOperativeStance TargetStance, bool bTargetInCover,
		float CoverMultiplier = 0.6f);

	/**
	 * The cell to bite from: among those it reaches with AP left for the bite, the best arc (weighted by the profile)
	 * for the least walking; if none leaves AP for the bite, the nearest (it closes in). INDEX_NONE: none reachable.
	 */
	CODEXTACTICS_API int32 ChooseMeleeCell(const FEnemyTurnProfile& Profile, const TArray<FEnemyMeleeCell>& Cells, int32 AP);

	/**
	 * The cell to shoot from: reachable with AP left for the shot, a line of fire, within range; the best hit chance,
	 * not next to an operative, inside the preferred band, least walking. INDEX_NONE: no shot this turn.
	 */
	CODEXTACTICS_API int32 ChooseFiringCell(const FEnemyTurnProfile& Profile, const TArray<FEnemyFiringCell>& Cells, int32 AP);
}
