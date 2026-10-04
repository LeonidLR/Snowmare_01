#include "Combat/CombatFeedbackActor.h"
#include "Components/MeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const FName FeedbackColorParam(TEXT("Color"));
	const FName FeedbackIntensityParam(TEXT("Intensity"));
}

ACombatFeedbackActor::ACombatFeedbackActor()
{
	PrimaryActorTick.bCanEverTick = true;
	SetCanBeDamaged(false);

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetVisibility(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cylinder.Succeeded())
	{
		Mesh->SetStaticMesh(Cylinder.Object);
	}

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Root);
	Light->SetCastShadows(false);
	Light->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GlowAsset(TEXT("/Game/VFX/Materials/M_CombatFeedback.M_CombatFeedback"));
	if (GlowAsset.Succeeded())
	{
		GlowMaterial = GlowAsset.Object;
	}
}

UMaterialInstanceDynamic* ACombatFeedbackActor::MakeGlow(const FLinearColor& Color, float Intensity)
{
	if (!GlowMaterial)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(GlowMaterial, this);
	Instance->SetVectorParameterValue(FeedbackColorParam, Color);
	Instance->SetScalarParameterValue(FeedbackIntensityParam, Intensity);
	return Instance;
}

void ACombatFeedbackActor::SetupBeam(const FVector& Start, const FVector& End, float Thickness, const FLinearColor& Color, float Intensity, float FadeTime)
{
	const FVector Delta = End - Start;
	const float Length = Delta.Size();
	if (Length < 1.f)
	{
		return;
	}
	// The engine cylinder is 100 cm tall along Z and 100 cm wide, centred on its pivot.
	SetActorLocation(Start);
	Mesh->SetWorldLocationAndRotation((Start + End) * 0.5f, FRotationMatrix::MakeFromZ(Delta / Length).Rotator());
	Mesh->SetWorldScale3D(FVector(Thickness / 100.f, Thickness / 100.f, Length / 100.f));
	Glow = MakeGlow(Color, Intensity);
	Mesh->SetMaterial(0, Glow);
	Mesh->SetVisibility(true);
	GlowIntensity = Intensity;
	GlowFade = FadeTime;
}

void ACombatFeedbackActor::SetupDisc(const FVector& Location, float Radius, float Height, const FLinearColor& Color, float Intensity, float FadeTime)
{
	SetActorLocation(Location);
	Mesh->SetWorldLocationAndRotation(Location, FRotator::ZeroRotator);
	Mesh->SetWorldScale3D(FVector(Radius / 50.f, Radius / 50.f, Height / 100.f));
	Glow = MakeGlow(Color, Intensity);
	Mesh->SetMaterial(0, Glow);
	Mesh->SetVisibility(true);
	GlowIntensity = Intensity;
	GlowFade = FadeTime;
	bPersistent = FadeTime <= 0.f;
}

void ACombatFeedbackActor::SetupLight(const FLinearColor& Color, float Intensity, float Radius, float FadeTime)
{
	Light->SetLightColor(Color);
	Light->SetIntensity(Intensity);
	Light->SetAttenuationRadius(Radius);
	Light->SetVisibility(true);
	LightIntensity = Intensity;
	LightFade = FadeTime;
}

void ACombatFeedbackActor::SetupOverlayFlash(AActor* Target, const FLinearColor& Color, float Intensity, float FadeTime, UMaterialInterface* Material)
{
	if (!Target)
	{
		return;
	}
	if (Material)
	{
		OverlayGlow = UMaterialInstanceDynamic::Create(Material, this);
		OverlayGlow->SetVectorParameterValue(FeedbackColorParam, Color);
		OverlayGlow->SetScalarParameterValue(FeedbackIntensityParam, Intensity);
	}
	else
	{
		OverlayGlow = MakeGlow(Color, Intensity);
	}
	if (!OverlayGlow)
	{
		return;
	}
	TArray<UMeshComponent*> Meshes;
	Target->GetComponents(Meshes);
	for (UMeshComponent* TargetMesh : Meshes)
	{
		if (!TargetMesh->IsVisible())
		{
			continue;
		}
		// Overlapping flashes on one mesh: remember the mesh's own overlay, not the running flash's glow — that glow is
		// destroyed with its flash actor, and restoring it later left the mesh pointing at a freed material (GC crash).
		UMaterialInterface* Previous = TargetMesh->GetOverlayMaterial();
		if (const ACombatFeedbackActor* Running = Previous ? Cast<ACombatFeedbackActor>(Previous->GetOuter()) : nullptr)
		{
			Previous = Running->GetSavedOverlay(TargetMesh);
		}
		OverlaidMeshes.Add(TargetMesh);
		PreviousOverlays.Add(Previous);
		TargetMesh->SetOverlayMaterial(OverlayGlow);
	}
	OverlayIntensity = Intensity;
	OverlayFade = FadeTime;
}

UMaterialInterface* ACombatFeedbackActor::GetSavedOverlay(const UMeshComponent* TargetMesh) const
{
	for (int32 Index = 0; Index < OverlaidMeshes.Num(); ++Index)
	{
		if (OverlaidMeshes[Index].Get() == TargetMesh)
		{
			return PreviousOverlays[Index];
		}
	}
	return nullptr;
}

void ACombatFeedbackActor::RestoreOverlays()
{
	for (int32 Index = 0; Index < OverlaidMeshes.Num(); ++Index)
	{
		UMeshComponent* TargetMesh = OverlaidMeshes[Index].Get();
		if (TargetMesh && TargetMesh->GetOverlayMaterial() == OverlayGlow)
		{
			TargetMesh->SetOverlayMaterial(PreviousOverlays[Index]);
		}
	}
	OverlaidMeshes.Reset();
	PreviousOverlays.Reset();
}

void ACombatFeedbackActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	bool bAlive = bPersistent;
	auto Fade = [this, &bAlive](float Start, float Duration) -> float
	{
		if (Duration <= 0.f)
		{
			return 0.f;
		}
		const float Alpha = FMath::Clamp(1.f - Age / Duration, 0.f, 1.f);
		bAlive |= Alpha > 0.f;
		return Start * Alpha;
	};
	if (Glow && !bPersistent)
	{
		Glow->SetScalarParameterValue(FeedbackIntensityParam, Fade(GlowIntensity, GlowFade));
	}
	if (LightFade > 0.f)
	{
		Light->SetIntensity(Fade(LightIntensity, LightFade));
	}
	if (OverlayGlow && OverlayFade > 0.f)
	{
		OverlayGlow->SetScalarParameterValue(FeedbackIntensityParam, Fade(OverlayIntensity, OverlayFade));
		if (Age >= OverlayFade)
		{
			RestoreOverlays();
		}
	}
	if (!bAlive)
	{
		Destroy();
	}
}

void ACombatFeedbackActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RestoreOverlays();
	Super::EndPlay(EndPlayReason);
}
