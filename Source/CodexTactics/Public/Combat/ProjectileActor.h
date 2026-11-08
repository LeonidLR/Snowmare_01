#pragma once

#include "CoreMinimal.h"
#include "Combat/Explosion.h"
#include "Combat/ProjectileRules.h"
#include "Data/CombatTypes.h"
#include "Data/WeaponStats.h"
#include "GameFramework/Actor.h"
#include "ProjectileActor.generated.h"

class UStaticMeshComponent;

/** One shot of a projectile weapon (UProjectileSubsystem::Fire). */
struct CODEXTACTICS_API FProjectileLaunch
{
	FVector From = FVector::ZeroVector;
	/** The launch velocity, cm/s (ProjectileRules::SolveLaunch / UProjectileSubsystem::FireAt). */
	FVector Velocity = FVector::ZeroVector;
	/** The weapon's projectile block (trajectory, gravity scale, arming, max flight). */
	FWeaponProjectileStats Projectile;
	/** The weapon's explosion block (radius 0 = no blast: a solid round). */
	FWeaponExplosionStats Explosion;
	/** On the actor the round strikes (armed or a dud). */
	float DirectDamage = 0.f;
	/** The blast's base damage (enemies, bodies, hulls; the squad x SquadBlastScale). */
	float BlastDamage = 0.f;
	float SquadBlastScale = 1.f;
	EDamageType DamageType = EDamageType::Explosive;
	float ArmorPenetration = 0.f;
	/** The round's class against armour (explosive = Vehicle; decision 1). */
	EArmorClass PenetrationClass = EArmorClass::Vehicle;
	/** The shooter (never struck by his own round). */
	TWeakObjectPtr<AActor> Instigator;
	FString SourceName;
	/** The blast kind (the event + the cover switch); none = ExplosionRules::ProjectileKind. */
	FName BlastKind;
	/** The squad fired it: patrols hear the blast and engage. */
	bool bSquadNoise = false;
	/** The sweep radius, cm. */
	float CollisionRadiusCm = 8.f;
};

/** How a flight ended. */
enum class EProjectileEnd : uint8
{
	/** Armed, struck something: exploded. */
	Impact,
	/** Struck something before arming: no blast (the direct share only). */
	Dud,
	/** Past max_flight_s: an armed round bursts in the air, an unarmed one vanishes. */
	AirBurst,
	Expired
};

/** A finished flight (UProjectileSubsystem::OnProjectileFinished). */
struct CODEXTACTICS_API FProjectileImpact
{
	EProjectileEnd End = EProjectileEnd::Expired;
	FVector Location = FVector::ZeroVector;
	TWeakObjectPtr<AActor> HitActor;
	float DirectDamage = 0.f;
	float FlightSeconds = 0.f;
	float FlightDistanceCm = 0.f;
	/** The blast (empty when there was none). */
	FExplosionResult Blast;
	bool bExploded = false;
	FString SourceName;
};

/**
 * A pooled flying round of the shared projectile layer (the weapons framework, 2026-11-09; docs/design/WEAPONS_FRAMEWORK.md 16; new - no Godot
 * reference). Flies ProjectileRules' fixed steps, sweeps each step's segment against geometry, characters and vehicles (the shooter ignored),
 * then: armed -> the direct share on the actor struck + Explosions::Detonate at the impact (a high-angle round = a plunging blast: walls do not
 * protect, a roof does); unarmed -> a dud; past max_flight_s -> an air burst. Taken from / given back to UProjectileSubsystem's pool. A
 * Blueprint subclass can add the mesh / VFX / sound in ReceiveLaunched / ReceiveFinished.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API AProjectileActor : public AActor
{
	GENERATED_BODY()

public:
	AProjectileActor();

	virtual void Tick(float DeltaSeconds) override;

	/** Starts the flight (the pool calls it). */
	void Launch(const FProjectileLaunch& InLaunch, float WorldGravityZ);

	/** Back to the pool state: hidden, no tick. */
	void Park();

	bool IsInFlight() const { return bInFlight; }
	const FProjectileFlightState& GetFlight() const { return Flight; }
	const FProjectileLaunch& GetLaunch() const { return Shot; }

	/** Flies DeltaSeconds now (the tick's step; smokes can drive it). */
	void Fly(float DeltaSeconds);

	/**
	 * The blast of a round bursting at Centre: every actor in reach (barrels chain), the weapon block's full-core curve (ExplosionRules::FromWeapon)
	 * for enemies, bodies and hulls, the squad x SquadBlastScale, explosive bodies (no plain kinetic share), the cover rule by kind (on for
	 * projectiles), plunging for a high-angle round, the squad's noise when the squad fired it.
	 */
	static FExplosionSpec BlastSpecFor(const FProjectileLaunch& Launch, const FVector& Centre, AActor* Source);

	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Projectile")
	void ReceiveLaunched(const FVector& Velocity);

	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Projectile")
	void ReceiveFinished(const FVector& Location, bool bExploded);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Projectile")
	TObjectPtr<UStaticMeshComponent> Mesh;

private:
	/** The flight's end: the damage, the blast, the report, back to the pool. */
	void Finish(EProjectileEnd End, const FVector& Location, AActor* HitActor);

	FProjectileLaunch Shot;
	FProjectileFlightState Flight;
	float MaxFlightSeconds = ProjectileRules::DefaultMaxFlightSeconds;
	bool bInFlight = false;
};
