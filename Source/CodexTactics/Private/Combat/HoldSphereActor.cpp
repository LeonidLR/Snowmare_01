#include "Combat/HoldSphereActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Interactables/RadiusRingSubsystem.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

AHoldSphereActor::AHoldSphereActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Dome = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Dome"));
	SetRootComponent(Dome);
	Dome->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Dome->SetCastShadow(false);
	Dome->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereFinder.Succeeded())
	{
		Dome->SetStaticMesh(SphereFinder.Object);
	}
	SetActorHiddenInGame(true);
}

float AHoldSphereActor::EasedRadius(float Progress, float MaxRadius)
{
	const float P = FMath::Clamp(Progress, 0.f, 1.f);
	return FMath::Clamp(MaxRadius * FMath::Max(0.01f, P * (2.f - P)), 0.f, 1500.f);
}

void AHoldSphereActor::UpdateProgress(const FVector& Ground, float HoldTime, float TargetHoldDuration, float RadiusCap)
{
	if (HoldTime < 0.05f)
	{
		HideSphere();
		return;
	}
	UWorld* World = GetWorld();
	const float MaxRadius = FMath::Min(1500.f, RadiusCap);
	const float Progress = FMath::Clamp(HoldTime / FMath::Max(0.01f, TargetHoldDuration), 0.f, 1.f);
	CurrentRadius = EasedRadius(Progress, MaxRadius);
	bShown = true;
	SetActorHiddenInGame(false);

	// Dome: radius x (1, 1, 0.45), lifted 0.1 x radius; alpha 0.10 -> 0.25 (Godot).
	if (!DomeMaterial)
	{
		if (UMaterialInterface* Glow = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/VFX/Materials/M_CombatFeedback.M_CombatFeedback")))
		{
			DomeMaterial = UMaterialInstanceDynamic::Create(Glow, this);
			DomeMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.12f, 0.95f, 0.35f));
			Dome->SetMaterial(0, DomeMaterial);
		}
	}
	if (DomeMaterial)
	{
		DomeMaterial->SetScalarParameterValue(TEXT("Intensity"), FMath::Lerp(0.10f, 0.25f, Progress));
	}
	SetActorLocation(Ground + FVector(0.f, 0.f, CurrentRadius * 0.1f));
	SetActorScale3D(FVector(CurrentRadius, CurrentRadius, CurrentRadius * 0.45f) / 50.f); // engine sphere: 100 cm

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (!GrowingRing && World)
	{
		GrowingRing = World->SpawnActor<ARadiusRingActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	}
	if (!BoundaryRing && World)
	{
		BoundaryRing = World->SpawnActor<ARadiusRingActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	}
	// Ring alpha 0.4 -> 0.95, boundary 0.35 -> 0.85 (colour brightness stands in for the alpha of the additive glow).
	if (GrowingRing)
	{
		GrowingRing->ShowRing(Ground, CurrentRadius, FLinearColor(0.2f, 1.f, 0.4f) * FMath::Lerp(0.4f, 0.95f, Progress));
	}
	if (BoundaryRing)
	{
		BoundaryRing->ShowRing(Ground, MaxRadius, FLinearColor(0.15f, 0.9f, 0.45f) * FMath::Lerp(0.35f, 0.85f, Progress));
	}
}

void AHoldSphereActor::HideSphere()
{
	bShown = false;
	CurrentRadius = 0.f;
	SetActorHiddenInGame(true);
	if (GrowingRing)
	{
		GrowingRing->SetActorHiddenInGame(true);
	}
	if (BoundaryRing)
	{
		BoundaryRing->SetActorHiddenInGame(true);
	}
}
