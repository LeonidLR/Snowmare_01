#include "UI/OperativePortraitCapture.h"

#include "Characters/OperativeCharacter.h"
#include "Components/PointLightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"

AOperativePortraitCapture::AOperativePortraitCapture()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	SetActorEnableCollision(false);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Movable);

	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
	Capture->SetupAttachment(RootComponent);
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->bAlwaysPersistRenderingState = true;
	Capture->ShowFlags.SetFog(false);
	Capture->ShowFlags.SetAtmosphere(false);
	Capture->ShowFlags.SetVolumetricFog(false);
	// The exposure adapts to the lit body alone (the scene behind is not rendered).
	Capture->PostProcessSettings.bOverride_AutoExposureMinBrightness = true;
	Capture->PostProcessSettings.AutoExposureMinBrightness = -3.f;
	Capture->PostProcessSettings.bOverride_AutoExposureMaxBrightness = true;
	Capture->PostProcessSettings.AutoExposureMaxBrightness = 12.f;
	Capture->PostProcessSettings.bOverride_VignetteIntensity = true;
	Capture->PostProcessSettings.VignetteIntensity = 0.f;
	Capture->PostProcessBlendWeight = 1.f;

	auto MakeLight = [this](const TCHAR* Name, float Candelas, const FVector& Offset, const FLinearColor& Color)
	{
		UPointLightComponent* Light = CreateDefaultSubobject<UPointLightComponent>(Name);
		Light->SetupAttachment(RootComponent);
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetIntensityUnits(ELightUnits::Candelas);
		Light->SetIntensity(Candelas);
		Light->SetAttenuationRadius(700.f);
		Light->SetCastShadows(false);
		Light->SetLightColor(Color);
		Light->SetRelativeLocation(Offset);
		return Light;
	};
	// Relative to the capture camera (X towards the operative): key high on the right, fill low on the left.
	KeyLight = MakeLight(TEXT("KeyLight"), 40.f, FVector(40.f, 120.f, 90.f), FLinearColor(1.f, 0.93f, 0.85f));
	FillLight = MakeLight(TEXT("FillLight"), 12.f, FVector(20.f, -140.f, -20.f), FLinearColor(0.7f, 0.8f, 1.f));
}

UTextureRenderTarget2D* AOperativePortraitCapture::GetRenderTarget()
{
	if (!RenderTarget)
	{
		RenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("PortraitTarget"));
		RenderTarget->ClearColor = FLinearColor(0.02f, 0.025f, 0.03f, 1.f);
		RenderTarget->InitCustomFormat(FMath::Max(16, Resolution.X), FMath::Max(16, Resolution.Y), PF_B8G8R8A8, false);
		RenderTarget->UpdateResourceImmediate(true);
		Capture->TextureTarget = RenderTarget;
	}
	return RenderTarget;
}

void AOperativePortraitCapture::SetTarget(AOperativeCharacter* Operative)
{
	Target = Operative;
	Capture->ShowOnlyActors.Reset();
	Capture->ShowOnlyComponents.Reset();
	Capture->bCaptureEveryFrame = Operative != nullptr;
	if (!Operative || !Operative->GetMesh())
	{
		return;
	}
	GetRenderTarget();
	// Only the body and what hangs on it (weapon, backpack, accessories under the skeletal mesh) - not the HUD-ish helpers on the
	// capsule (selection ring, facing marker, vision cone, order lines).
	auto AddVisible = [this](UPrimitiveComponent* Primitive)
	{
		if (Primitive && Primitive->IsVisible() && !Primitive->bHiddenInGame)
		{
			Capture->ShowOnlyComponents.Add(Primitive);
		}
	};
	AddVisible(Operative->GetMesh());
	TArray<USceneComponent*> BodyParts;
	Operative->GetMesh()->GetChildrenComponents(true, BodyParts);
	for (USceneComponent* Child : BodyParts)
	{
		AddVisible(Cast<UPrimitiveComponent>(Child));
	}
	TArray<AActor*> Attached;
	Operative->GetAttachedActors(Attached, true, true); // weapon / backpack actors he carries
	for (const AActor* Actor : Attached)
	{
		TInlineComponentArray<UPrimitiveComponent*> Primitives(Actor);
		for (UPrimitiveComponent* Primitive : Primitives)
		{
			AddVisible(Primitive);
		}
	}
	Capture->FOVAngle = FieldOfView;
	Place();
}

void AOperativePortraitCapture::Place()
{
	const AOperativeCharacter* Operative = Target.Get();
	if (!Operative)
	{
		return;
	}
	// A three-quarter view from in front of him, slightly above the chest.
	const FVector Forward = Operative->GetActorForwardVector().GetSafeNormal2D().RotateAngleAxis(-22.f, FVector::UpVector);
	const FVector Focus = Operative->GetActorLocation() + FVector(0.f, 0.f, FocusHeightCm);
	const FVector Eye = Focus + Forward * DistanceCm + FVector(0.f, 0.f, 12.f);
	SetActorLocationAndRotation(Eye, (Focus - Eye).Rotation());
}

void AOperativePortraitCapture::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Place();
}
