#include "Combat/ProjectileActor.h"

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/ProjectileSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

AProjectileActor::AProjectileActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	Mesh->SetRelativeScale3D(FVector(0.12f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Sphere.Succeeded())
	{
		Mesh->SetStaticMesh(Sphere.Object);
	}
	SetActorHiddenInGame(true);
}

FExplosionSpec AProjectileActor::BlastSpecFor(const FProjectileLaunch& Launch, const FVector& Centre, AActor* Source)
{
	FExplosionSpec Spec;
	Spec.Kind = Launch.BlastKind.IsNone() ? ExplosionRules::ProjectileKind() : Launch.BlastKind;
	Spec.Centre = Centre;
	Spec.RadiusCm = Launch.Explosion.RadiusCm;
	Spec.EnemyDamage = Launch.BlastDamage;
	Spec.EnemyFalloff = ExplosionRules::FromWeapon(Launch.Explosion);
	Spec.SquadDamage = Launch.BlastDamage;
	Spec.SquadFalloff = Spec.EnemyFalloff;
	Spec.SquadScale = Launch.SquadBlastScale;
	Spec.DamageType = Launch.DamageType;
	Spec.ArmorPenetration = Launch.ArmorPenetration;
	Spec.PenetrationClass = Launch.PenetrationClass;
	Spec.SourceName = Launch.SourceName;
	Spec.Source = Source;
	Spec.Targets = EBlastTargets::AllActorsInRadius;
	Spec.bOtherBodiesKinetic = false;
	Spec.bChainBarrelsAndTraps = true;
	Spec.bSquadNoise = Launch.bSquadNoise;
	Spec.bPlunging = Launch.Projectile.IsHighAngle();
	return Spec;
}

void AProjectileActor::Launch(const FProjectileLaunch& InLaunch, float WorldGravityZ)
{
	Shot = InLaunch;
	Flight = FProjectileFlightState();
	Flight.Position = Shot.From;
	Flight.Velocity = Shot.Velocity;
	Flight.GravityZ = ProjectileRules::GravityFor(Shot.Projectile, WorldGravityZ);
	MaxFlightSeconds = ProjectileRules::MaxFlightSeconds(Shot.Projectile);
	bInFlight = true;
	SetActorLocationAndRotation(Shot.From, Shot.Velocity.Rotation());
	SetActorHiddenInGame(false);
	SetActorTickEnabled(true);
	ReceiveLaunched(Shot.Velocity);
}

void AProjectileActor::Park()
{
	bInFlight = false;
	SetActorTickEnabled(false);
	SetActorHiddenInGame(true);
}

void AProjectileActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Fly(DeltaSeconds);
}

void AProjectileActor::Fly(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!bInFlight || !World)
	{
		return;
	}
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	Objects.AddObjectTypesToQuery(ECC_Vehicle);
	Objects.AddObjectTypesToQuery(ECC_Destructible);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ProjectileFlight), false, this);
	if (AActor* Shooter = Shot.Instigator.Get())
	{
		Params.AddIgnoredActor(Shooter);
	}
	const FCollisionShape Shape = FCollisionShape::MakeSphere(FMath::Max(1.f, Shot.CollisionRadiusCm));
	FHitResult Struck;
	bool bStruck = false;
	ProjectileRules::Advance(Flight, DeltaSeconds, [&](const FVector& From, const FVector& To, int32 /*StepIndex*/)
	{
		bStruck = World->SweepSingleByObjectType(Struck, From, To, FQuat::Identity, Objects, Shape, Params);
		return bStruck;
	});
	if (bStruck)
	{
		Finish(ProjectileRules::IsArmed(Flight, Shot.Projectile.ArmingDistanceCm) ? EProjectileEnd::Impact : EProjectileEnd::Dud,
			Struck.bStartPenetrating ? FVector(Struck.TraceStart) : FVector(Struck.Location), Struck.GetActor());
		return;
	}
	if (Flight.FlightSeconds() >= MaxFlightSeconds)
	{
		Finish(ProjectileRules::IsArmed(Flight, Shot.Projectile.ArmingDistanceCm) ? EProjectileEnd::AirBurst : EProjectileEnd::Expired,
			Flight.Position, nullptr);
		return;
	}
	if (!Flight.Velocity.IsNearlyZero())
	{
		SetActorLocationAndRotation(Flight.Position, Flight.Velocity.Rotation());
	}
	else
	{
		SetActorLocation(Flight.Position);
	}
}

void AProjectileActor::Finish(EProjectileEnd End, const FVector& Location, AActor* HitActor)
{
	UWorld* World = GetWorld();
	bInFlight = false;
	FProjectileImpact Impact;
	Impact.End = End;
	Impact.Location = Location;
	Impact.HitActor = HitActor;
	Impact.FlightSeconds = Flight.FlightSeconds();
	Impact.FlightDistanceCm = Flight.DistanceCm;
	Impact.SourceName = Shot.SourceName;
	SetActorLocation(Location);
	// The direct share on the actor struck (armed or a dud).
	if (HitActor && Shot.DirectDamage > 0.f && (End == EProjectileEnd::Impact || End == EProjectileEnd::Dud))
	{
		const USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
		AOperativeCharacter* Operative = Cast<AOperativeCharacter>(HitActor);
		UHealthComponent* Health = HitActor->FindComponentByClass<UHealthComponent>();
		if (Operative && Squad && Squad->GetMembers().Contains(Operative))
		{
			Operative->TakeHit(Shot.DirectDamage, Shot.SourceName, false, true, Shot.Instigator.Get());
			Impact.DirectDamage = Shot.DirectDamage;
		}
		else if (Health && Health->IsAlive())
		{
			FDamageSpec Hit;
			Hit.Amount = Shot.DirectDamage;
			Hit.DamageType = End == EProjectileEnd::Dud ? EDamageType::Kinetic : Shot.DamageType;
			Hit.ArmorPenetration = Shot.ArmorPenetration;
			Hit.PenetrationClass = Shot.PenetrationClass;
			Hit.AttackerSource = Shot.SourceName;
			Health->TakeDamage(Hit);
			Impact.DirectDamage = Shot.DirectDamage;
		}
	}
	// The blast (an armed impact or an air burst).
	if ((End == EProjectileEnd::Impact || End == EProjectileEnd::AirBurst) && Shot.Explosion.RadiusCm > 0.f && World)
	{
		// Out of the wall's surface a little so the cover trace starts on the shooter's side.
		const FVector Centre = Location - Flight.Velocity.GetSafeNormal() * 5.f;
		Impact.Blast = Explosions::Detonate(World, BlastSpecFor(Shot, Centre, this));
		Impact.bExploded = true;
	}
	ReceiveFinished(Location, Impact.bExploded);
	Park();
	if (UProjectileSubsystem* Projectiles = World ? World->GetSubsystem<UProjectileSubsystem>() : nullptr)
	{
		Projectiles->Release(this, Impact);
	}
}
