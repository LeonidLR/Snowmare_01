#include "Combat/GrenadeActor.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/GrenadeRules.h"
#include "Combat/HealthComponent.h"
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
	ApplyAreaEffect();
	ReceiveDetonated(GetActorLocation());
	Destroy();
}

void AGrenadeActor::ApplyAreaEffect()
{
	const FVector Center = GetActorLocation();
	const FText Source = LOCTEXT("Source", "Граната");
	// Patrols hear the squad's grenade (enemy_perception.json hear_explosion_m) and engage (user request 2026-10-06).
	AEnemyCharacter::NotifySquadNoise(GetWorld(), Center, ESquadNoise::Explosion);
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	TArray<AActor*> InRadius;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (*It != this && FVector::Dist(Center, It->GetActorLocation()) <= EffectRadius)
		{
			InRadius.Add(*It);
		}
	}
	for (AActor* Candidate : InRadius)
	{
		if (!IsValid(Candidate))
		{
			continue;
		}
		// 1. Fuel barrels explode; 2. trapped objects go off (Godot shoot_and_explode / detonate_trap).
		if (ABarrelActor* Barrel = Cast<ABarrelActor>(Candidate))
		{
			Barrel->Explode(Source);
		}
		else if (AInteractableActor* Object = Cast<AInteractableActor>(Candidate); Object && Object->bTrapped)
		{
			Object->DetonateTrap(true, Source);
		}
		// 3. Damage with the falloff.
		UHealthComponent* Health = IsValid(Candidate) ? Candidate->FindComponentByClass<UHealthComponent>() : nullptr;
		if (!Health || !Health->IsAlive())
		{
			continue;
		}
		const float Applied = Damage * GrenadeRules::DamageFalloff(FVector::Dist(Center, Candidate->GetActorLocation()), EffectRadius);
		if (Candidate->ActorHasTag(TEXT("Enemy")))
		{
			FDamageSpec Spec;
			Spec.Amount = Applied;
			Spec.DamageType = EDamageType::Explosive;
			Spec.AttackerSource = Source.ToString();
			Health->TakeDamage(Spec);
		}
		else if (AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Candidate); Operative && Squad && Squad->GetMembers().Contains(Operative))
		{
			Operative->TakeHit(Applied * GrenadeRules::SquadDamageScale, Source.ToString(), false, true); // Godot bypass_avoidance
		}
		else
		{
			FDamageSpec Spec;
			Spec.Amount = Applied;
			Spec.AttackerSource = Source.ToString();
			Health->TakeDamage(Spec);
		}
	}
}

#undef LOCTEXT_NAMESPACE
