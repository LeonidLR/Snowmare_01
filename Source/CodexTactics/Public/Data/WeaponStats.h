#pragma once

#include "CoreMinimal.h"
#include "Data/CombatTypes.h"
#include "Combat/WeaponRangeRules.h"
#include "WeaponStats.generated.h"

/**
 * The unified weapon schema (weapons framework step 1, week plan 2026-11-08; docs/design/WEAPONS_FRAMEWORK.md).
 * Godot reference: resources/weapon_data.gd (the common core: damage, range, fire rate, ammo, status, thermal, grid). UE-only:
 * kind, armour penetration class, damage falloff, per-round reload, spread scale, recoil and pellets (read since step 2, the
 * game designer's decisions 2026-11-08), min range + the projectile / explosion blocks (RESERVED for week 2: parsed and
 * validated only). Defaults mean the pre-framework behaviour (one pellet, no falloff, magazine reload, spread x1, no recoil).
 *
 * Every weapon user - the squad, the hostile PMC fighters (AOperativeCharacter with TeamId != 0), the turn-based grid -
 * reads UWeaponDataAsset::Stats, never the asset's flat fields. Units: centimetres / seconds in C++, metres / seconds in
 * Content/Data/Weapons/weapons_tuning.json (WeaponStatsRules converts).
 */

/** How a weapon delivers its damage (selects the per-kind blocks). Today every ranged kind fires the hitscan shot path. */
UENUM(BlueprintType)
enum class EWeaponKind : uint8
{
	/** Instant trace (M16, pistol, sniper rifle, shotgun, plasma carbine). */
	Hitscan,
	/** A flying round with speed / gravity (RPG-7, mortar shell; week 2). */
	Projectile,
	/** Thrown by hand on an arc (grenade). */
	Thrown,
	/** A blow within reach (knife, hammer). */
	Melee,
	/** A short continuous stream (flamethrower, cryo emitter). */
	Beam
};

/** How a reload refills the magazine (reserved: every weapon reloads the whole magazine today). */
UENUM(BlueprintType)
enum class EWeaponReloadMode : uint8
{
	/** One reload fills the magazine (ReloadSeconds). */
	Magazine,
	/** Round by round (a pump shotgun; PerRoundReloadSeconds each, interruptible). */
	PerRound
};

/* EArmorClass (armour classes of bodies / vehicles) lives in Data/CombatTypes.h: FDamageSpec carries it too. */

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

