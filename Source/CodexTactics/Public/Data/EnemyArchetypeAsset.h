#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/CombatTypes.h"
#include "EnemyArchetypeAsset.generated.h"

/**
 * Data asset defining an enemy archetype.
 * Parity with Godot enemy_base.gd + enemy_*.gd + game_balance_config.gd.
 */
UCLASS(BlueprintType)
class CODEXTACTICS_API UEnemyArchetypeAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Identity")
	EEnemyArchetype Archetype = EEnemyArchetype::FrostHound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Identity")
	FText EnemyName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Stats")
	float MaxHealth = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Stats")
	EArmorTier ArmorTier = EArmorTier::Light;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Stats")
	float BaseArmorReduction = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Movement")
	float MoveSpeedCm = 540.0f; // Hound: 5.4 m/s -> 540 cm/s

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Movement")
	float Acceleration = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Combat")
	float AttackDamage = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Combat")
	float AttackRangeCm = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Combat")
	float AttackCooldown = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Combat")
	float CritChance = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Combat")
	float CritMultiplier = 1.75f;

	/** Preferred standoff distance for ranged enemies like Spitter (Godot: 12.0m -> 1200 cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Combat")
	float PreferredDistanceCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Fear")
	bool bFearsFire = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Fear")
	float FireFearRadiusCm = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Combat")
	float BarricadeDamageMultiplier = 1.0f; // Brute has 2.0x

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Defense")
	FElementalAffinities ElementalAffinities;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Reward")
	int32 ExpReward = 9;
};
