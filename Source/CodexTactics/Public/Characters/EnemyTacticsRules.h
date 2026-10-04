#pragma once

#include "CoreMinimal.h"
#include "Data/CombatTypes.h"

/**
 * Pack tactics of the melee enemies (UE-only, no Godot reference: Godot enemy_base.gd only beelines to the nearest
 * victim). Pure rules, tested in CodexTactics.Characters.EnemyTactics.*; UEnemyTacticsSubsystem applies them to the
 * live enemies (real time), the turn-based AI reuses them. User decisions 2026-10-04: deeper, more challenging enemy
 * AI in both combat modes; per-archetype morale (fast ones fall back to the pack and come again, brutes / frostbitten
 * never do).
 */

/** How one archetype hunts (weights are fractions of the distance cost they remove). */
struct CODEXTACTICS_API FEnemyTacticsProfile
{
	/** Prefers a wounded operative: up to this share of the distance cost off at 0 HP. */
	float WoundedWeight = 0.3f;
	/** Prefers an operative standing apart (no mate within 6 m). */
	float IsolatedWeight = 0.2f;
	/** Prefers an operative in the open (not crouched behind a barricade). */
	float ExposedWeight = 0.15f;
	/** Prefers an operative facing away. */
	float BackWeight = 0.15f;
	/** At most this many melee enemies on one operative; more spread to the others (surround the squad). */
	int32 MaxAttackersPerTarget = 3;
	/** Share of the attackers on a target that go round its flank instead of straight at it. */
	float FlankShare = 0.34f;
	/** Morale: falls back below this health share ... */
	float MoraleHealthFraction = 0.f;
	/** ... or when this many pack mates died close by within the last few seconds (0: never). */
	int32 MoraleNearbyDeaths = 0;
	/** Never falls back, never flanks (brutes, frostbitten: they walk into the fire). */
	bool bHoldsGround = false;
};

/** What the enemy tactics know about one operative. */
struct CODEXTACTICS_API FEnemyTacticsTarget
{
	FVector Location = FVector::ZeroVector;
	FVector Forward = FVector::ForwardVector;
	float HealthFraction = 1.f;
	/** Distance to the closest squad mate (cm); huge when alone. */
	float NearestMateDistance = 0.f;
	bool bInCover = false;
	/** Melee enemies already sent at him. */
	int32 Attackers = 0;
	/** This enemy can reach him (no recent dead end, not inside a feared fire zone). */
	bool bUsable = true;
	/** This enemy's current target: costs 20 % less (no flip-flopping between equal targets). */
	bool bCurrent = false;
};

enum class EEnemyTacticRole : uint8
{
	/** Straight at the target (pins him). */
	Direct,
	/** Round the target's flank, away from his squad. */
	Flank,
	/** Morale broke: back to the pack, then in again. */
	FallBack
};

namespace EnemyTacticsRules
{
	/** The archetype's hunting style (hound pack hunter, cutter flanker, frostbitten / brute hold the ground). */
	CODEXTACTICS_API FEnemyTacticsProfile ProfileFor(EEnemyArchetype Archetype);

	/** Lower is better: distance (m) minus the profile's preferences; over-crowded targets cost extra. */
	CODEXTACTICS_API float TargetCost(const FEnemyTacticsProfile& Profile, const FVector& Enemy, const FEnemyTacticsTarget& Target);

	/** Index of the cheapest usable target, INDEX_NONE without one. */
	CODEXTACTICS_API int32 ChooseTarget(const FEnemyTacticsProfile& Profile, const FVector& Enemy, const TArray<FEnemyTacticsTarget>& Targets);

	/**
	 * Roles of one target's attackers (ordered closest first). Profiles[i] / PreviousRoles[i] (nullptr: new on this
	 * target): the flank share of the non-holding attackers goes round, at least one pins him; returning attackers keep
	 * their role where the share allows (no reshuffling as they run), newcomers fill from the farthest.
	 */
	CODEXTACTICS_API TArray<EEnemyTacticRole> AssignRoles(const TArray<FEnemyTacticsProfile>& Profiles,
		const TArray<const EEnemyTacticRole*>& PreviousRoles);

	/** Rank-th attacker (0 = closest) of AttackerCount on one target: Direct or Flank (the closest always pins). */
	CODEXTACTICS_API EEnemyTacticRole RoleFor(const FEnemyTacticsProfile& Profile, int32 Rank, int32 AttackerCount);

	/**
	 * A point DistanceCm from Target, AngleDegrees off the axis from the squad's centre through the target (so behind /
	 * beside him, away from his mates), on the side the enemy already is.
	 */
	CODEXTACTICS_API FVector FlankPoint(const FVector& Enemy, const FVector& Target, const FVector& SquadCentre,
		float DistanceCm = 450.f, float AngleDegrees = 70.f);

	/** The enemy attacks from behind (outside the target's front 120 degrees). */
	CODEXTACTICS_API bool IsBehind(const FVector& TargetLocation, const FVector& TargetForward, const FVector& Enemy);

	/** Morale check: below the health share or too many pack mates died near him. */
	CODEXTACTICS_API bool ShouldFallBack(const FEnemyTacticsProfile& Profile, float HealthFraction, int32 RecentNearbyDeaths);

	/** Where a broken enemy runs: DistanceCm away from the squad's centre, bent towards the pack's centre. */
	CODEXTACTICS_API FVector FallBackPoint(const FVector& Enemy, const FVector& SquadCentre, const FVector* PackCentre,
		float DistanceCm = 1000.f);
}
