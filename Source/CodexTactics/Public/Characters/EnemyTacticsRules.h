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

/**
 * The flank detour of a Flank-role enemy (user decision 2026-10-18: "a real detour"): it walks to its flank point first and
 * closes in only from there - or once it is already within bite reach on the flank side. A blocked / unreachable point never
 * freezes it: after StuckSeconds without progress or TimeoutSeconds in all it goes straight in. Codex.Enemy.Flank* knobs.
 */
struct CODEXTACTICS_API FFlankDetourConfig
{
	/** At the flank point this close (planar), cm. */
	float ArriveCm = 150.f;
	/** "On the flank side": the enemy's direction from the target within this of the flank point's, degrees. */
	float SideConeDeg = 60.f;
	/** Within bite reach: the planar bite reach plus this, cm. */
	float BiteSlackCm = 40.f;
	/** Gives the detour up after this long, s. */
	float TimeoutSeconds = 8.f;
	/** ... or after this long without 50 cm of progress (a blocked point), s. */
	float StuckSeconds = 1.5f;
	/** Codex.Enemy.FlankDetour 0: the pre-2026-10-18 cut straight in within 5 m of the target (A/B). */
	bool bLegacyFiveMetreCut = false;
};

/** One tick of a flank detour as the rule sees it. */
struct CODEXTACTICS_API FFlankDetourInput
{
	float DistanceToPointCm = 0.f;
	float DistanceToTargetCm = 0.f;
	/** Planar bite reach for the height difference (EnemyAIRules::MeleeReachPlanar), cm. */
	float BiteReachCm = 150.f;
	/** Angle between (enemy - target) and (flank point - target), degrees. */
	float AngleFromFlankDeg = 180.f;
	float ElapsedSeconds = 0.f;
	float StuckSeconds = 0.f;
};

/** Why a flank detour ended (Continue = keep walking to the flank point). */
enum class EFlankDetourResult : uint8
{
	Continue,
	AtFlankPoint,
	BiteFromFlank,
	Stuck,
	Timeout,
	LegacyCut
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
	 * A point DistanceCm from Target, AngleDegrees off the line from the target to the enemy (so beside him, as seen from the
	 * enemy), turned to his OUTER side (away from the squad's centre; a target at the centre: a fixed side). Reworked 2026-10-18:
	 * the old point was AngleDegrees off the centre->target axis towards the enemy - in front of a wing operative of a line.
	 */
	CODEXTACTICS_API FVector FlankPoint(const FVector& Enemy, const FVector& Target, const FVector& SquadCentre,
		float DistanceCm = 450.f, float AngleDegrees = 90.f);

	/** The flank detour: keep walking to the point, or why it ends (FFlankDetourConfig). */
	CODEXTACTICS_API EFlankDetourResult EvaluateFlankDetour(const FFlankDetourInput& Input, const FFlankDetourConfig& Config);

	/** Short name of a result (logs). */
	CODEXTACTICS_API const TCHAR* FlankDetourResultName(EFlankDetourResult Result);

	/** The enemy attacks from behind (outside the target's front 120 degrees). */
	CODEXTACTICS_API bool IsBehind(const FVector& TargetLocation, const FVector& TargetForward, const FVector& Enemy);

	/** Morale check: below the health share or too many pack mates died near him. */
	CODEXTACTICS_API bool ShouldFallBack(const FEnemyTacticsProfile& Profile, float HealthFraction, int32 RecentNearbyDeaths);

	/** Where a broken enemy runs: DistanceCm away from the squad's centre, bent towards the pack's centre. */
	CODEXTACTICS_API FVector FallBackPoint(const FVector& Enemy, const FVector& SquadCentre, const FVector* PackCentre,
		float DistanceCm = 1000.f);
}