/** Damage of one round / blow before armour, cover and stance (JSON base_damage, damage_type, armor_penetration, penetration_class). */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FWeaponDamageStats
{
	GENERATED_BODY()

	/** Damage of one hit (one round, one blow, the blast centre), HP. Godot base_damage. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Damage", meta = (ClampMin = "0"))
	float BaseDamage = 18.0f;

	/** Elemental type (affinities, floating numbers, tracer). Godot damage_type. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Damage")
	EDamageType DamageType = EDamageType::Kinetic;

	/** Fraction [0..1] of the target's armour reduction the hit ignores (UHealthComponent::TakeDamage). Godot armor_penetration. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Damage", meta = (ClampMin = "0", ClampMax = "1"))
	float ArmorPenetration = 0.20f;

	/** The highest armour class the weapon defeats: against a higher-class body its ArmorPenetration does not apply (ArmorRules, decision 1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Damage")
	EArmorClass PenetrationClass = EArmorClass::Light;

	/** Damage multiplier at the max reach, linear from the effective range (WeaponBallisticsRules, decision 4); 1 = no damage falloff. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Damage", meta = (ClampMin = "0", ClampMax = "1"))
	float FalloffDamageFloor = 1.f;
};

/** Distances (JSON attack_range_m, effective_range_m, min_range_m). */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FWeaponRangeStats
{
	GENERATED_BODY()

	/** The MAX reach, cm: no shot beyond it (bug #39). Godot attack_range x 100. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Range", meta = (ClampMin = "0"))
	float MaxRangeCm = 1400.0f;

	/**
	 * The effective range, cm (bug #39): up to it the distance table applies unchanged, from it to MaxRangeCm the hit chance falls
	 * steeply (WeaponRangeRules). 0 = MaxRangeCm (no falloff: melee, the grenade).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Range", meta = (ClampMin = "0"))
	float EffectiveRangeCm = 0.0f;

	/** RESERVED (week 2, the mortar's 30 m): no shot closer than this, cm; 0 = none. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Range", meta = (ClampMin = "0"))
	float MinRangeCm = 0.0f;

	/** The effective range resolved into (0, MaxRangeCm]. */
	float GetEffectiveRangeCm() const { return WeaponRangeRules::ResolveEffectiveCm(EffectiveRangeCm, MaxRangeCm); }

	/** Hit chance multiplier at DistanceCm (EffectiveScale: the stance / elevation bonus stretching the effective range); 0 beyond the max reach. */
	float GetHitMultiplier(float DistanceCm, float EffectiveScale = 1.f) const
	{
		return WeaponRangeRules::HitMultiplier(DistanceCm, WeaponRangeRules::ScaledEffectiveCm(EffectiveRangeCm, MaxRangeCm, EffectiveScale), MaxRangeCm,
			WeaponRangeRules::GetFalloff());
	}
};

/** Cadence and suppression (JSON fire_rate, full_auto_interval, suppression_weight). */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FWeaponFireStats
{
	GENERATED_BODY()

	/** Seconds between aimed shots (JSON "fire_rate" - an interval, not shots per second; Godot fire_rate). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Fire", meta = (ClampMin = "0"))
	float ShotIntervalSeconds = 0.65f;

	/**
	 * Seconds between rounds on full-auto, used by the SUPPRESS order (user decision 2026-10-26: assault rifles switch to full-auto
	 * while suppressing; docs/design/SUPPRESSION.md). 0 = no full-auto: the order fires at ShotIntervalSeconds.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Fire", meta = (ClampMin = "0"))
	float FullAutoIntervalSeconds = 0.f;

	/** This weapon's rounds suppress x this (SuppressionRules weight by fire class x it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Fire", meta = (ClampMin = "0"))
	float SuppressionWeight = 1.f;

	bool HasFullAuto() const { return FullAutoIntervalSeconds > 0.f; }
};

/** Magazine and reload (JSON uses_ammo, max_clip_size, default_reserve_ammo, reload_time, reload_mode, reload_per_round_s). */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FWeaponAmmoStats
{
	GENERATED_BODY()

	/** False for melee weapons (no clip, no reload). Godot uses_ammo. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Ammo")
	bool bUsesAmmo = true;

	/** Rounds in a full magazine. Godot max_clip_size. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Ammo", meta = (ClampMin = "0"))
	int32 MagazineSize = 30;

	/** Rounds carried besides the magazine at the start; -1 = infinite (the pistol). Godot default_reserve_ammo. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Ammo", meta = (ClampMin = "-1"))
	int32 ReserveAmmo = 100;

	/** Seconds of a full reload. Godot reload_time. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Ammo", meta = (ClampMin = "0"))
	float ReloadSeconds = 2.68f;

	/** The whole magazine at once, or round by round (the shotgun, decision 3; WeaponBallisticsRules). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Ammo")
	EWeaponReloadMode ReloadMode = EWeaponReloadMode::Magazine;

	/** Seconds per round when ReloadMode is PerRound; 0 = ReloadSeconds / MagazineSize. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Ammo", meta = (ClampMin = "0"))
	float PerRoundReloadSeconds = 0.f;
};

/** Hit chance / damage by distance, spread and recoil (JSON base_hit_chances, distance_damage_multipliers, spread_scale, recoil_*). */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FWeaponAccuracyStats
{
	GENERATED_BODY()

	/**
	 * Hit chance per distance cell (1.5 m in real time - round(m / 1.5) - one grid cell in turn-based), clamped to the last
	 * entry; empty = 0.85. Godot base_hit_chances.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Accuracy")
	TArray<float> HitChanceByCell;

	/** Damage multiplier per distance cell (same cells), clamped to the last entry; empty = x1. Godot distance_damage_multipliers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Accuracy")
	TArray<float> DamageMultiplierByCell;

	/** x the quick fire / SUPPRESS stance spread cone (QuickFireRules / SuppressionRules); 1 = as before. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Accuracy", meta = (ClampMin = "0"))
	float SpreadScale = 1.f;

	/** Spread added per full-auto round of the SUPPRESS order, degrees (bloom, decision 5); 0 = no recoil. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Accuracy", meta = (ClampMin = "0"))
	float RecoilPerRoundDeg = 0.f;

	/** Seconds for one round's recoil to settle (the bloom falls at RecoilPerRoundDeg / this per second). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Accuracy", meta = (ClampMin = "0"))
	float RecoilRecoverySeconds = 0.f;

	/** The hit chance at DistanceCells (1-based, clamped into the table); 0.85 with no table (Godot get_hit_chance_for_distance). */
	float GetHitChanceForCells(int32 DistanceCells) const
	{
		if (HitChanceByCell.Num() == 0)
		{
			return 0.85f;
		}
		return HitChanceByCell[FMath::Clamp(DistanceCells - 1, 0, HitChanceByCell.Num() - 1)];
	}
};

/** Status applied by a hit (JSON status_effect, status_duration, status_tick_damage). */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FWeaponStatusStats
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Status")
	EStatusEffect Effect = EStatusEffect::None;

	/** Seconds the status lasts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Status", meta = (ClampMin = "0"))
	float DurationSeconds = 0.0f;

	/** Damage per status tick (burning, bleeding), HP. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Status", meta = (ClampMin = "0"))
	float TickDamage = 0.0f;
};

/** The shooter's own cold change per shot (JSON self_cold_generation, self_warmth_generation; cryo emitter / flamethrower). */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FWeaponThermalStats
{
	GENERATED_BODY()

	/** Cold points added to the shooter per shot (0..100 scale). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Thermal", meta = (ClampMin = "0"))
	float SelfColdPerShot = 0.0f;

	/** Cold points removed from the shooter per shot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Thermal", meta = (ClampMin = "0"))
	float SelfWarmthPerShot = 0.0f;
};

/** The Gorky grid attack (JSON attack_shape, max_range_cells). */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FWeaponTurnBasedStats
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|TurnBased")
	EAttackShape AttackShape = EAttackShape::Rays8;

	/** Grid reach, cells. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|TurnBased", meta = (ClampMin = "1"))
	int32 MaxRangeCells = 5;
};

/** Per-kind block (Hitscan; the shotgun, decision 2): several pellets per aimed shot, each rolling the hit chance (JSON "pellets"). */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FWeaponPelletStats
{
	GENERATED_BODY()

	/** Pellets per aimed shot; 1 = a single bullet. BaseDamage = the whole shot at point blank, split evenly over Count (decision 2). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Pellets", meta = (ClampMin = "1"))
	int32 Count = 1;

	/** Half-angle of the pellet cone, degrees (data for the visuals / the area fire; the aimed shot rolls per pellet). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Pellets", meta = (ClampMin = "0", ClampMax = "45"))
	float ConeHalfAngleDeg = 0.f;
};

/** RESERVED per-kind block (Projectile / Thrown; RPG-7, mortar, week 2): the flying round (JSON "projectile"). */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FWeaponProjectileStats
{
	GENERATED_BODY()

	/** Launch speed, cm/s (JSON speed_mps x 100); required > 0 for a Projectile. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Projectile", meta = (ClampMin = "0"))
	float SpeedCmS = 0.f;

	/** x world gravity; 0 = a flat flight (rocket), 1 = a ballistic arc. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Projectile", meta = (ClampMin = "0"))
	float GravityScale = 0.f;

	/** The warhead arms after this flight distance, cm; closer = no explosion. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Projectile", meta = (ClampMin = "0"))
	float ArmingDistanceCm = 0.f;

	/** Plunging high-angle fire over walls (the mortar; SQUAD_SIX_FIRE_TEAMS.md 7.3). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Projectile")
	bool bHighAngle = false;
};

/** RESERVED per-kind block (Projectile / Thrown; shared explosion code, week 2): the blast (JSON "explosion"). */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FWeaponExplosionStats
{
	GENERATED_BODY()

	/** Blast radius, cm; 0 = no blast (a direct hit only). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Explosion", meta = (ClampMin = "0"))
	float RadiusCm = 0.f;

	/** Full damage inside this radius, cm (<= RadiusCm); linear falloff to EdgeDamageMultiplier at RadiusCm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Explosion", meta = (ClampMin = "0"))
	float FullDamageRadiusCm = 0.f;

	/** Damage multiplier at the blast's edge. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Explosion", meta = (ClampMin = "0", ClampMax = "1"))
	float EdgeDamageMultiplier = 1.f;

	/** The RPG-7 back-blast danger zone behind the shooter: length, cm; 0 = none. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Explosion", meta = (ClampMin = "0"))
	float BackBlastLengthCm = 0.f;

	/** Half-angle of the back-blast cone, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Explosion", meta = (ClampMin = "0", ClampMax = "90"))
	float BackBlastHalfAngleDeg = 0.f;
};

/**
 * One weapon's complete tunable data: the common core every kind has + the per-kind blocks (read only for their kinds).
 * Built from the asset's own fields (UWeaponDataAsset::RebuildStatsFromAsset) and overridden by weapons_tuning.json at StartPlay
 * (WeaponTuning::ApplyFile -> WeaponStatsRules::ApplyJson + Validate).
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FWeaponStats
{
	GENERATED_BODY()

	/** Delivery kind (JSON "kind"); today every ranged kind fires the hitscan path, the dispatch by kind starts 2026-11-09. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	EWeaponKind Kind = EWeaponKind::Hitscan;

	/** Fire stance rules and animation set (JSON "handling"; SniperRifle: kneeling / prone only, bolt between shots). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	EWeaponHandling Handling = EWeaponHandling::Standard;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	FWeaponDamageStats Damage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	FWeaponRangeStats Range;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	FWeaponFireStats Fire;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	FWeaponAmmoStats Ammo;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	FWeaponAccuracyStats Accuracy;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	FWeaponStatusStats Status;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	FWeaponThermalStats Thermal;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	FWeaponTurnBasedStats TurnBased;

	/** RESERVED per-kind blocks (defaults = today's behaviour). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|PerKind")
	FWeaponPelletStats Pellets;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|PerKind")
	FWeaponProjectileStats Projectile;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|PerKind")
	FWeaponExplosionStats Explosion;

	bool IsSniperRifle() const { return Handling == EWeaponHandling::SniperRifle; }
};
