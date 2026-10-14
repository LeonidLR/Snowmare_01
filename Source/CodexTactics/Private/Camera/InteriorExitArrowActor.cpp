#include "Camera/InteriorExitArrowActor.h"
#include "Camera/CameraZoneVolume.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "InteriorExitArrow"

namespace InteriorExitArrow
{
	/** Navigation language of the art bible: amber = the way / warmth. */
	const FLinearColor Amber(1.f, 0.45f, 0.03f);
	const FLinearColor AmberHot(1.f, 0.7f, 0.15f);
	/**
	 * The engine shape material (opaque, "Color"): reads amber over snow and bright floors (the additive combat-feedback glow
	 * washed out to white there); the glow material only when the engine one is missing.
	 */
	const TCHAR* ShapeMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
	const TCHAR* GlowMaterialPath = TEXT("/Game/VFX/Materials/M_CombatFeedback.M_CombatFeedback");
	/** Opaque text: the default translucent one faded into the background. */
	const TCHAR* TextMaterialPath = TEXT("/Engine/EngineMaterials/DefaultTextMaterialOpaque.DefaultTextMaterialOpaque");
}

AInteriorExitArrowActor::AInteriorExitArrowActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;

	ClickBox = CreateDefaultSubobject<UBoxComponent>(TEXT("ClickBox"));
	RootComponent = ClickBox;
	ClickBox->SetBoxExtent(FVector(85.f, 50.f, 45.f));
	// Only the cursor trace (Visibility) hits it: no blocking of bodies, cameras or shots.
	ClickBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ClickBox->SetCollisionObjectType(ECC_WorldDynamic);
	ClickBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	ClickBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	ClickBox->SetCanEverAffectNavigation(false);
	ClickBox->SetGenerateOverlapEvents(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(TEXT("/Engine/BasicShapes/Cone.Cone"));
	auto MakePart = [this](const TCHAR* Name, UStaticMesh* Mesh)
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(ClickBox);
		Part->SetStaticMesh(Mesh);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCanEverAffectNavigation(false);
		Part->SetCastShadow(false);
		return Part;
	};
	// A flat arrow on the floor pointing along local +X (out of the door): a 90 cm shaft and a 70 cm head.
	Shaft = MakePart(TEXT("Shaft"), CubeMesh.Object);
	Shaft->SetRelativeLocation(FVector(-25.f, 0.f, -25.f));
	Shaft->SetRelativeScale3D(FVector(0.9f, 0.3f, 0.06f));
	Head = MakePart(TEXT("Head"), ConeMesh.Object);
	Head->SetRelativeLocation(FVector(55.f, 0.f, -25.f));
	Head->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));
	Head->SetRelativeScale3D(FVector(0.7f, 0.7f, 0.5f));

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(ClickBox);
	Label->SetRelativeLocation(FVector(0.f, 0.f, 55.f));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextCenter);
	Label->SetWorldSize(56.f);
	Label->SetText(LOCTEXT("Exit", "EXIT"));
	Label->SetTextRenderColor(InteriorExitArrow::Amber.ToFColor(true));
	Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AInteriorExitArrowActor::Setup(ACameraZoneVolume* InZone, const FVector& DoorPoint, const FVector& Outward, const FVector& InExitTarget)
{
	Zone = InZone;
	ExitTarget = InExitTarget;
	OutwardDirection = Outward.GetSafeNormal2D().IsNearlyZero() ? FVector::ForwardVector : Outward.GetSafeNormal2D();
	SetActorLocationAndRotation(DoorPoint + FVector(0.f, 0.f, 30.f), OutwardDirection.Rotation());
	if (!Material)
	{
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, InteriorExitArrow::ShapeMaterialPath);
		if (!Base)
		{
			Base = LoadObject<UMaterialInterface>(nullptr, InteriorExitArrow::GlowMaterialPath);
		}
		if (UMaterialInterface* TextMaterial = LoadObject<UMaterialInterface>(nullptr, InteriorExitArrow::TextMaterialPath))
		{
			Label->SetTextMaterial(TextMaterial);
		}
		if (Base)
		{
			Material = UMaterialInstanceDynamic::Create(Base, this);
			Shaft->SetMaterial(0, Material);
			Head->SetMaterial(0, Material);
		}
	}
	ApplyColor();
}

void AInteriorExitArrowActor::SetHovered(bool bHovered)
{
	if (bIsHovered == bHovered)
	{
		return;
	}
	bIsHovered = bHovered;
	Label->SetText(bHovered ? LOCTEXT("ExitHover", "EXIT - click") : LOCTEXT("Exit", "EXIT"));
	ApplyColor();
}

void AInteriorExitArrowActor::ApplyColor()
{
	const FLinearColor Color = bIsHovered ? InteriorExitArrow::AmberHot : InteriorExitArrow::Amber;
	Label->SetTextRenderColor(Color.ToFColor(true));
	if (Material)
	{
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		Material->SetScalarParameterValue(TEXT("Intensity"), bIsHovered ? 2.f : 1.f); // more saturates the additive glow to white
	}
}

void AInteriorExitArrowActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// A slow pulse along the exit direction draws the eye; the label turns to the camera that shows it.
	PulseTime += FApp::GetDeltaTime();
	const float Pulse = FMath::Sin(PulseTime * 4.f) * 6.f;
	Shaft->SetRelativeLocation(FVector(-25.f + Pulse, 0.f, -25.f));
	Head->SetRelativeLocation(FVector(55.f + Pulse, 0.f, -25.f));
	if (const APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0); PC && PC->PlayerCameraManager)
	{
		const FVector ToCamera = PC->PlayerCameraManager->GetCameraLocation() - Label->GetComponentLocation();
		if (!ToCamera.IsNearlyZero())
		{
			Label->SetWorldRotation(ToCamera.Rotation());
		}
	}
}

#undef LOCTEXT_NAMESPACE
