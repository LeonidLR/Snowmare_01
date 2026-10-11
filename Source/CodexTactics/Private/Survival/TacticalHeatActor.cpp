#include "Survival/TacticalHeatActor.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Interactables/HeatSourceComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/PackageName.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Survival/TacticalHeatTimerComponent.h"
#include "UObject/ConstructorHelpers.h"

ATacticalHeatActor::ATacticalHeatActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); // never blocks a click, a path or a shot
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BaseMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (BaseMaterial.Succeeded())
	{
		Mesh->SetMaterial(0, BaseMaterial.Object);
	}

	HeatSource = CreateDefaultSubobject<UHeatSourceComponent>(TEXT("HeatSource"));
	HeatSource->SetupAttachment(Root);
	HeatSource->bHeatActive = false;
	HeatSource->Tolerance = 0.f;
	// Warm zones are spheres around the source and operatives are measured at their capsule centre: centre the zone at body
	// height so the radius reads as a distance on the ground.
	HeatSource->SetRelativeLocation(FVector(0.f, 0.f, 90.f));

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Root);
	Light->SetRelativeLocation(FVector(0.f, 0.f, 80.f));
	Light->SetIntensityUnits(ELightUnits::Candelas);
	Light->SetIntensity(LightIntensity);
	Light->SetAttenuationRadius(800.f);
	Light->SetCastShadows(false);
	Light->SetVisibility(false);

	Effect = CreateDefaultSubobject<UNiagaraComponent>(TEXT("Effect"));
	Effect->SetupAttachment(Root);
	Effect->SetAutoActivate(false);

	Timer = CreateDefaultSubobject<UTacticalHeatTimerComponent>(TEXT("Timer"));
}

void ATacticalHeatActor::SetPlaceholderShape(const TCHAR* MeshPath, const FVector& Scale, const FVector& Offset)
{
	ConstructorHelpers::FObjectFinder<UStaticMesh> Shape(MeshPath);
	if (Shape.Succeeded())
	{
		Mesh->SetStaticMesh(Shape.Object);
	}
	Mesh->SetRelativeScale3D(Scale);
	Mesh->SetRelativeLocation(Offset);
}

void ATacticalHeatActor::BeginPlay()
{
	Super::BeginPlay();
	Timer->OnAdvanced.AddUObject(this, &ATacticalHeatActor::HandleAdvanced);
	Timer->OnBurntOut.AddUObject(this, &ATacticalHeatActor::HandleBurntOut);
	ApplyBurningVisuals();
}

bool ATacticalHeatActor::IsBurning() const
{
	return Timer && Timer->IsBurning();
}

bool ATacticalHeatActor::IsBurntOut() const
{
	return Timer && Timer->IsBurntOut();
}

float ATacticalHeatActor::GetSaveRoundSeconds() const
{
	return Timer ? Timer->GetSecondsPerRound() : 0.f;
}

float ATacticalHeatActor::GetTimeLeft() const
{
	return Timer ? Timer->GetTimeLeft() : 0.f;
}

void ATacticalHeatActor::Ignite(float Seconds, float SecondsPerRound)
{
	Timer->Ignite(Seconds, SecondsPerRound);
	ApplyBurningVisuals();
}

void ATacticalHeatActor::RestoreHeatState(bool bBurning, bool bBurntOut, float TimeLeft, float SecondsPerRound)
{
	Timer->Restore(bBurning, bBurntOut, TimeLeft, SecondsPerRound);
	ApplyBurningVisuals();
	if (!bBurning && bBurntOut && SpentLifetime > 0.f)
	{
		SetLifeSpan(SpentLifetime);
	}
}

void ATacticalHeatActor::SetEffectActive(UNiagaraComponent* InEffect, const TSoftObjectPtr<UNiagaraSystem>& Asset, bool bActive)
{
	if (!InEffect)
	{
		return;
	}
	if (bActive && !InEffect->GetAsset() && !Asset.IsNull())
	{
		UNiagaraSystem* System = Asset.Get();
		if (!System && FPackageName::DoesPackageExist(Asset.ToSoftObjectPath().GetLongPackageName()))
		{
			System = Asset.LoadSynchronous();
		}
		if (System)
		{
			InEffect->SetAsset(System);
		}
	}
	if (bActive && InEffect->GetAsset())
	{
		InEffect->Activate(true);
	}
	else if (!bActive && InEffect->IsActive())
	{
		InEffect->Deactivate();
	}
}

void ATacticalHeatActor::ApplyBurningVisuals()
{
	const bool bBurning = IsBurning();
	HeatSource->SetHeatActive(bBurning);
	Light->SetVisibility(bBurning);
	Light->SetIntensity(LightIntensity);
	SetEffectActive(Effect, EffectAsset, bBurning);
	if (!BodyMaterial && Mesh->GetStaticMesh())
	{
		BodyMaterial = Mesh->CreateAndSetMaterialInstanceDynamic(0);
	}
	if (BodyMaterial)
	{
		BodyMaterial->SetVectorParameterValue(TEXT("Color"), bBurning ? BurningColor : SpentColor);
	}
	if (bBurning != bLastBurning)
	{
		bLastBurning = bBurning;
		ReceiveBurningChanged(bBurning);
	}
}

void ATacticalHeatActor::HandleAdvanced(float DeltaSeconds)
{
	const float TimeLeft = GetTimeLeft();
	const float Strength = FadeSeconds > 0.f && TimeLeft < FadeSeconds ? FMath::Clamp(TimeLeft / FadeSeconds, 0.f, 1.f) : 1.f;
	Light->SetIntensity(LightIntensity * Strength * FMath::FRandRange(0.9f, 1.1f));
}

void ATacticalHeatActor::HandleBurntOut()
{
	ApplyBurningVisuals();
	if (SpentLifetime > 0.f)
	{
		SetLifeSpan(SpentLifetime);
	}
}
