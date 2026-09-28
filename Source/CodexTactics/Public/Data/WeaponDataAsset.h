#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/CombatTypes.h"
#include "WeaponDataAsset.generated.h"

/**
 * Data asset for weapons in Operation: Cold Silence.
 * Direct parity with Godot resources/weapon_data.gd.
 */
UCLASS(BlueprintType)
class CODEXTACTICS_API UWeaponDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Identity")
	FString WeaponId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Identity")
	FText WeaponName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Damage")
	EDamageType DamageType = EDamageType::Kinetic;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Damage")
	float BaseDamage = 18.0f;

	/** Attack range in Unreal centimeters (Godot attack_range * 100). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Damage")
	float AttackRangeCm = 1400.0f;

	/** Interval between shots in real-time seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Damage")
	float FireRate = 0.65f;

	/** Armor penetration fraction [0..1]. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Damage")
	float ArmorPenetration = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Ammo")
	bool bUsesAmmo = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Ammo")
	int32 MaxClipSize = 30;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Ammo")
	int32 DefaultReserveAmmo = 100;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Ammo")
	float ReloadTime = 2.68f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Status")
	EStatusEffect StatusEffect = EStatusEffect::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Status")
	float StatusDuration = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Status")
	float StatusTickDamage = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Thermal")
	float SelfColdGeneration = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Thermal")
	float SelfWarmthGeneration = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|TurnBased")
	EAttackShape AttackShape = EAttackShape::Rays8;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|TurnBased")
	int32 MaxRangeCells = 5;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|TurnBased")
	TArray<float> BaseHitChances;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|TurnBased")
	TArray<float> DistanceDamageMultipliers;
};
