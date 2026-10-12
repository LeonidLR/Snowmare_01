#pragma once

#include "CoreMinimal.h"
#include "Data/CombatTypes.h"
#include "Interactables/DeployableRules.h"
#include "VehicleHullRules.generated.h"

/**
 * Pure rules of the squad vehicle's hull (APC BTR-82, phase P4; docs/vehicles/APC_02_DESIGN.md §1.5, user decisions
 * 2026-10-12): hull health and the armour profile per damage source, disabled at 0 HP (never destroyed), the Engineer's
 * repair, the enemies' interest in a manned vehicle, and the explosion / mine damage to the hull. UE-only (Godot had no
 * vehicles): design-number tests in CodexTactics.Vehicle.Hull.*.
 */

/** What hit the hull (each source has its own armour factor). */
UENUM(BlueprintType)
enum class EVehicleDamageSource : uint8
{
	/** Claws and bites: hounds, frostbitten, cutters (and the base enemy). */
	SmallMelee,
	/** The Brute's heavy blow. */
	HeavyMelee,
	/** A spitter's acid / a cryo drone's spit. */
	Acid,
	/** The marksman's rifle. */
	Rifle,
	/** A blast nearby: a barrel, a grenade, a trapped object, a mine / tripwire that went off beside the hull. */
	Explosion,
	/** A mine / tripwire set off by the vehicle itself (under the wheels): the flat MineDamage. */
	Mine,
	/** Ramming a Brute at speed (VehicleCombatRules ram). */
	Ram,
	/** Anything else (full damage). */
	Other
};

