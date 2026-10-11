#pragma once

#include "CoreMinimal.h"

/**
 * How an incoming blow on an operative is resolved (AOperativeCharacter::TakeHit / TakeGridBlast). Each modifier applies
 * exactly once per kind (audit 2026-10-11 area 2 #2: the turn-based blasts used to get the stance factor twice).
 */
enum class EOperativeHitKind : uint8
{
	/** Shot / bite / melee in real time: dodge, cover, stance defence, fortitude, downed x1.33, heavy-hit knockdown. */
	Attack,
	/**
	 * bBypassAvoidance: real-time blasts / traps (no stance, no fortitude) and the turn-based grid shots / bites (the
	 * grid already applied its stance factor): max(1, Amount x downed), rounded when downed (grid damage is integer).
	 */
	Unavoidable,
	/**
	 * Turn-based barrel / mine blast (Godot _detonate_barrel / _detonate_mine -> player.gd take_damage): the Amount already
	 * carries the grid stance factor (TurnBasedRules::StanceDamageMultiplier), so only the luck dodge (kept from Godot),
	 * the fortitude cut and the downed x1.33 apply; no cover, no heavy-hit knockdown (NotifyExplosion floors him).
	 */
	GridBlast,
};

/**
 * Pure rules of the operative hit formula.
 * Godot reference: Scenes/movements/player.gd take_damage (dodge luck * 0.4 %, fortitude 1.5 % per point up to 50 %).
 */
namespace OperativeHitRules
{
	/** Luck dodge applies (Attack and GridBlast; Godot take_damage without bypass_avoidance). */
	CODEXTACTICS_API bool CanDodge(EOperativeHitKind Kind);

	/** Sprint 12 cover absorb / head-down crit undo apply (Attack only). */
	CODEXTACTICS_API bool UsesCover(EOperativeHitKind Kind);

	/** The stance defence multiplier applies (Attack only: GridBlast is pre-multiplied, Unavoidable ignores it). */
	CODEXTACTICS_API bool UsesStanceDefense(EOperativeHitKind Kind);

	/** The fortitude cut applies (Attack and GridBlast). */
	CODEXTACTICS_API bool UsesFortitude(EOperativeHitKind Kind);

	/** A single heavy hit may knock him down (Attack only; blasts knock down through UKnockdownComponent::NotifyExplosion). */
	CODEXTACTICS_API bool AllowsHeavyHitKnockdown(EOperativeHitKind Kind);

	/** Dodged when the kind can dodge and DodgeRoll (0..100) < Luck * 0.4. */
	CODEXTACTICS_API bool IsDodged(EOperativeHitKind Kind, float DodgeRoll, float Luck);

	/** Fortitude damage cut: clamp(Fortitude * 1.5 %, 0, 50 %). */
	CODEXTACTICS_API float FortitudeCut(float Fortitude);

	/**
	 * Health lost by a hit that was not dodged. AmountAfterCover = the amount after the cover absorb (Attack; the others
	 * pass the raw amount). StanceDefense = UHealthComponent::GetDefenseMultiplier (crouch 0.75 / prone 0.5 in real time),
	 * KnockdownScale = UKnockdownComponent::GetDamageMultiplier (1.33 while down).
	 */
	CODEXTACTICS_API float FinalDamage(EOperativeHitKind Kind, float AmountAfterCover, float StanceDefense, float Fortitude, float KnockdownScale);
}
