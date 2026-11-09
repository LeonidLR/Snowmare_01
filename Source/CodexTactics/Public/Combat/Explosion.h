#pragma once

#include "CoreMinimal.h"
#include "Combat/ExplosionRules.h"
#include "Data/CombatTypes.h"

class AActor;
class AOperativeCharacter;
class AVehiclePawn;
class UWorld;

/**
 * The shared explosion's world part (the weapons framework, 2026-11-09; docs/design/WEAPONS_FRAMEWORK.md 16): every blast - the grenade, the
 * fuel barrel, mines / tripwires / trapped objects, the projectiles (and later the RPG-7 and the mortar) - goes through Explosions::Detonate with
 * an FExplosionSpec. It applies the falloff to enemies / the squad / other bodies (explosive damage = armour class Vehicle: decision 1), the cover
 * rule when the kind uses it, chained barrels / traps, the vehicles' hulls, the noise / patrol alert, the knockdown, the suppression + the
 * explosion event (snow craters) and the OnDetonated hook (the camera shake; a knockback later). The grenade and the placed charges build their
 * specs with GrenadeSpec / ChargeSpec: the same numbers as their old code (parity: CodexTactics.Weapons.Explosion.Parity). UE-only.
 */

/** Who a blast looks at. */
enum class EBlastTargets : uint8
{
	/** Every actor whose origin is within the radius (the grenade: enemies, the squad, other bodies, barrels, trapped objects). */
	AllActorsInRadius,
	/** The registered enemies and the squad (the placed charges: mines, tripwires, barrels, trapped objects). */
	EnemiesAndSquad
};

/** One blast. */
struct CODEXTACTICS_API FExplosionSpec
{
	/** GameplayEvents::ExplosionKinds name / ExplosionRules::ProjectileKind (the event, the occlusion switch). */
	FName Kind;
	FVector Centre = FVector::ZeroVector;
	float RadiusCm = 0.f;
	/** Enemies (and the vehicles' hulls, and other bodies for AllActorsInRadius). */
	float EnemyDamage = 0.f;
	FBlastFalloff EnemyFalloff;
	/** The squad (their base already scaled by the source, e.g. x0.75 for a placed charge). */
	float SquadDamage = 0.f;
	FBlastFalloff SquadFalloff;
	/** The grenade's squad share (x0.65 of the enemy damage, Godot take_damage(applied * 0.65)); 1 = SquadDamage as given. */
	float SquadScale = 1.f;
	EDamageType DamageType = EDamageType::Explosive;
	float ArmorPenetration = 0.f;
	/** The class of the blast against armour: explosive = Vehicle (no class limit; decision 1). */
	EArmorClass PenetrationClass = EArmorClass::Vehicle;
	EStatusEffect Status = EStatusEffect::None;
	float StatusDuration = 0.f;
	float StatusTickDamage = 0.f;
	FString SourceName;
	EBlastTargets Targets = EBlastTargets::EnemiesAndSquad;
	/** AllActorsInRadius: bodies that are neither enemies nor the squad take the enemy curve as plain (Kinetic, no penetration) damage. */
	bool bOtherBodiesKinetic = true;
	/** The grenade: fuel barrels in reach explode, trapped objects go off. */
	bool bChainBarrelsAndTraps = false;
	/** Placed charges: their damage is a trap event (patrols search, no ambush; FScopedTrapBlast) and the patrols round it are alerted. */
	bool bTrapEvent = false;
	/** The squad's own blast: patrols hear it and engage (AEnemyCharacter::NotifySquadNoise Explosion). */
	bool bSquadNoise = false;
	/** Everyone within the knockdown radius (2.5 m) is knocked down (KnockdownRules). */
	bool bKnockdown = true;
	/** A plunging round (high-angle fire): walls do not protect, a roof does. */
	bool bPlunging = false;
	/** The cover rule: from the config by Kind unless set here. */
	TOptional<bool> OcclusionOverride;
	/** The vehicle that set the charge off itself (flat mine damage on its hull). */
	const AVehiclePawn* TriggeredByVehicle = nullptr;
	/** The exploding actor (never a victim; the event's subject). */
	AActor* Source = nullptr;
	/** Raise GameplayEvents::Explosion (+ the suppression report). */
	bool bRaiseEvent = true;
	/** A squad member hit (placed charges: he stops and the feed line names the HP lost). */
	TFunction<void(AOperativeCharacter& Member, float Damage)> OnSquadHit;
};

/** A body the blast reached. */
struct CODEXTACTICS_API FExplosionVictim
{
	TWeakObjectPtr<AActor> Actor;
	float DistanceCm = 0.f;
	/** The damage applied (before the body's own armour). */
	float Damage = 0.f;
	/** The cover multiplier used (1 = open). */
	float Cover = 1.f;
};

/** What a blast did. */
struct CODEXTACTICS_API FExplosionResult
{
	FName Kind;
	FVector Centre = FVector::ZeroVector;
	float RadiusCm = 0.f;
	TArray<FExplosionVictim> Victims;
	int32 Knockdowns = 0;
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnExplosionDetonated, UWorld* /*World*/, const FExplosionResult& /*Result*/);

namespace Explosions
{
	/** Applies the blast (see the file comment); returns who it reached. */
	CODEXTACTICS_API FExplosionResult Detonate(UWorld* World, const FExplosionSpec& Spec);

	/** After every blast (the camera shake; a knockback hook later). */
	CODEXTACTICS_API FOnExplosionDetonated& OnDetonated();

	/** The grenade's blast (GrenadeActor): every actor in reach, the grenade curve, the squad x0.65, chained barrels / traps, the squad noise. */
	CODEXTACTICS_API FExplosionSpec GrenadeSpec(const FVector& Centre, float Damage, float RadiusCm, const FString& SourceName, AActor* Source);

	/** A placed charge (AInteractableActor::ApplyBlast: mines, tripwires, barrels, trapped objects): enemies x0.4 / squad x0.5 linear curves. */
	CODEXTACTICS_API FExplosionSpec ChargeSpec(const FVector& Centre, float EnemyDamage, float SquadDamage, float RadiusCm, float ArmorPenetration,
		EDamageType DamageType, const FString& SourceName, AActor* Source);

	/** A wall between the blast and the body (a Visibility trace from 50 cm above the centre to the body's chest, the source and the body ignored). */
	CODEXTACTICS_API bool IsWallBetween(UWorld* World, const FVector& Centre, const AActor* Body, const AActor* Source);

	/** A roof / ceiling above the body (a Visibility trace 10 m straight up from its head). */
	CODEXTACTICS_API bool IsRoofAbove(UWorld* World, const AActor* Body);
}
