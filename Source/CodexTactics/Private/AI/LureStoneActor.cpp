#include "AI/LureStoneActor.h"

#include "AI/StealthLureSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Combat/GrenadeRules.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

ALureStoneActor::ALureStoneActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetCastShadow(false);
	// Placeholder: a 7 cm engine sphere (the stone / bolt / cartridge).
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereFinder.Succeeded())
	{
		Mesh->SetStaticMesh(SphereFinder.Object);
	}
	Mesh->SetRelativeScale3D(FVector(0.07f));
	Tags.Add(TEXT("LureStone"));
}

void ALureStoneActor::Throw(AOperativeCharacter* InThrower, const FVector& Target, float ReleaseDelay)
{
	Thrower = InThrower;
	StartLocation = GetActorLocation();
	TargetLocation = Target;
	ReleaseLeft = FMath::Max(ReleaseDelay, 0.f);
	FlightDuration = GrenadeRules::FlightDuration(FVector::Dist2D(StartLocation, TargetLocation));
	FlightElapsed = 0.f;
	bFlying = true;
	SetActorHiddenInGame(ReleaseLeft > 0.f);
	SetActorTickEnabled(true);
}

void ALureStoneActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bFlying)
	{
		SetActorTickEnabled(false);
		return;
	}
	if (ReleaseLeft > 0.f)
	{
		// In the thrower's hand until the release point of the throw clip (like a grenade / flare).
		ReleaseLeft -= DeltaSeconds;
		if (const AOperativeCharacter* ThrowerActor = Thrower.Get())
		{
			StartLocation = ThrowerActor->GetActorLocation() + FVector(0.f, 0.f, GrenadeRules::HandHeight);
			SetActorLocation(StartLocation);
		}
		if (ReleaseLeft <= 0.f)
		{
			SetActorHiddenInGame(false);
		}
		return;
	}
	FlightElapsed += DeltaSeconds;
	const float Progress = FlightDuration > 0.f ? FMath::Clamp(FlightElapsed / FlightDuration, 0.f, 1.f) : 1.f;
	SetActorLocation(GrenadeRules::ArcPoint(StartLocation, TargetLocation, Progress));
	if (Progress >= 1.f)
	{
		Land();
	}
}

void ALureStoneActor::Land()
{
	bFlying = false;
	SetActorTickEnabled(false);
	SetActorHiddenInGame(false);
	SetActorLocation(TargetLocation + FVector(0.f, 0.f, 4.f));
	UE_LOG(LogCodexTactics, Display, TEXT("[Stealth] stone landed at %s"), *TargetLocation.ToCompactString());
	if (UStealthLureSubsystem* Lure = GetWorld() ? GetWorld()->GetSubsystem<UStealthLureSubsystem>() : nullptr)
	{
		Lure->EmitNoise(TargetLocation, EStealthLureKind::Throw, Thrower.Get());
	}
	OnLanded(TargetLocation);
	SetLifeSpan(FMath::Max(GroundLifetime, 0.1f));
}
