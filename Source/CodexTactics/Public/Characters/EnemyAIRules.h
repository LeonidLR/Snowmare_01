#pragma once

#include "CoreMinimal.h"
#include "Characters/OperativeMovementRules.h"
#include "Combat/SquadFireRules.h"
#include "Data/CombatTypes.h"

class UGodotBalanceAsset;

/** Godot enemy_base.gd / enemy_frost_spitter.gd tuning shared by all enemies (game_balance_config enemy_* keys). */
struct CODEXTACTICS_API FEnemyAIConfig
{
	bool bCanTargetTurrets = true;
	/** A turret this close pulls large enemies off the squad, cm. */
	float TurretThreatDistance = 1000.f;
	bool bFireFearEnabled = true;
	float FireFearRadius = 600.f;
	float FireFearFleeSpeedMultiplier = 1.2f;
	/** Spitters keep about this distance, cm. */
	float SpitterPreferredRange = 1200.f;
	/** Crouched behind a barricade: spitter damage cut (Godot crouch_barricade_cover_reduction). */
	float CrouchCoverReduction = 0.35f;
};

enum class EEnemyTargetKind : uint8
{
	Operative,
	Turret,
	Generator,
	/** A manned squad vehicle (APC P4, UE-only): weighed by the candidate's own Weight. */
	Vehicle
};

/** A possible victim (Godot groups "squad", "turrets", "generators"). */
struct CODEXTACTICS_API FEnemyTargetCandidate
{
	EEnemyTargetKind Kind = EEnemyTargetKind::Operative;
	/** Godot global_position: operatives at their centre (1 m above the feet), objects at their base. */
	FVector Location = FVector::ZeroVector;
	/** Alive, not broken and (turrets) attackable. */
	bool bUsable = true;
	/** Vehicle candidates only: distance weight (VehicleHullRules::GetEnemyTargetWeight; Location = the nearest hull point). */
	float Weight = 1.f;
};

/** What a spitter does this frame (Godot enemy_frost_spitter.gd _process_enemy_behavior). */
enum class ESpitterMove : uint8
{
	Approach,
	Retreat,
	Hold
};

/** Spitter line of fire (Godot _check_line_of_sight). */
struct CODEXTACTICS_API FSpitterLine
{
	bool bHasLos = false;
	/** Damage factor: 1 - cover reduction when the target crouches behind a barricade. */
	float Cover = 1.f;
};

/** Enemy AI rules (Godot Scenes/movements/enemy_base.gd, enemy_frost_*.gd). Distances in cm, Godot positions. */
namespace EnemyAIRules
{
	CODEXTACTICS_API FEnemyAIConfig ConfigFromBalance(const UGodotBalanceAsset* Balance);

	/**
	 * The archetype of an enemy placed in a level (not made by a spawner, which sets it itself; user bug 2026-10-12: a placed
	 * BP_Enemy_Brute kept the property default FrostHound and played a 45 HP hound): the designer's instance value when it
	 * differs from the class default; else the game mode's archetype -> class mapping, reverse-looked-up over ClassChain (the
	 * actor's class first, then its parents: a child of a mapped Blueprint counts); else the class default (a C++ subclass such
	 * as AMarksmanEnemyCharacter sets its own). OutSource: "instance" / "class mapping" / "class default".
	 */
	CODEXTACTICS_API EEnemyArchetype ResolvePlacedArchetype(EEnemyArchetype InstanceValue, EEnemyArchetype ClassDefaultValue, const TArray<FString>& ClassChain,
		const TArray<TPair<EEnemyArchetype, FString>>& Mapping, FString& OutSource);

	/**
	 * Nameplate / feed name: the Blueprint's DisplayNameOverride (trimmed) when it has one (a variant body of an archetype:
	 * BP_Enemy_FrozenPMC_Fighter -> "Frozen PMC"), else the archetype's name.
	 */
	CODEXTACTICS_API FString ResolveDisplayName(const FString& ArchetypeName, const FString& Override);

	/**
	 * The knockdown clip sub-folder of /Game/Animations_KnockDown for a Blueprint's KnockdownClipFolder override: slashes
	 * and a pasted "/Game/Animations_KnockDown" prefix are stripped ("" = the folder itself, the UE5 Manny clips).
	 */
	CODEXTACTICS_API FString NormalizeKnockdownClipFolder(const FString& Folder);

	/** Hounds, cutters and frostbitten go for the generator / turrets first (Godot is_small_enemy). */
	CODEXTACTICS_API bool IsSmallEnemy(EEnemyArchetype Archetype);

