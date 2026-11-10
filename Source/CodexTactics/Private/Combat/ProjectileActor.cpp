#include "Combat/ProjectileActor.h"

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/ProjectileSubsystem.h"
#include "Combat/SuppressionSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Vehicles/VehicleHullRules.h"
#include "Vehicles/VehiclePawn.h"
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
	VisualMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
	VisualMeshComponent->SetupAttachment(Mesh);
	VisualMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VisualMeshComponent->SetCastShadow(false);
	VisualMeshComponent->SetUsingAbsoluteScale(true); // the root sphere is scaled 0.12
	VisualMeshComponent->SetVisibility(false);
	SetActorHiddenInGame(true);
}

const UStaticMesh* AProjectileActor::GetVisualMesh() const
{
	return VisualMeshComponent && VisualMeshComponent->IsVisible() ? VisualMeshComponent->GetStaticMesh() : nullptr;
}

FTransform AProjectileActor::GetVisualTransform() const
{
	return VisualMeshComponent ? VisualMeshComponent->GetComponentTransform() : GetActorTransform();
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
	// The weapon's own round model (the RPG-7's rocket, nose along the flight) instead of the sphere; a pooled round keeps nothing of the last.
	const bool bOwnModel = Shot.VisualMesh != nullptr;
	Mesh->SetVisibility(!bOwnModel);
	VisualMeshComponent->SetStaticMesh(Shot.VisualMesh);
	// The root sphere's scale also scales a child's relative location: undo it so the model's offset stays in centimetres.
	const FVector RootScale = Mesh->GetRelativeScale3D();
	VisualMeshComponent->SetRelativeLocationAndRotation(Shot.VisualTransform.GetLocation() / RootScale.ComponentMax(FVector(0.001f)),
		Shot.VisualTransform.GetRotation());
	VisualMeshComponent->SetWorldScale3D(Shot.VisualTransform.GetScale3D());
	VisualMeshComponent->SetVisibility(bOwnModel);
	if (Shot.TrailFx)
	{
		Trail = UNiagaraFunctionLibrary::SpawnSystemAttached(Shot.TrailFx, Mesh, NAME_None, FVector::ZeroVector, FRotator::ZeroRotator,
			EAttachLocation::KeepRelativeOffset, false);
	}
	SetActorHiddenInGame(false);
	SetActorTickEnabled(true);
	ReceiveLaunched(Shot.Velocity);
}

void AProjectileActor::Park()
{
	bInFlight = false;
	if (Trail)
	{
		Trail->Deactivate(); // the smoke left behind fades on its own
		Trail->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		Trail = nullptr;
	}
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
	Impact.Instigator = Shot.Instigator;
	Impact.bLauncher = Shot.bLauncherSuppression;
	SetActorLocation(Location);
	// The direct hit on the actor struck (armed or a dud): its whole share - the blast below leaves it out (the designer 2026-11-09: the RPG-7's
	// direct hit = 140, not 140 + the splash).
	AActor* Struck = nullptr;
	if (HitActor && Shot.DirectDamage > 0.f && (End == EProjectileEnd::Impact || End == EProjectileEnd::Dud))
	{
		const USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
		AOperativeCharacter* Operative = Cast<AOperativeCharacter>(HitActor);
		UHealthComponent* Health = HitActor->FindComponentByClass<UHealthComponent>();
		if (AVehiclePawn* Vehicle = Cast<AVehiclePawn>(HitActor))
		{
			// A hull: the explosive hit through the vehicle's hull rules (as the blast's hull damage would be).
			EVehicleDamageSource DamageSource = End == EProjectileEnd::Dud ? EVehicleDamageSource::Other : EVehicleDamageSource::Explosion;
			if (End != EProjectileEnd::Dud)
			{
				VehicleHullRules::GetBlastHullDamage(Shot.DirectDamage, false, Vehicle->HullConfig, DamageSource);
			}
			Vehicle->ApplyHullDamage(Shot.DirectDamage, DamageSource, Shot.SourceName, Shot.Instigator.Get());
			Impact.DirectDamage = Shot.DirectDamage;
			Struck = Vehicle;
		}
		else if (Operative && Squad && Squad->GetMembers().Contains(Operative))
		{
			Operative->TakeHit(Shot.DirectDamage, Shot.SourceName, false, true, Shot.Instigator.Get());
			Impact.DirectDamage = Shot.DirectDamage;
			Struck = Operative;
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
			Struck = HitActor;
		}
	}
	// The blast (an armed impact or an air burst).
	if ((End == EProjectileEnd::Impact || End == EProjectileEnd::AirBurst) && Shot.Explosion.RadiusCm > 0.f && World)
	{
		// Out of the wall's surface a little so the cover trace starts on the shooter's side.
		const FVector Centre = Location - Flight.Velocity.GetSafeNormal() * 5.f;
		FExplosionSpec Spec = BlastSpecFor(Shot, Centre, this);
		Spec.StruckActor = Struck;
		Impact.Blast = Explosions::Detonate(World, Spec);
		Impact.bExploded = true;
		// The RPG-7 (the game designer 2026-11-09): "one RPG salvo is enough to make any squad duck" - a hit or a near miss.
		if (Shot.bLauncherSuppression)
		{
			Impact.SuppressedUnits = USuppressionSubsystem::ReportLauncherBlastIn(World, Centre, Shot.Instigator.Get());
		}
	}
	ReceiveFinished(Location, Impact.bExploded);
	Park();
	if (UProjectileSubsystem* Projectiles = World ? World->GetSubsystem<UProjectileSubsystem>() : nullptr)
	{
		Projectiles->Release(this, Impact);
	}
}
