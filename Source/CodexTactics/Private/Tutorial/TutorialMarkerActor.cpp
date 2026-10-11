#include "Tutorial/TutorialMarkerActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Tutorial/TutorialSubsystem.h"
#include "UObject/ConstructorHelpers.h"

namespace TutorialMarkerLocal
{
	constexpr float CylinderRadiusCm = 50.f; // /Engine/BasicShapes/Cylinder: 100 cm wide, 100 cm tall, pivot at the centre
	constexpr float RingThicknessCm = 3.f;
}

ATutorialMarkerActor::ATutorialMarkerActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false; // only while shown (the beacon bobs)
	MarkerRoot = CreateDefaultSubobject<USceneComponent>(TEXT("MarkerRoot"));
	SetRootComponent(MarkerRoot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	auto MakePart = [this](const TCHAR* Name) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(MarkerRoot);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetGenerateOverlapEvents(false);
		Part->SetCanEverAffectNavigation(false);
		Part->CastShadow = false;
		Part->bReceivesDecals = false;
		return Part;
	};
	Ring = MakePart(TEXT("Ring"));
	Beacon = MakePart(TEXT("Beacon"));
	if (CylinderMesh.Succeeded())
	{
		Ring->SetStaticMesh(CylinderMesh.Object);
		Beacon->SetStaticMesh(CylinderMesh.Object);
	}
	if (ShapeMaterial.Succeeded())
	{
		MarkerMaterial = ShapeMaterial.Object;
	}
	ApplyShape();
	// Visible in the editor; in the game only while its step / beat is active.
	SetActorHiddenInGame(true);
}

void ATutorialMarkerActor::ApplyShape()
{
	using namespace TutorialMarkerLocal;
	const float Scale = RingRadiusCm / CylinderRadiusCm;
	Ring->SetRelativeScale3D(FVector(Scale, Scale, RingThicknessCm / 100.f));
	Ring->SetRelativeLocation(FVector(0.f, 0.f, RingThicknessCm * 0.5f + 2.f));
	Beacon->SetRelativeScale3D(FVector(0.12f, 0.12f, BeaconHeightCm / 100.f));
	Beacon->SetRelativeLocation(FVector(0.f, 0.f, BeaconHeightCm * 0.5f));
	Beacon->SetVisibility(bShowBeacon);
}

void ATutorialMarkerActor::ApplyMaterial()
{
	if (!MarkerMaterial)
	{
		return;
	}
	MaterialInstance = UMaterialInstanceDynamic::Create(MarkerMaterial, this);
	MaterialInstance->SetVectorParameterValue(TEXT("Color"), Color);
	Ring->SetMaterial(0, MaterialInstance);
	Beacon->SetMaterial(0, MaterialInstance);
}

void ATutorialMarkerActor::BeginPlay()
{
	Super::BeginPlay();
	ApplyShape();
	ApplyMaterial();
	SetActorHiddenInGame(true);
	bShown = false;
	if (UTutorialSubsystem* Tutorial = GetWorld() ? GetWorld()->GetSubsystem<UTutorialSubsystem>() : nullptr)
	{
		Tutorial->RegisterMarker(this);
	}
}

void ATutorialMarkerActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UTutorialSubsystem* Tutorial = GetWorld() ? GetWorld()->GetSubsystem<UTutorialSubsystem>() : nullptr)
	{
		Tutorial->UnregisterMarker(this);
	}
	Super::EndPlay(EndPlayReason);
}

void ATutorialMarkerActor::SetMarkerShown(bool bShow)
{
	if (bShown == bShow)
	{
		return;
	}
	bShown = bShow;
	PulseTime = 0.f;
	SetActorHiddenInGame(!bShow);
	SetActorTickEnabled(bShow && bShowBeacon);
	OnMarkerShownChanged(bShow);
}

void ATutorialMarkerActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// A slow bob of the beacon so the eye catches it; the ring stays on the ground.
	PulseTime += DeltaSeconds;
	Beacon->SetRelativeLocation(FVector(0.f, 0.f, BeaconHeightCm * 0.5f + 15.f * FMath::Sin(PulseTime * 2.5f)));
}

#if WITH_EDITOR
void ATutorialMarkerActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	ApplyShape();
}
#endif
