#include "Tutorial/TutorialMarkerActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Components/PostProcessComponent.h"
#include "Environment/WeatherPresenterActor.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Tutorial/TutorialSubsystem.h"
#include "UObject/ConstructorHelpers.h"

namespace TutorialMarkerLocal
{
	constexpr float CylinderRadiusCm = 50.f; // /Engine/BasicShapes/Cylinder: 100 cm wide, 100 cm tall, pivot at the centre
	constexpr float RingThicknessCm = 3.f;
	const TCHAR* const LegacyMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

	bool IsHoloPart(const UStaticMeshComponent* Part)
	{
		const UMaterialInterface* Material = Part ? Part->GetMaterial(0) : nullptr;
		const UMaterial* Base = Material ? Material->GetMaterial() : nullptr;
		return Base && Base->GetPathName() == ATutorialMarkerActor::HoloMaterialPath;
	}
}

ATutorialMarkerActor::ATutorialMarkerActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false; // only while shown (the beacon bobs)
	MarkerRoot = CreateDefaultSubobject<USceneComponent>(TEXT("MarkerRoot"));
	SetRootComponent(MarkerRoot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(TutorialMarkerLocal::LegacyMaterialPath);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> HoloMaterial(HoloInstancePath);
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
	if (HoloMaterial.Succeeded())
	{
		MarkerMaterial = HoloMaterial.Object; // 2026-10-23: the orange hologram
	}
	else if (ShapeMaterial.Succeeded())
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
	// 2026-10-23: a marker left on the old engine placeholder (or none) wears the hologram like every other one.
	if (!MarkerMaterial || MarkerMaterial->GetPathName() == TutorialMarkerLocal::LegacyMaterialPath)
	{
		if (UMaterialInterface* Holo = LoadObject<UMaterialInterface>(nullptr, HoloInstancePath))
		{
			MarkerMaterial = Holo;
		}
	}
	if (!MarkerMaterial)
	{
		return;
	}
	MaterialInstance = UMaterialInstanceDynamic::Create(MarkerMaterial, this);
	if (bCustomColor)
	{
		MaterialInstance->SetVectorParameterValue(TEXT("Color"), Color);
	}
	Ring->SetMaterial(0, MaterialInstance);
	Beacon->SetMaterial(0, MaterialInstance);
	// Custom primitive data 0: the disc draws its rim and rings, the pole its scanlines (M_HoloMarker "IsDisc").
	Ring->SetCustomPrimitiveDataFloat(0, 1.f);
	Beacon->SetCustomPrimitiveDataFloat(0, 0.f);
}

bool ATutorialMarkerActor::IsHologram() const
{
	return TutorialMarkerLocal::IsHoloPart(Ring) && TutorialMarkerLocal::IsHoloPart(Beacon);
}

void ATutorialMarkerActor::ApplyLegacyLookForComparison()
{
	UMaterialInterface* Legacy = LoadObject<UMaterialInterface>(nullptr, TutorialMarkerLocal::LegacyMaterialPath);
	if (!Legacy)
	{
		return;
	}
	MaterialInstance = UMaterialInstanceDynamic::Create(Legacy, this);
	MaterialInstance->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.55f, 0.08f)); // the old amber
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
	SetActorTickEnabled(bShow); // the bob of the beacon, the grade's saturation compensation of the hologram
	SaturationCheckIn = 0.f;
	OnMarkerShownChanged(bShow);
}

void ATutorialMarkerActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// A slow bob of the beacon so the eye catches it; the ring stays on the ground.
	PulseTime += DeltaSeconds;
	if (bShowBeacon)
	{
		Beacon->SetRelativeLocation(FVector(0.f, 0.f, BeaconHeightCm * 0.5f + 15.f * FMath::Sin(PulseTime * 2.5f)));
	}
	SaturationCheckIn -= DeltaSeconds;
	if (SaturationCheckIn <= 0.f)
	{
		SaturationCheckIn = 0.5f;
		UpdateSaturationCompensation();
	}
}

void ATutorialMarkerActor::UpdateSaturationCompensation()
{
	if (!MaterialInstance)
	{
		return;
	}
	float Saturation = 1.f;
	const AWeatherPresenterActor* Presenter = AWeatherPresenterActor::Find(GetWorld());
	if (const UPostProcessComponent* Grade = Presenter ? Presenter->GetOwnPostProcess() : nullptr; Grade && Grade->Settings.bOverride_ColorSaturation)
	{
		Saturation = static_cast<float>(Grade->Settings.ColorSaturation.W);
	}
	MaterialInstance->SetScalarParameterValue(TEXT("SaturationCompensation"), FMath::Clamp(1.f / FMath::Max(Saturation, 0.05f), 1.f, 2.5f));
}

#if WITH_EDITOR
void ATutorialMarkerActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	ApplyShape();
}
#endif
