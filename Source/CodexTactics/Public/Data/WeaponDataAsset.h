#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/CombatTypes.h"
#include "WeaponDataAsset.generated.h"

class UStaticMesh;

/** How the operative handles the weapon (fire stance, animation set). */
UENUM(BlueprintType)
enum class EWeaponHandling : uint8
{
	/** Any stance, also on the move (M16, pistol, ...). */
	Standard,
	/**
	 * Bolt-action sniper rifle (user request 2026-10-09): fires only kneeling / prone and standing still (SniperRules), works
	 * the bolt between shots, plays the operative's sniper animation set (UOperativeAnimInstance Sniper* clips).
	 */
	SniperRifle
};

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

	/** Fire stance rules and animation set (SniperRifle: kneeling / prone only, bolt between shots). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Handling")
	EWeaponHandling Handling = EWeaponHandling::Standard;

	/**
	 * Model in the operative's hand while this weapon is equipped (attached to AOperativeCharacter::WeaponSocket). Empty = the
	 * operative Blueprint's own WeaponMesh (the M16). Swap in a real model here (e.g. a sniper rifle) without touching code.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Visual")
	TSoftObjectPtr<UStaticMesh> HandMesh;

	/** Use HandMeshAttachTransform instead of the Blueprint's WeaponMesh offset (a new model usually needs its own grip offset). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Visual")
	bool bOverrideHandMeshTransform = false;

	/** HandMesh relative to the weapon socket (hand_r) when bOverrideHandMeshTransform is set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Visual", meta = (EditCondition = "bOverrideHandMeshTransform"))
	FTransform HandMeshAttachTransform;

	/** Muzzle on HandMesh (its own space) when the mesh has no "Muzzle" socket; zero = the operative's MuzzleOffset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Visual")
	FVector HandMeshMuzzleOffset = FVector::ZeroVector;

	bool IsSniperRifle() const { return Handling == EWeaponHandling::SniperRifle; }

	/** Shot tracer / muzzle flash colour (Godot WeaponData tracer_color; M16 = default). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Visual")
	FLinearColor TracerColor = FLinearColor(0.2f, 1.f, 0.4f, 1.f);

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

	UFUNCTION(BlueprintPure, Category = "Weapon|TurnBased")
	float GetHitChanceForDistance(int32 DistanceCells) const
	{
		if (BaseHitChances.Num() == 0)
		{
			return 0.85f;
		}
		const int32 Index = FMath::Clamp(DistanceCells - 1, 0, BaseHitChances.Num() - 1);
		return BaseHitChances[Index];
	}
};