/** Hull tuning (vehicle Blueprint, Unreal is master; weapons_tuning.json "vehicle_weapons.apc_hull" overrides at BeginPlay). */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FVehicleHullConfig
{
	GENERATED_BODY()

	/** Hull hit points (task: ~1200). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull", meta = (ClampMin = "1"))
	float MaxHealth = 1200.f;

	/** Claws / bites of the small enemies scratch the armour (design x0.1: a pile of hounds still hurts over time). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armour", meta = (ClampMin = "0"))
	float SmallMeleeFactor = 0.1f;

	/** The Brute's blow (design x1.0: the real threat). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armour", meta = (ClampMin = "0"))
	float HeavyMeleeFactor = 1.f;

	/** Spitter acid eats the paint, not the plate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armour", meta = (ClampMin = "0"))
	float AcidFactor = 0.5f;

	/** The marksman's rifle against armour (task x0.25). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armour", meta = (ClampMin = "0"))
	float RifleFactor = 0.25f;

	/** Barrels, grenades, trapped objects (design x1.0) - the blast's enemy damage at the hull's distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armour", meta = (ClampMin = "0"))
	float ExplosionFactor = 1.f;

	/** A mine / tripwire the vehicle sets off itself: flat damage (design 250; mines are THE threat). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armour", meta = (ClampMin = "0"))
	float MineDamage = 250.f;

	/** Anything unclassified. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armour", meta = (ClampMin = "0"))
	float OtherFactor = 1.f;

	/** Below this fraction of MaxHealth the crew warns once ("she won't take much more"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull", meta = (ClampMin = "0", ClampMax = "1"))
	float LowHealthFraction = 0.25f;

	/** The Engineer's repair of a disabled hull (task: hold ~5 s; no parts for now). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Repair", meta = (ClampMin = "0.1"))
	float RepairSeconds = 5.f;

	/** Repair time for anyone else; 0 = only the Engineer can repair it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Repair", meta = (ClampMin = "0"))
	float OtherRepairSeconds = 0.f;

	/** A repaired hull comes back at this fraction of MaxHealth (task: 50 %). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Repair", meta = (ClampMin = "0.01", ClampMax = "1"))
	float RepairFraction = 0.5f;

	/** The repairer works within this distance of the hull side. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Repair", meta = (ClampMin = "50"))
	float RepairReachCm = 220.f;

	/**
	 * Enemies go for a manned vehicle when it is the closest threat: its hull distance x this weight competes with the
	 * operatives' (small enemies weigh operatives 1.0, large ones 0.7; Godot turret weights 0.5 / 0.85).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemies", meta = (ClampMin = "0.1"))
	float EnemyTargetWeightSmall = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemies", meta = (ClampMin = "0.1"))
	float EnemyTargetWeightLarge = 0.85f;

	/** Enemies notice a manned vehicle within this distance (the engine / the guns are loud); hordes always know. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemies", meta = (ClampMin = "0"))
	float EnemyAwarenessCm = 4000.f;
};

/** The hull's state (saved: "hull_hp", "disabled"). */
struct CODEXTACTICS_API FVehicleHullState
{
	float Health = 0.f;
	bool bDisabled = false;
};

/** What one hit did to the hull. */
enum class EVehicleHullChange : uint8
{
	/** No damage (already disabled / nothing left after the armour). */
	None,
	Damaged,
	/** Damaged and crossed LowHealthFraction on this hit. */
	DamagedLow,
	/** Reached 0: disabled on this hit. */
	Disabled
};

/** Why a Ctrl+click on an object cannot be shelled (None = fire). */
UENUM(BlueprintType)
enum class EVehicleObjectShot : uint8
{
	None,
	/** Not a barrel / mine / trapped object (or already blown). */
	NotATarget,
	NoGunner,
	Disabled,
	/** Turn-based / the world AI pause: the turret is held. */
	Held,
	NoShells,
	OutOfRange,
	DeadZone,
	NoLineOfFire
};

namespace VehicleHullRules
{
	/** Armour factor of a source (Mine is flat, see GetHullDamage). */
	CODEXTACTICS_API float GetSourceFactor(EVehicleDamageSource Source, const FVehicleHullConfig& Config);

	/** Damage the hull takes from Amount of Source: Amount x factor; a Mine = the flat MineDamage. Never negative. */
	CODEXTACTICS_API float GetHullDamage(float Amount, EVehicleDamageSource Source, const FVehicleHullConfig& Config);

	/** The source of an enemy's attack on the hull: Brute -> HeavyMelee, spitter / cryo drone -> Acid, marksman -> Rifle, else SmallMelee. */
	CODEXTACTICS_API EVehicleDamageSource ClassifyEnemyAttack(EEnemyArchetype Archetype);

	/** A fresh hull: full health, running. */
	CODEXTACTICS_API FVehicleHullState MakeFullHull(const FVehicleHullConfig& Config);

	/**
	 * Applies HullDamage (already through the armour) to State: health down to 0 at the least; reaching 0 disables the hull
	 * (never destroyed). A disabled hull takes nothing more. OutDealt = the health actually lost.
	 */
	CODEXTACTICS_API EVehicleHullChange ApplyDamage(FVehicleHullState& State, float HullDamage, const FVehicleHullConfig& Config, float& OutDealt);

	/** Repair time for an operative of Role (the Engineer: RepairSeconds; others OtherRepairSeconds; <= 0 = cannot). */
	CODEXTACTICS_API float GetRepairSeconds(EOperativeRole Role, const FVehicleHullConfig& Config);

	/** The repair: running again at RepairFraction of MaxHealth (or more if it somehow had more). */
	CODEXTACTICS_API void Repair(FVehicleHullState& State, const FVehicleHullConfig& Config);

	/** A saved hull (load): health clamped to 0..MaxHealth, disabled at 0 health. */
	CODEXTACTICS_API FVehicleHullState RestoreHull(float SavedHealth, bool bSavedDisabled, const FVehicleHullConfig& Config);

	/**
	 * Is a vehicle an enemy target at all: manned (crew inside: the damage proxy), not disabled, and either known to the
	 * enemy's horde or within EnemyAwarenessCm of it (hull distance).
	 */
	CODEXTACTICS_API bool IsEnemyTarget(int32 CrewCount, bool bDisabled, bool bHordeKnowsSquad, float HullDistanceCm, const FVehicleHullConfig& Config);

	/** Target weight of a manned vehicle for an enemy (small / large). */
	CODEXTACTICS_API float GetEnemyTargetWeight(bool bSmallEnemy, const FVehicleHullConfig& Config);

	/**
	 * Explosion damage to the hull: a mine / tripwire the vehicle set off itself (bTriggeredByThisVehicle) = Mine (flat);
	 * otherwise BlastDamageAtHull (the blast's own falloff at the hull's distance) x ExplosionFactor.
	 */
	CODEXTACTICS_API float GetBlastHullDamage(float BlastDamageAtHull, bool bTriggeredByThisVehicle, const FVehicleHullConfig& Config, EVehicleDamageSource& OutSource);

	/** HUD line "HULL 840/1200" / "HULL DISABLED - Engineer [Y] repairs" / "HULL REPAIRING 60%". */
	CODEXTACTICS_API FString GetHudLine(const FVehicleHullState& State, const FVehicleHullConfig& Config, float RepairProgress);

	/** Short name of a source ("SmallMelee", ...; the VehicleDamaged event Kind). */
	CODEXTACTICS_API FString GetSourceName(EVehicleDamageSource Source);

	/** Inputs of a Ctrl+click on an object with the gunner's cannon. */
	struct FObjectShotCheck
	{
		/** A barrel / mine / trapped object (crate trap included) that can still go off. */
		bool bShootable = true;
		bool bHasGunner = true;
		bool bDisabled = false;
		bool bHeld = false;
		int32 ShellsLeft = 1;
		/** Muzzle to the object. */
		float DistanceCm = 0.f;
		/** The object to the hull side (2D). */
		float HullDistanceCm = 1000.f;
		bool bLineOfFire = true;
		float CannonRangeCm = 4000.f;
		float DeadZoneCm = 400.f;
	};

	/** The cannon may shell the object (None = fire; else the first refusal: not a target, disabled, no gunner, held, shells, range, dead zone, line). */
	CODEXTACTICS_API EVehicleObjectShot CheckObjectShot(const FObjectShotCheck& Check);
}
