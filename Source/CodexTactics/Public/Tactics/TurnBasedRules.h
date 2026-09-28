#pragma once

#include "CoreMinimal.h"
#include "Characters/OperativeMovementRules.h"
#include "TurnBasedRules.generated.h"

class UGorkyGridManager;
class UWeaponDataAsset;

/**
 * Turn-based balance (Godot resources/game_balance_config.gd tactical_* exports; the defaults below are the Godot
 * defaults until the Phase 2 importer fills them from game_balance_config.tres).
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FTurnBasedBalance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TurnBased") int32 SquadMaxAP = 8;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TurnBased") int32 EnemyMaxAP = 6;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TurnBased") int32 MoveAPCost = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TurnBased") int32 DiagonalAPCost = 2;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TurnBased") int32 AttackAPCost = 3;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TurnBased") int32 PushBarrelAPCost = 2;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TurnBased") int32 StanceAPCost = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TurnBased") float CrouchAccuracyBonus = 0.15f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TurnBased") float ProneAccuracyBonus = 0.30f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TurnBased") float SquadBaseDamage = 30.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TurnBased") float EnemyBaseDamage = 18.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TurnBased") float TurretDamage = 25.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TurnBased") int32 TurretRange = 5;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TurnBased") float BarrelDamage = 80.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TurnBased") float CrouchDamageMultiplier = 0.70f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TurnBased") float ProneDamageMultiplier = 0.50f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TurnBased") float RearAttackMultiplier = 1.75f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TurnBased") float FlankAttackMultiplier = 1.25f;
};

/** One cell a weapon can hit from the attacker's cell. */
struct CODEXTACTICS_API FTurnBasedAttackCell
{
	int32 Distance = 1;
	int32 MaxRange = 1;
	float HitChance = 0.f;
	float ProjectedDamage = 0.f;
};

/**
 * Pure rules of the Gorky 17 turn-based combat.
 * Godot reference: Scripts/tactics/turn_based_combat_manager.gd calculate_hit_chance, calculate_turret_hit_chance,
 * get_weapon_attack_cells, _cast_attack_ray; resources/weapon_data.gd get_damage_for_distance.
 */
namespace TurnBasedRules
{
	/** Distance in cells (Chebyshev, diagonals count as one). */
	CODEXTACTICS_API int32 CellDistance(const FIntPoint& From, const FIntPoint& To);

	/** Godot fallback hit curve without weapon data (and for turrets): 95 / 85 / 75 / 65 / 50 %. */
	CODEXTACTICS_API float FallbackHitChance(int32 DistanceCells);

	/** Weapon curve (or the fallback) + stance bonus (crouch +15 %, prone +30 %), clamped to 5..99 %. */
	CODEXTACTICS_API float CalculateHitChance(const UWeaponDataAsset* Weapon, int32 DistanceCells, EOperativeStance Stance, const FTurnBasedBalance& Balance);

	CODEXTACTICS_API float CalculateTurretHitChance(int32 DistanceCells);

	/** Base damage x the weapon's distance multiplier (or FallbackDamage without weapon data). */
	CODEXTACTICS_API float GetDamageForDistance(const UWeaponDataAsset* Weapon, int32 DistanceCells, float FallbackDamage);

	/** Is the offset on the weapon's fire lanes (Godot weapon_data.gd is_target_in_pattern; no weapon = 8 rays of 5). */
	CODEXTACTICS_API bool IsTargetInPattern(const UWeaponDataAsset* Weapon, const FIntPoint& Offset);

	/** Incoming damage multiplier of the defender's stance (crouch 0.70, prone 0.50). */
	CODEXTACTICS_API float StanceDamageMultiplier(EOperativeStance Stance, const FTurnBasedBalance& Balance);

	/** Squad attack damage: max(1, round(base x arc multiplier - armour x arc armour multiplier)). */
	CODEXTACTICS_API int32 SquadAttackDamage(float BaseDamage, float ArcMultiplier, float Armor, float ArcArmorMultiplier);

	/** Enemy melee damage: max(1, round(base x arc multiplier x stance multiplier)). */
	CODEXTACTICS_API int32 EnemyAttackDamage(float BaseDamage, float ArcMultiplier, float StanceMultiplier);

	/**
	 * Cells the weapon can target from From: 8 or 4 rays stopped by the first occupant that is not a mine (the
	 * occupied cell itself is still a target), the 8 neighbours for melee, or any cell in range for free targeting.
	 */
	CODEXTACTICS_API TMap<FIntPoint, FTurnBasedAttackCell> GetWeaponAttackCells(const UGorkyGridManager& Grid, const FIntPoint& From,
		const UWeaponDataAsset* Weapon, EOperativeStance Stance, const FTurnBasedBalance& Balance);
}