	/** Godot per-type elemental_affinities (enemy_base default, hound, spitter, brute, frostbitten, cutter, cryo drone). */
	CODEXTACTICS_API FElementalAffinities GetAffinities(EEnemyArchetype Archetype);

	/** Godot base_armor_reduction per type. */
	CODEXTACTICS_API float GetBaseArmor(EEnemyArchetype Archetype);

	/**
	 * Godot _find_closest_squad_member: small enemies weigh generators at 0.4 x distance and turrets at 0.5 x, then
	 * operatives at 1 x; large ones weigh operatives at 0.7 x and then turrets at 0.85 x (0.6 x when a turret hit
	 * them last), also taking a turret within the threat distance unless something is clearly closer. APC P4: a Vehicle
	 * candidate (a manned APC) competes at distance x its own Weight, right after the operatives.
	 * Returns the candidate index or INDEX_NONE.
	 */
	CODEXTACTICS_API int32 SelectTarget(const FEnemyAIConfig& Config, bool bSmallEnemy, const FVector& Enemy,
		const TArray<FEnemyTargetCandidate>& Candidates, bool bLastAttackerTurret);

	/**
	 * Godot _find_blocking_barricade: the closest obstacle within 2.2 m that lies towards the target (dot > 0.15), or
	 * any within 2.2 m without a target. Returns the index or INDEX_NONE.
	 */
	CODEXTACTICS_API int32 SelectBlockingObstacle(const FVector& Enemy, const FVector* Target, const TArray<FVector>& Obstacles);

	/** Godot can_melee_attack: within the attack range and at most 1.2 m of height difference. */
	CODEXTACTICS_API bool CanMelee(float DistanceCm, float HeightDifferenceCm, float AttackRangeCm);

	/** Planar distance at which CanMelee passes for that height difference (sqrt(range^2 - height^2)), cm. */
	CODEXTACTICS_API float MeleeReachPlanar(float AttackRangeCm, float HeightDifferenceCm);

	/**
	 * Acceptance radius of the close-in move to a melee target (UE-only, bug hunt 2026-10-18). Godot walks on until
	 * can_melee_attack; UE's MoveToActor reach test adds both capsule radii to the acceptance, so AttackRange x 0.5
	 * (90 + 40 + 40 = 170 cm for a hound) stopped hounds / frostbitten 1.65-1.75 m from the operative while the bite
	 * needs <= 1.5 m planar (1.8 m to his centre 1 m up): they stood there, never bit, gave the target up ("no way").
	 * Returns min(AttackRange x 0.5, planar reach - both radii - MarginCm floored to 5 cm), at least 0.
	 */
	CODEXTACTICS_API float MeleeApproachAcceptance(float AttackRangeCm, float HeightDifferenceCm, float SelfRadiusCm, float GoalRadiusCm,
		float MarginCm = 15.f);

	/** Godot fire fear steering: away from the fire, bent 0.5 towards the side of the target (when there is one). */
	CODEXTACTICS_API FVector FleeDirection(const FVector& Enemy, const FVector& Fire, const FVector* Target);

	/**
	 * Detour around a fire / heat zone (user request 2026-09-30; Godot only flees, which makes the enemy shake at the
	 * edge). When the straight way from Enemy to Target crosses the circle of AvoidRadius around Fire, OutWaypoint is
	 * the next point on the circle (AvoidRadius + 1 m) at most StepDegrees further round towards the Target side;
	 * a Target inside the zone (a running generator) is waited for at the nearest point of the edge.
	 * @return false when the way is clear (walk straight).
	 */
	CODEXTACTICS_API bool FireDetourWaypoint(const FVector& Enemy, const FVector& Fire, float AvoidRadius, const FVector& Target,
		FVector& OutWaypoint, float StepDegrees = 35.f);

	/** Godot _find_spitter_target score: 100 - distance (m) + 50 on elevated ground; under a platform (dead zone) skipped. */
	CODEXTACTICS_API int32 SelectSpitterTarget(const FVector& Spitter, const TArray<FVector>& Operatives, const TArray<bool>& Elevated);

	/** Approach without a line of sight or beyond preferred + 2 m, back off inside preferred - 3 m, else hold. */
	CODEXTACTICS_API ESpitterMove SpitterMove(bool bHasLos, float DistanceCm, float PreferredRangeCm);

	/** Barricades: a prone target is hidden, a crouched one gets cover (1 - reduction), a standing one none; walls block. */
	CODEXTACTICS_API FSpitterLine JudgeSpitterLine(EShotLineHit Hit, EOperativeStance TargetStance, float CrouchCoverReduction);
}
