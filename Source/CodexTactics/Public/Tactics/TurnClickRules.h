#pragma once

#include "CoreMinimal.h"
#include "Tactics/Gorky17Types.h"

/** What a world click does in the turn-based fight (UTurnBasedCombatSubsystem::HandleWorldClick). */
enum class ETurnClickAction : uint8
{
	/** Nothing (e.g. Ctrl + click on a squad mate: no friendly fire, no selection). */
	None,
	/** Make the clicked operative the active unit. */
	SelectUnit,
	/** Attack the cell (enemy / barrel / barricade) with the active unit (AttackCellCinematic). */
	Attack,
	/** Pick the adjacent object up for relocation (barrel push, turret / barricade move). */
	Relocate,
	/** The object is too far to be relocated: the «подойти вплотную» hint. */
	NeedApproach,
	/** Walk the active unit to the cell. */
	Walk,
	/** Attack mode is on and the cell holds no target (Godot: no walk order). */
	NoTargetInAttackMode,
	/** Ctrl + click on a cell without a target: the hint, never a walk. */
	NoTargetForAttackOrder
};

/**
 * Pure rules of the turn-based clicks and of the held-enemy guard (tests CodexTactics.Tactics.TurnBased.CtrlClick* /
 * HeldEnemyGuard). Godot reference: main.gd turn-based click branch (Shift + barrel / barricade = shot).
 */
namespace TurnClickRules
{
	/**
	 * The click on a cell holding Occupant. User request 2026-10-06: the attack order is Ctrl + click in every mode (real
	 * time, tactical pause, turn-based) — it replaces the Godot Shift modifier of the grid. Plain click on an enemy still
	 * attacks (Godot); plain click on a barrel / barricade / turret relocates it when adjacent (bAdjacent; barrels need an
	 * orthogonal neighbour, the caller decides), else the approach hint; with Ctrl a barrel / barricade is shot, a turret
	 * (own) and a squad mate are ignored, an empty / mine / obstacle cell gets the «no target» hint.
	 */
	CODEXTACTICS_API ETurnClickAction ResolveClick(EGorkyOccupantType Occupant, bool bAttackOrder, bool bAdjacent, bool bAttackMode);

	/**
	 * Held-enemy guard (bug fix 2026-10-06, MarksmanCloseShotSmoke): a frozen enemy (on the grid outside a scripted move,
	 * or in stasis off it) must stay where the fight put it. True when its 2D offset from Anchor exceeds ToleranceCm.
	 */
	CODEXTACTICS_API bool IsHeldUnitDisplaced(const FVector& Location, const FVector& Anchor, float ToleranceCm);

	/** The 2D point lies on the Cells x Cells grid of CellSize cm whose corner is Origin. */
	CODEXTACTICS_API bool IsInsideGrid(const FVector& Point, const FVector& Origin, int32 Cells, float CellSize);

	/** How far a held grid enemy may drift from its cell centre before it is put back: half a cell. */
	CODEXTACTICS_API float GetHeldUnitTolerance(float CellSize);
}
