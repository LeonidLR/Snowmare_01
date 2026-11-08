#include "Combat/GrenadeActor.h"
#include "Combat/Explosion.h"
#include "Vehicles/VehicleSubsystem.h"
#include "Combat/KnockdownComponent.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/GrenadeRules.h"
#include "Combat/HealthComponent.h"
#include "Core/PlayerCommands.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/InteractableActor.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "GrenadeActor"

AGrenadeActor::AGrenadeActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Sphere.Succeeded())
	{
		Mesh->SetStaticMesh(Sphere.Object);
		Mesh->SetRelativeScale3D(FVector(0.12f)); // placeholder: a Blueprint subclass sets the real mesh
	}
	Tags.Add(TEXT("Grenade"));
}

void AGrenadeActor::ConfigureFrom(AOperativeCharacter* InThrower, float InReleaseDelay)
{
	Thrower = InThrower;
	ReleaseDelay = FMath::Max(InReleaseDelay, 0.f);
	if (InThrower)
	{
		Damage = InThrower->GrenadeDamage > 0.f ? InThrower->GrenadeDamage : Damage;
		EffectRadius = InThrower->GrenadeEffectRadius > 0.f ? InThrower->GrenadeEffectRadius : EffectRadius;
		ThrowRange = InThrower->GrenadeThrowRange > 0.f ? InThrower->GrenadeThrowRange : ThrowRange;
	}
	SetActorHiddenInGame(ReleaseDelay > 0.f);
}

FVector AGrenadeActor::ThrowTo(const FVector& WorldTarget)
{
	if (bDetonated)
	{
		return GetActorLocation();
	}
	StartLocation = GetActorLocation();
	const AOperativeCharacter* ThrowerActor = Thrower.Get();
	const float MaxRange = GrenadeRules::EffectiveRange(ThrowRange, ThrowerActor ? ThrowerActor->GetStance() : EOperativeStance::Standing);
	TargetLocation = GrenadeRules::ClampTarget(StartLocation, WorldTarget, MaxRange);
	FlightDuration = GrenadeRules::FlightDuration(FVector::Dist2D(StartLocation, TargetLocation));
	FlightElapsed = 0.f;
	bFlying = true;
	bLanded = false;
	SetActorTickEnabled(true);
	return TargetLocation;
}

void AGrenadeActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bFlying)
	{
		return;
	}
	if (ReleaseDelay > 0.f)
	{
		// In the thrower's hand until the release point of the animation.
		ReleaseDelay -= DeltaSeconds;
		if (const AOperativeCharacter* ThrowerActor = Thrower.Get())
		{
			StartLocation = ThrowerActor->GetActorLocation() + FVector(0.f, 0.f, GrenadeRules::HandHeight);
			SetActorLocation(StartLocation);
		}
		if (ReleaseDelay <= 0.f)
		{
			SetActorHiddenInGame(false);
		}
		return;
	}
	FlightElapsed += DeltaSeconds;
	const float Progress = FMath::Clamp(FlightElapsed / FlightDuration, 0.f, 1.f);
	SetActorLocation(GrenadeRules::ArcPoint(StartLocation, TargetLocation, Progress));
	if (Progress >= 1.f)
	{
		Land();
	}
}

void AGrenadeActor::Land()
{
	bFlying = false;
	bLanded = true;
	SetActorLocation(TargetLocation);
	SetActorTickEnabled(false);
	ReceiveLanded(TargetLocation);
	if (FuseTime <= 0.f)
	{
		Detonate();
		return;
	}
	TWeakObjectPtr<AGrenadeActor> WeakThis(this);
	GetWorldTimerManager().SetTimer(FuseHandle, FTimerDelegate::CreateLambda([WeakThis]()
	{
		if (WeakThis.IsValid())
		{
			WeakThis->Detonate();
		}
	}), FuseTime, false);
}

bool AGrenadeActor::CancelAndRefund()
{
	if (bDetonated)
	{
		return false;
	}
	bDetonated = true;
	bFlying = false;
	GetWorldTimerManager().ClearTimer(FuseHandle);
	if (AOperativeCharacter* ThrowerActor = Thrower.Get())
	{
		++ThrowerActor->GrenadesCount;
	}
	Destroy();
	return true;
}

void AGrenadeActor::Detonate()
{
	if (bDetonated)
	{
		return;
	}
	bDetonated = true;
	ApplyAreaEffect(); // raises GameplayEvents::Explosion (snow crater) at its end
	ReceiveDetonated(GetActorLocation());
	Destroy();
}

void AGrenadeActor::ApplyAreaEffect()
{
	// The shared explosion model (WEAPONS_FRAMEWORK.md 16; parity with the old inline code: CodexTactics.Weapons.Explosion.Parity): vehicles,
	// the squad noise (patrols engage), every actor in reach - barrels explode, trapped objects go off, enemies x the grenade curve, the
	// squad x0.65 (bypass avoidance), other bodies plain - the knockdown (2.5 m) and the explosion event (snow crater + suppression).
	Explosions::Detonate(GetWorld(), Explosions::GrenadeSpec(GetActorLocation(), Damage, EffectRadius, LOCTEXT("Source", "Grenade").ToString(), this));
}

#undef LOCTEXT_NAMESPACE
