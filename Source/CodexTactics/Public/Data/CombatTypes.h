#pragma once

#include "CoreMinimal.h"
#include "CombatTypes.generated.h"

/**
 * Elemental damage types matching Godot weapon_data.gd and enemy_base.gd elemental_affinities:
 * 0: KINETIC, 1: MELEE, 2: FIRE, 3: CRYO, 4: ENERGY, 5: EXPLOSIVE.
 */
UENUM(BlueprintType)
enum class EDamageType : uint8
{
	Kinetic UMETA(DisplayName = "Kinetic"),
	Melee UMETA(DisplayName = "Melee"),
	Fire UMETA(DisplayName = "Fire"),
	Cryo UMETA(DisplayName = "Cryo"),
	Energy UMETA(DisplayName = "Energy"),
	Explosive UMETA(DisplayName = "Explosive")
};

/**
 * Armor tiers matching Godot enemy_base.gd ArmorTier:
 * LIGHT (green), MEDIUM (yellow), HEAVY (red).
 */
UENUM(BlueprintType)
enum class EArmorTier : uint8
{
	Light UMETA(DisplayName = "Light"),
	Medium UMETA(DisplayName = "Medium"),
	Heavy UMETA(DisplayName = "Heavy")
};

/**
 * Status effects matching Godot weapon_data.gd / enemy_base.gd:
 * 0: None, 1: Burning, 2: Frozen, 3: Shocked/Stagger, 4: Bleeding, 5: ArmorShred.
 */
UENUM(BlueprintType)
enum class EStatusEffect : uint8
{
	None UMETA(DisplayName = "None"),
	Burning UMETA(DisplayName = "Burning"),
	Frozen UMETA(DisplayName = "Frozen"),
	Stagger UMETA(DisplayName = "Stagger"),
	Bleeding UMETA(DisplayName = "Bleeding"),
	ArmorShred UMETA(DisplayName = "Armor Shred")
};

/**
 * Enemy archetypes in Operation: Cold Silence.
 */
UENUM(BlueprintType)
enum class EEnemyArchetype : uint8
{
	Base UMETA(DisplayName = "Base"),
	FrostHound UMETA(DisplayName = "Frost Hound"),
	Spitter UMETA(DisplayName = "Spitter"),
	Brute UMETA(DisplayName = "Brute"),
	Frostbitten UMETA(DisplayName = "Frostbitten"),
	Cutter UMETA(DisplayName = "Cutter"),
	CryoDrone UMETA(DisplayName = "Cryo Drone")
};

/**
 * Attack shapes for weapons (Godot weapon_data.gd: RAYS_8, RAYS_4, MELEE_ADJ, FREE_TARGET).
 */
UENUM(BlueprintType)
enum class EAttackShape : uint8
{
	Rays8 UMETA(DisplayName = "8 Rays"),
	Rays4 UMETA(DisplayName = "4 Rays"),
	MeleeAdj UMETA(DisplayName = "Melee Adjacent"),
	FreeTarget UMETA(DisplayName = "Free Target")
};

/**
 * Elemental affinities table (multipliers per damage type).
 * Godot reference defaults: Kinetic 1.0, Melee 1.0, Fire 1.5, Cryo 0.0, Energy 1.0, Explosive 1.2.
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FElementalAffinities
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Affinity")
	float Kinetic = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Affinity")
	float Melee = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Affinity")
	float Fire = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Affinity")
	float Cryo = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Affinity")
	float Energy = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Affinity")
	float Explosive = 1.2f;

	float GetMultiplier(EDamageType DamageType) const
	{
		switch (DamageType)
		{
		case EDamageType::Kinetic: return Kinetic;
		case EDamageType::Melee: return Melee;
		case EDamageType::Fire: return Fire;
		case EDamageType::Cryo: return Cryo;
		case EDamageType::Energy: return Energy;
		case EDamageType::Explosive: return Explosive;
		default: return 1.0f;
		}
	}
};

/**
 * Specification for a single damage event passed to HealthComponent.
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FDamageSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Damage")
	float Amount = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Damage")
	EDamageType DamageType = EDamageType::Kinetic;

	/** Armor penetration fraction [0..1] that reduces target's effective armor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Damage", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ArmorPenetration = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Damage")
	FString AttackerSource;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Damage")
	EStatusEffect StatusEffect = EStatusEffect::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Damage")
	float StatusDuration = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Damage")
	float StatusTickDamage = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Damage")
	bool bIsCritical = false;
};
