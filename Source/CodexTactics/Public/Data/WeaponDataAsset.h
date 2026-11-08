#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/CombatTypes.h"
#include "Data/WeaponStats.h"
#include "WeaponDataAsset.generated.h"

class UStaticMesh;

/**
 * Data asset for weapons in Operation: Cold Silence. Godot reference: resources/weapon_data.gd.
 *
 * Since the weapons framework (2026-11-08, docs/design/WEAPONS_FRAMEWORK.md) gameplay code reads ONLY `Stats` (FWeaponStats, the
 * unified schema) plus the identity / visual fields. `Stats` is transient - the resolved runtime view, never saved:
 *   1. built from the asset's own "base" fields (the flat legacy properties at the end) on load / creation / an editor edit;
 *   2. overridden by Content/Data/Weapons/weapons_tuning.json at StartPlay (WeaponTuning::ApplyFile, validated).
 * The base fields keep their names so the existing DA_Weapon_* assets load unchanged; they are the fallback layer only. Do not
 * read them in gameplay code (guarded by CodexTactics.Weapons.Schema.NoLegacyReads).
 */
UCLASS(BlueprintType)
class CODEXTACTICS_API UWeaponDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// ---- Identity / visuals (not part of the tuning schema) ----

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Identity")
	FString WeaponId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Identity")
	FText WeaponName;

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

	/** Shot tracer / muzzle flash colour (Godot WeaponData tracer_color; M16 = default). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Visual")
	FLinearColor TracerColor = FLinearColor(0.2f, 1.f, 0.4f, 1.f);

	// ---- The schema (what gameplay reads) ----

	/** The weapon's resolved data: the base fields + weapons_tuning.json (StartPlay). Transient: never saved into the asset. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Stats")
	FWeaponStats Stats;

	/** Rebuilds Stats from the base fields (drops a JSON override until the next WeaponTuning::ApplyFile). */
	void RebuildStatsFromAsset();

	/** The base fields as schema stats (what Stats is before the JSON). Kind: Melee when the weapon uses no ammo, else Hitscan. */
	FWeaponStats MakeBaseStats() const;

	bool IsSniperRifle() const { return Stats.IsSniperRifle(); }
	bool HasFullAuto() const { return Stats.Fire.HasFullAuto(); }
	float GetEffectiveRangeCm() const { return Stats.Range.GetEffectiveRangeCm(); }

	/** Hit chance multiplier at DistanceCm (EffectiveScale: the stance / elevation bonus stretching the effective range); 0 beyond the max reach. */
	float GetRangeHitMultiplier(float DistanceCm, float EffectiveScale = 1.f) const { return Stats.Range.GetHitMultiplier(DistanceCm, EffectiveScale); }

	UFUNCTION(BlueprintPure, Category = "Weapon|TurnBased")
	float GetHitChanceForDistance(int32 DistanceCells) const { return Stats.Accuracy.GetHitChanceForCells(DistanceCells); }

	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	// ---- Base fields (legacy layout = the fallback layer; mapped into Stats by MakeBaseStats, see the migration table) ----

	/** Base of Stats.Damage.DamageType. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Base")
	EDamageType DamageType = EDamageType::Kinetic;

	/** Base of Stats.Handling. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Base")
	EWeaponHandling Handling = EWeaponHandling::Standard;

	/** Base of Stats.Damage.BaseDamage. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Base")
	float BaseDamage = 18.0f;

	/** Base of Stats.Range.MaxRangeCm (Godot attack_range * 100). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Base")
	float AttackRangeCm = 1400.0f;

	/** Base of Stats.Range.EffectiveRangeCm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Base", meta = (ClampMin = "0"))
	float EffectiveRangeCm = 0.0f;

	/** Base of Stats.Fire.ShotIntervalSeconds (an interval, s). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Base")
	float FireRate = 0.65f;

	/** Base of Stats.Damage.ArmorPenetration. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Base")
	float ArmorPenetration = 0.20f;

	/** Base of Stats.Ammo.bUsesAmmo. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Base")
	bool bUsesAmmo = true;

	/** Base of Stats.Ammo.MagazineSize. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Base")
	int32 MaxClipSize = 30;

	/** Base of Stats.Ammo.ReserveAmmo. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Base")
	int32 DefaultReserveAmmo = 100;

	/** Base of Stats.Ammo.ReloadSeconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Base")
	float ReloadTime = 2.68f;

	/** Base of Stats.Fire.FullAutoIntervalSeconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Base", meta = (ClampMin = "0"))
	float FullAutoInterval = 0.f;

	/** Base of Stats.Fire.SuppressionWeight. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Base", meta = (ClampMin = "0"))
	float SuppressionWeight = 1.f;

	/** Base of Stats.Status.Effect. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Base")
	EStatusEffect StatusEffect = EStatusEffect::None;

	/** Base of Stats.Status.DurationSeconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Base")
	float StatusDuration = 0.0f;

	/** Base of Stats.Status.TickDamage. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Base")
	float StatusTickDamage = 0.0f;

	/** Base of Stats.Thermal.SelfColdPerShot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Base")
	float SelfColdGeneration = 0.0f;

	/** Base of Stats.Thermal.SelfWarmthPerShot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Base")
	float SelfWarmthGeneration = 0.0f;

	/** Base of Stats.TurnBased.AttackShape. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Base")
	EAttackShape AttackShape = EAttackShape::Rays8;

	/** Base of Stats.TurnBased.MaxRangeCells. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Base")
	int32 MaxRangeCells = 5;

	/** Base of Stats.Accuracy.HitChanceByCell. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Base")
	TArray<float> BaseHitChances;

	/** Base of Stats.Accuracy.DamageMultiplierByCell. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Base")
	TArray<float> DistanceDamageMultipliers;
};
